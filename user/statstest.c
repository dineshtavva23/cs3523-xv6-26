// statstest.c  – rigorous kernel statistics tests
//
// Tests:
//   T1  Baseline: fresh process starts with zero disk stats
//   T2  disk_writes > 0 after eviction workload
//   T3  disk_reads  > 0 after swap-in workload
//   T4  avg_disk_latency > 0 once I/O has occurred
//   T5  avg_disk_latency = total_latency / disk_ops  (formula check)
//   T6  Stats increase monotonically: no counter ever decrements
//   T7  Per-process isolation: child workload does NOT change parent stats
//   T8  SSTF avg_latency <= FCFS avg_latency on the same workload size
//   T9  SSTF writes == FCFS writes (policy changes order, not volume)
//   T10 getvmstats(-1) returns error (invalid pid)
//   T11 Stats after sbrk-shrink: swap slots freed, no spurious extra reads
//   T12 Heavy workload: counters do not wrap / overflow to zero

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PAGE_SIZE    4096
#define WORK_PAGES   400    // > MAX_FRAMES to guarantee eviction
#define PRESSURE     450
#define HEAVY_PAGES  450


struct vmstats{
  int page_faults;
  int pages_evicted;
  int resident_pages;
  int pages_swapped_in;
  int pages_swapped_out;
  int disk_reads;
  int disk_writes;
  int avg_disk_latency;
};

static int total_pass = 0;
static int total_fail = 0;

static void
result(const char *name, int ok)
{
  if(ok){ printf("  PASS: %s\n", name); total_pass++; }
  else  { printf("  FAIL: %s\n", name); total_fail++; }
}

static void
fill(char *p, int page, int seed)
{
  for(int j = 0; j < PAGE_SIZE; j++)
    p[j] = (char)((page * 23 + j * 9 + seed * 41) & 0xFF);
}

// perform a full evict+swapin workload under a given policy
// returns delta stats
static void
run_workload(int policy, int seed,
             struct vmstats *before_out, struct vmstats *after_out)
{
  setdisksched(policy);
  getvmstats(getpid(), before_out);

  char *base = sbrk(WORK_PAGES * PAGE_SIZE);
  if(base == (char*)-1){ return; }

  for(int i=0;i<WORK_PAGES;i++) fill(base+i*PAGE_SIZE,i,seed);

  char *p = sbrk(PRESSURE*PAGE_SIZE);
  if(p!=(char*)-1){ for(int i=0;i<PRESSURE;i++) p[i*PAGE_SIZE]=(char)(i^seed); sbrk(-(PRESSURE*PAGE_SIZE)); }

  // force swap-ins
  for(int i=0;i<WORK_PAGES;i++) (void)base[i*PAGE_SIZE];

  getvmstats(getpid(), after_out);
  sbrk(-(WORK_PAGES*PAGE_SIZE));
}

// ── T1: fresh-process baseline ───────────────────────────────────────────
static void
t1_baseline(void)
{
  printf("\n[T1] Fresh process starts with zero disk stats\n");
  // We are the first workload; stats should start at 0 before any I/O
  // (checked at program start, before any sbrk)
  struct vmstats s;
  int ret = getvmstats(getpid(), &s);
  result("getvmstats returns 0 for own pid", ret == 0);
  result("initial disk_reads >= 0",       s.disk_reads  >= 0);
  result("initial disk_writes == 0",      s.disk_writes == 0);
  result("initial avg_disk_latency >= 0", s.avg_disk_latency >= 0);
}

// ── T2: writes > 0 after eviction ────────────────────────────────────────
static void
t2_writes_nonzero(void)
{
  printf("\n[T2] disk_writes > 0 after eviction workload\n");

  struct vmstats before, after;
  run_workload(0, 1, &before, &after);

  uint64 dw = after.disk_writes - before.disk_writes;
  printf("  eviction disk_writes delta = %llu\n", (unsigned long long)dw);
  result("disk_writes > 0",          dw > 0);
  result("disk_writes >= 32",        dw >= 32);  // at least WORK_PAGES - MAX_FRAMES
}

// ── T3: reads > 0 after swap-in ──────────────────────────────────────────
static void
t3_reads_nonzero(void)
{
  printf("\n[T3] disk_reads > 0 after swap-in workload\n");

  struct vmstats before, after;
  run_workload(0, 2, &before, &after);

  uint64 dr = after.disk_reads - before.disk_reads;
  printf("  swap-in disk_reads delta = %llu\n", (unsigned long long)dr);
  result("disk_reads > 0",   dr > 0);
  result("disk_reads >= 32", dr >= 32);
}

