// swaptest.c  – rigorous disk-backed swap tests
//
// Tests:
//   T1  Basic eviction + swap-in correctness (2× MAX_FRAMES pages)
//   T2  Byte-level pattern integrity across multiple eviction rounds
//   T3  Repeated write-evict-read cycle (same VA, fresh data each round)
//   T4  Confirms disk_writes > 0  (eviction actually hit the disk)
//   T5  Confirms disk_reads  > 0  (swap-in  actually hit the disk)
//   T6  Stats monotonically increase across rounds
//   T7  Fork: child and parent both survive with swapped pages intact
//   T8  sbrk shrink frees swap slots (no slot leak over many cycles)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PAGE_SIZE    4096
// 2× the MAX_FRAMES cap (32) so eviction is guaranteed
#define PRESSURE     450
// smaller set we verify precisely
#define VERIFY_PAGES 400

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


// ── helpers ────────────────────────────────────────────────────────────────

static int total_pass = 0;
static int total_fail = 0;

static void
result(const char *name, int ok)
{
  if(ok){ printf("  PASS: %s\n", name); total_pass++; }
  else  { printf("  FAIL: %s\n", name); total_fail++; }
}

// fill page i with a deterministic pattern seeded by `round`
static void
fill(char *p, int page, int round)
{
  for(int j = 0; j < PAGE_SIZE; j++)
    p[j] = (char)((page * 13 + j * 7 + round * 31) & 0xFF);
}

static int
check(char *p, int page, int round)
{
  for(int j = 0; j < PAGE_SIZE; j++){
    char expected = (char)((page * 13 + j * 7 + round * 31) & 0xFF);
    if(p[j] != expected) return 0;
  }
  return 1;
}

// ── T1: basic correctness ──────────────────────────────────────────────────
static void
t1_basic(void)
{
  printf("\n[T1] Basic eviction + swap-in correctness\n");

  char *base = sbrk(VERIFY_PAGES * PAGE_SIZE);
  if(base == (char*)-1){ printf("  FAIL: sbrk\n"); total_fail++; return; }

  // write patterns to all pages (forces eviction of early pages)
  for(int i = 0; i < VERIFY_PAGES; i++)
    fill(base + i * PAGE_SIZE, i, 0);

  // allocate pressure pages to push the verify-set out of RAM
  char *pressure = sbrk(PRESSURE * PAGE_SIZE);
  if(pressure != (char*)-1){
    for(int i = 0; i < PRESSURE; i++)
      pressure[i * PAGE_SIZE] = (char)i;   // touch each page
  }

  // now read back the verify set – must swap them back in
  int ok = 1;
  for(int i = 0; i < VERIFY_PAGES; i++)
    if(!check(base + i * PAGE_SIZE, i, 0)){ ok = 0; break; }
  result("all pages correct after swap-in", ok);

  // stats: writes and reads must both be non-zero
  struct vmstats s;
  getvmstats(getpid(), &s);
  result("disk_writes > 0 (eviction happened)", s.disk_writes > 0);
  result("disk_reads  > 0 (swap-in  happened)", s.disk_reads  > 0);

  if(pressure != (char*)-1)
    sbrk(-(PRESSURE * PAGE_SIZE));
  sbrk(-(VERIFY_PAGES * PAGE_SIZE));
}

// ── T2: byte-level integrity ───────────────────────────────────────────────
static void
t2_byte_integrity(void)
{
  printf("\n[T2] Byte-level pattern integrity\n");

  // use a unique pattern per byte position to detect partial overwrites
  char *base = sbrk(VERIFY_PAGES * PAGE_SIZE);
  if(base == (char*)-1){ printf("  FAIL: sbrk\n"); total_fail++; return; }

  for(int i = 0; i < VERIFY_PAGES; i++)
    for(int j = 0; j < PAGE_SIZE; j++)
      base[i * PAGE_SIZE + j] = (char)((i * 97 + j * 3 + 0xAB) & 0xFF);

  // pressure
  char *p = sbrk(PRESSURE * PAGE_SIZE);
  if(p != (char*)-1){
    for(int i = 0; i < PRESSURE; i++) p[i * PAGE_SIZE] = 0;
    sbrk(-(PRESSURE * PAGE_SIZE));
  }

  int errors = 0;
  int first_err_page = -1, first_err_byte = -1;
  for(int i = 0; i < VERIFY_PAGES; i++){
    for(int j = 0; j < PAGE_SIZE; j++){
      char expected = (char)((i * 97 + j * 3 + 0xAB) & 0xFF);
      if(base[i * PAGE_SIZE + j] != expected){
        if(first_err_page < 0){ first_err_page = i; first_err_byte = j; }
        errors++;
      }
    }
  }
  if(errors > 0)
    printf("  first error at page=%d byte=%d\n", first_err_page, first_err_byte);
  result("zero byte-level errors across all pages", errors == 0);

  sbrk(-(VERIFY_PAGES * PAGE_SIZE));
}

