#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


/* ---------------- CPU BOUND ----------------
   Pure computation with no syscalls.
   Should gradually move to lower queues.
*/
void cpu_bound() {
  volatile int x = 0;

  while (1) {
    for (int i = 0; i < 10000000; i++)
      x += i;
  }
}

/* ---------------- SYSCALL HEAVY ----------------
   Makes many syscalls.
   Should remain in higher priority queues.
*/
void syscall_heavy() {
  while (1) {
    for (int i = 0; i < 1000; i++)
      getpid();
  }
}

/* ---------------- MIXED WORKLOAD ----------------
   Alternates CPU and syscalls.
   Should settle in middle queues.
*/
void mixed_workload() {
  volatile int x = 0;

  while (1) {
    /* CPU phase */
    for (int i = 0; i < 5000000; i++)
      x++;

    /* syscall phase */
    for (int i = 0; i < 200; i++)
      getpid();
  }
}

/* ----------- Print scheduler stats ----------- */
void print_stats(int pid) {
  struct mlfqinfo info;

  if (getmlfqinfo(pid, &info) == 0) {
    printf("PID %d: Level=%d | Ticks=[%d %d %d %d] | Scheduled=%d | Syscalls=%d\n",
           pid,
           info.level,
           info.ticks[0],
           info.ticks[1],
           info.ticks[2],
           info.ticks[3],
           info.times_scheduled,
           info.total_syscalls);
  }
}

/* ---------------- MAIN TEST ---------------- */
int main() {

  int pids[3];
  char *names[] = {"CPU-BOUND", "SYSCALL-HEAVY", "MIXED"};

  /* spawn processes */
  for (int i = 0; i < 3; i++) {
    int pid = fork();

    if (pid == 0) {
      if (i == 0)
        cpu_bound();
      else if (i == 1)
        syscall_heavy();
      else
        mixed_workload();

      exit(0);
    }

    pids[i] = pid;
  }

  /* monitor behaviour */
  for (int t = 0; t < 15; t++) {

    pause(50);   // allow scheduler to run

    printf("\n========== INTERVAL %d ==========\n", t);

    for (int i = 0; i < 3; i++) {
      printf("%s -> ", names[i]);
      print_stats(pids[i]);
    }
  }

  /* kill children */
  for (int i = 0; i < 3; i++)
    kill(pids[i]);

  for (int i = 0; i < 3; i++)
    wait(0);

  exit(0);
}