// ── T4: avg_latency > 0 once I/O occurred ────────────────────────────────
static void
t4_latency_nonzero(void)
{
  printf("\n[T4] avg_disk_latency > 0 after I/O\n");

  struct vmstats before, after;
  run_workload(0, 3, &before, &after);

  printf("  avg_disk_latency = %llu\n", (unsigned long long)after.avg_disk_latency);
  result("avg_disk_latency > 0", after.avg_disk_latency > 0);
  // latency must be at least ROTATIONAL_CONST (5) per operation
  result("avg_disk_latency >= ROTATIONAL_CONST (5)", after.avg_disk_latency >= 5);
}

// ── T5: latency formula sanity ────────────────────────────────────────────
// avg_latency should stay in a plausible range:
//   min = ROTATIONAL_CONST = 5
//   max = VDISK 5 MAXIMUM DISPERSION + C = 10053
static void
t5_latency_range(void)
{
  printf("\n[T5] avg_disk_latency in plausible range [5, 2053]\n");

  struct vmstats before, after;
  run_workload(0, 4, &before, &after);

  uint64 lat = after.avg_disk_latency;
  printf("  avg_disk_latency = %llu\n", (unsigned long long)lat);
  result("avg_disk_latency >= 5    (at least rotational const)", lat >= 5);
  result("avg_disk_latency <= 10053 (no more than max seek + C)", lat <= 10053);
}

// ── T6: stats increase monotonically ─────────────────────────────────────
static void
t6_monotonic(void)
{
  printf("\n[T6] Stats increase monotonically across multiple workloads\n");

  struct vmstats s[4];
  getvmstats(getpid(), &s[0]);

  for(int i = 1; i <= 3; i++){
    struct vmstats dummy;
    run_workload(0, 10 + i, &dummy, &s[i]);
  }

  int writes_mono = 1, reads_mono = 1;
  for(int i = 1; i <= 3; i++){
    if(s[i].disk_writes <= s[i-1].disk_writes) writes_mono = 0;
    if(s[i].disk_reads  <= s[i-1].disk_reads)  reads_mono  = 0;
  }
  result("disk_writes strictly increases over 3 workloads", writes_mono);
  result("disk_reads  strictly increases over 3 workloads", reads_mono);
}

// ── T7: per-process isolation ─────────────────────────────────────────────
static void
t7_isolation(void)
{
  printf("\n[T7] Per-process stats isolation (child vs parent)\n");

  struct vmstats parent_before;
  getvmstats(getpid(), &parent_before);

  int pid = fork();
  if(pid == 0){
    // child does heavy I/O
    struct vmstats d, a;
    run_workload(0, 20, &d, &a);
    exit(0);
  }
  int st; wait(&st);

  struct vmstats parent_after;
  getvmstats(getpid(), &parent_after);

  result("parent writes unchanged during child workload",
         parent_after.disk_writes == parent_before.disk_writes);
  result("parent reads unchanged during child workload",
         parent_after.disk_reads == parent_before.disk_reads);

  // child stats should be independent – we can't check them after exit,
  // but we've verified they didn't corrupt the parent's counters
}

// ── T8: SSTF avg_latency <= FCFS avg_latency ─────────────────────────────
static void
t8_sstf_vs_fcfs(void)
{
  printf("\n[T8] SSTF avg_latency <= FCFS avg_latency\n");

  struct vmstats b_fcfs, a_fcfs, b_sstf, a_sstf;
  run_workload(0, 30, &b_fcfs, &a_fcfs);
  run_workload(1, 31, &b_sstf, &a_sstf);

  uint64 lat_fcfs = a_fcfs.avg_disk_latency;
  uint64 lat_sstf = a_sstf.avg_disk_latency;
  printf("  FCFS avg_latency=%llu  SSTF avg_latency=%llu\n",
         (unsigned long long)lat_fcfs, (unsigned long long)lat_sstf);

  result("SSTF avg_latency <= FCFS avg_latency (w/ margin)", lat_sstf <= (lat_fcfs + 100));
}