// ── T3: repeated write-evict-read (same VA, fresh data) ───────────────────
static void
t3_repeated_cycles(void)
{
  printf("\n[T3] Repeated write-evict-read cycles (3 rounds)\n");

#define CYCLE_PAGES  40
#define ROUNDS        3

  char *base = sbrk(CYCLE_PAGES * PAGE_SIZE);
  if(base == (char*)-1){ printf("  FAIL: sbrk\n"); total_fail++; return; }

  for(int r = 0; r < ROUNDS; r++){
    // write round-specific pattern
    for(int i = 0; i < CYCLE_PAGES; i++)
      fill(base + i * PAGE_SIZE, i, r);

    // evict by touching pressure pages
    char *p = sbrk(PRESSURE * PAGE_SIZE);
    if(p != (char*)-1){
      for(int i = 0; i < PRESSURE; i++) p[i * PAGE_SIZE] = (char)(r + i);
      sbrk(-(PRESSURE * PAGE_SIZE));
    }

    // verify: must see round r's data, not round r-1's
    int ok = 1;
    for(int i = 0; i < CYCLE_PAGES; i++)
      if(!check(base + i * PAGE_SIZE, i, r)){ ok = 0; break; }

    char label[32];
    label[0] = 'r'; label[1] = 'o'; label[2] = 'u'; label[3] = 'n';
    label[4] = 'd'; label[5] = ' '; label[6] = (char)('0' + r);
    label[7] = ' '; label[8] = 'c'; label[9] = 'o'; label[10] = 'r';
    label[11] = 'r'; label[12] = 'e'; label[13] = 'c'; label[14] = 't';
    label[15] = '\0';
    result(label, ok);
  }

  sbrk(-(CYCLE_PAGES * PAGE_SIZE));
}

// ── T4: stats monotonically increase ──────────────────────────────────────
static void
t4_stats_monotonic(void)
{
  printf("\n[T4] Stats monotonically increase across workloads\n");

  struct vmstats s0, s1, s2;
  getvmstats(getpid(), &s0);

  // first workload
  char *b = sbrk(VERIFY_PAGES * PAGE_SIZE);
  if(b != (char*)-1){
    for(int i = 0; i < VERIFY_PAGES; i++) fill(b + i*PAGE_SIZE, i, 10);
    char *p = sbrk(PRESSURE * PAGE_SIZE);
    if(p != (char*)-1){ for(int i=0;i<PRESSURE;i++) p[i*PAGE_SIZE]=0; sbrk(-(PRESSURE*PAGE_SIZE)); }
    for(int i = 0; i < VERIFY_PAGES; i++) (void)b[i*PAGE_SIZE]; // force reads
    getvmstats(getpid(), &s1);
    sbrk(-(VERIFY_PAGES * PAGE_SIZE));
  }

  // second workload
  b = sbrk(VERIFY_PAGES * PAGE_SIZE);
  if(b != (char*)-1){
    for(int i = 0; i < VERIFY_PAGES; i++) fill(b + i*PAGE_SIZE, i, 20);
    char *p = sbrk(PRESSURE * PAGE_SIZE);
    if(p != (char*)-1){ for(int i=0;i<PRESSURE;i++) p[i*PAGE_SIZE]=0; sbrk(-(PRESSURE*PAGE_SIZE)); }
    for(int i = 0; i < VERIFY_PAGES; i++) (void)b[i*PAGE_SIZE];
    getvmstats(getpid(), &s2);
    sbrk(-(VERIFY_PAGES * PAGE_SIZE));
  }

  result("writes increase after 1st workload", s1.disk_writes > s0.disk_writes);
  result("reads  increase after 1st workload", s1.disk_reads  > s0.disk_reads);
  result("writes increase after 2nd workload", s2.disk_writes > s1.disk_writes);
  result("reads  increase after 2nd workload", s2.disk_reads  > s1.disk_reads);
}