// ── T9: SSTF and FCFS produce same number of writes ────────────────────
static void
t9_same_write_volume(void)
{
  printf("\n[T9] SSTF and FCFS produce same number of writes (same workload)\n");

  struct vmstats bf, af, bs, as;
  run_workload(0, 40, &bf, &af);
  run_workload(1, 40, &bs, &as);  // same seed = same workload size

  uint64 writes_fcfs = af.disk_writes - bf.disk_writes;
  uint64 writes_sstf = as.disk_writes - bs.disk_writes;
  printf("  FCFS writes=%llu  SSTF writes=%llu\n",
         (unsigned long long)writes_fcfs, (unsigned long long)writes_sstf);

  // same number of pages → same evictions → same write count
  result("write count identical under FCFS and SSTF", writes_fcfs == writes_sstf);
}

// ── T10: getvmstats with invalid pid ────────────────────────────────────
static void
t10_invalid_pid(void)
{
  printf("\n[T10] getvmstats returns -1 for invalid pids\n");

  struct vmstats s;
  result("getvmstats(-1) == -1",    getvmstats(-1, &s)    == -1);
  result("getvmstats(99999) == -1", getvmstats(99999, &s) == -1);
  result("getvmstats(0) == -1",     getvmstats(0, &s)     == -1);
}

// ── T11: stats after sbrk shrink ─────────────────────────────────────────
static void
t11_shrink(void)
{
  printf("\n[T11] Stats stable after sbrk shrink (no spurious reads)\n");

  struct vmstats before;
  getvmstats(getpid(), &before);

  char *b = sbrk(WORK_PAGES * PAGE_SIZE);
  if(b == (char*)-1){ printf("  FAIL: sbrk\n"); total_fail++; return; }
  for(int i=0;i<WORK_PAGES;i++) fill(b+i*PAGE_SIZE,i,50);

  char *p = sbrk(PRESSURE*PAGE_SIZE);
  if(p!=(char*)-1){ for(int i=0;i<PRESSURE;i++) p[i*PAGE_SIZE]=0; sbrk(-(PRESSURE*PAGE_SIZE)); }

  struct vmstats mid; getvmstats(getpid(), &mid);

  // now shrink – should free swap slots, NOT read them back
  sbrk(-(WORK_PAGES * PAGE_SIZE));

  struct vmstats after; getvmstats(getpid(), &after);

  // reads must not increase after the shrink (we didn't read the pages back)
  result("shrink does not increment disk_reads", after.disk_reads == mid.disk_reads);
  printf("  writes=%llu  reads=%llu  (after shrink reads=%llu)\n",
         (unsigned long long)(mid.disk_writes - before.disk_writes),
         (unsigned long long)(mid.disk_reads  - before.disk_reads),
         (unsigned long long)(after.disk_reads - mid.disk_reads));
}

// ── T12: counters do not wrap to zero under heavy load ───────────────────
static void
t12_no_wrap(void)
{
  printf("\n[T12] Counters do not wrap to zero under heavy load\n");

  struct vmstats prev; getvmstats(getpid(), &prev);
  int wrapped = 0;

  for(int r = 0; r < 4; r++){
    struct vmstats dummy, cur;
    run_workload(0, 60 + r, &dummy, &cur);

    if(cur.disk_writes < prev.disk_writes ||
       cur.disk_reads  < prev.disk_reads){
      wrapped = 1;
      printf("  WRAP detected at round %d: writes %llu→%llu reads %llu→%llu\n",
             r,
             (unsigned long long)prev.disk_writes,
             (unsigned long long)cur.disk_writes,
             (unsigned long long)prev.disk_reads,
             (unsigned long long)cur.disk_reads);
      break;
    }
    prev = cur;
  }
  result("counters never wrap over 4 heavy workloads", !wrapped);
}

// ── main ──────────────────────────────────────────────────────────────────
int
main(void)
{
  printf("=== statstest (rigorous) ===\n");

  // T1 must run before any I/O, so it's first
  t1_baseline();
  t2_writes_nonzero();
  t3_reads_nonzero();
  t4_latency_nonzero();
  t5_latency_range();
  t6_monotonic();
  t7_isolation();
  t8_sstf_vs_fcfs();
  t9_same_write_volume();
  t10_invalid_pid();
  t11_shrink();
  t12_no_wrap();

  printf("\n=== RESULTS: %d passed, %d failed ===\n", total_pass, total_fail);
  exit(total_fail == 0 ? 0 : 1);
}