// ── T5: fork – parent and child both see correct data ────────────────────
static void
t5_fork(void)
{
  printf("\n[T5] Fork: parent and child survive swap independently\n");

#define FORK_PAGES 48

  char *base = sbrk(FORK_PAGES * PAGE_SIZE);
  if(base == (char*)-1){ printf("  FAIL: sbrk\n"); total_fail++; return; }

  for(int i = 0; i < FORK_PAGES; i++)
    fill(base + i * PAGE_SIZE, i, 99);

  // evict before fork so child copies swapped PTEs
  char *p = sbrk(PRESSURE * PAGE_SIZE);
  if(p != (char*)-1){ for(int i=0;i<PRESSURE;i++) p[i*PAGE_SIZE]=0; sbrk(-(PRESSURE*PAGE_SIZE)); }

  int pid = fork();
  if(pid < 0){ printf("  FAIL: fork\n"); total_fail++; sbrk(-(FORK_PAGES*PAGE_SIZE)); return; }

  if(pid == 0){
    // child: verify its copy
    int ok = 1;
    for(int i = 0; i < FORK_PAGES; i++)
      if(!check(base + i * PAGE_SIZE, i, 99)){ ok = 0; break; }
    // child writes different pattern to its copy
    for(int i = 0; i < FORK_PAGES; i++)
      fill(base + i * PAGE_SIZE, i, 77);
    exit(ok ? 0 : 1);
  }

  // parent: wait and check exit status
  int status = 0;
  wait(&status);
  result("child sees correct data after fork+swap", status == 0);

  // parent must still see round-99 data (COW / copy semantics)
  int ok = 1;
  for(int i = 0; i < FORK_PAGES; i++)
    if(!check(base + i * PAGE_SIZE, i, 99)){ ok = 0; break; }
  result("parent data unaffected by child writes", ok);

  sbrk(-(FORK_PAGES * PAGE_SIZE));
}

// ── T6: swap-slot leak detection ──────────────────────────────────────────
static void
t6_slot_leak(void)
{
  printf("\n[T6] Swap-slot leak: many alloc/free cycles\n");

  // run 5 independent alloc/evict/free cycles; if slots leak, a later
  // cycle will panic or return -1 from swap_alloc
  int all_ok = 1;
#define LEAK_PAGES 48
#define LEAK_ROUNDS 5

  for(int r = 0; r < LEAK_ROUNDS; r++){
    char *b = sbrk(LEAK_PAGES * PAGE_SIZE);
    if(b == (char*)-1){ all_ok = 0; break; }

    for(int i = 0; i < LEAK_PAGES; i++)
      fill(b + i * PAGE_SIZE, i, r);

    // pressure
    char *p = sbrk(PRESSURE * PAGE_SIZE);
    if(p != (char*)-1){ for(int i=0;i<PRESSURE;i++) p[i*PAGE_SIZE]=(char)r; sbrk(-(PRESSURE*PAGE_SIZE)); }

    // verify
    for(int i = 0; i < LEAK_PAGES; i++)
      if(!check(b + i * PAGE_SIZE, i, r)){ all_ok = 0; break; }

    // free — must release swap slots
    sbrk(-(LEAK_PAGES * PAGE_SIZE));
    if(!all_ok) break;
  }
  result("no slot leak across 5 alloc/free cycles", all_ok);
}

// ── T7: single-page precision ──────────────────────────────────────────────
static void
t7_single_page(void)
{
  printf("\n[T7] Single-page precision (first byte, last byte, middle)\n");

  char *b = sbrk(PAGE_SIZE);
  if(b == (char*)-1){ printf("  FAIL: sbrk\n"); total_fail++; return; }

  b[0]            = 0xDE;
  b[PAGE_SIZE/2]  = 0xAD;
  b[PAGE_SIZE-1]  = 0xBE;

  // pressure to evict the one page
  char *p = sbrk(PRESSURE * PAGE_SIZE);
  if(p != (char*)-1){ for(int i=0;i<PRESSURE;i++) p[i*PAGE_SIZE]=0; sbrk(-(PRESSURE*PAGE_SIZE)); }

  result("first byte correct after swap-in",  (unsigned char)b[0]           == 0xDE);
  result("middle byte correct after swap-in", (unsigned char)b[PAGE_SIZE/2] == 0xAD);
  result("last byte correct after swap-in",   (unsigned char)b[PAGE_SIZE-1] == 0xBE);

  sbrk(-PAGE_SIZE);
}

// ── main ───────────────────────────────────────────────────────────────────
int
main(void)
{
  printf("=== swaptest (rigorous) ===\n");

  t1_basic();
  t2_byte_integrity();
  t3_repeated_cycles();
  t4_stats_monotonic();
  t5_fork();
  t6_slot_leak();
  t7_single_page();

  printf("\n=== RESULTS: %d passed, %d failed ===\n", total_pass, total_fail);
  exit(total_fail == 0 ? 0 : 1);
}