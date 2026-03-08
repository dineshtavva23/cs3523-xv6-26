#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/*
============================================================
SC-MLFQ Experimental Evaluation Program
============================================================

This program evaluates the behavior of the SC-MLFQ scheduler
under three workloads:

1. CPU-bound process
   Expected: gradual demotion to lower queues.

2. Syscall-heavy process
   Expected: remain in higher queues due to frequent syscalls.

3. Mixed workload
   Expected: remain in intermediate queues.

Scheduler statistics are periodically printed using
getmlfqinfo().
============================================================
*/

/* ---------------- CPU BOUND ----------------
   Pure computation with no syscalls.
   Expected behaviour:
   Level 0 → Level 1 → Level 2 → Level 3
*/
void cpu_bound(){
  volatile int x = 0;

  while(1){
    for(int i=0;i <10000000;i++){
      x +=i;
    }
  }
}

/* ---------------- SYSCALL HEAVY ----------------
   Repeated system calls.
   Expected behaviour:
   Process remains at high priority.
*/
void syscall_heavy(){
  while(1){
    for(int i=0;i<1000;i++){
      getpid();
    }
  }
}

/* ---------------- MIXED WORKLOAD ----------------
   Alternates CPU work and syscalls.
   Expected behaviour:
   Intermediate queue levels.
*/
void mixed_workload() {
  volatile int x = 0;
  while(1){
    for(int i =0;i<5000000;i++){
      x++;
    }

    for(int i=0;i<200;i++){
      getpid();
    }
  }
}


/* ---------- Print formatted statistics ---------- */
void print_stats(int pid){

  struct mlfqinfo info;
  if (getmlfqinfo(pid, &info) == 0){

    printf("PID:%d | Level:%d | "
           "Ticks[L0:%d L1:%d L2:%d L3:%d] | "
           "Scheduled:%d | Syscalls:%d\n",
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

int main(void){
  int pids[3];

  char *names[]={
    "CPU-BOUND",
    "SYSCALL-HEAVY",
    "MIXED"
  };

  printf("\n--------------------------------------------\n");
  printf(" SC-MLFQ Scheduler Experimental Evaluation\n");
  printf("---------------------------------------------\n\n");


  /* Spawn processes */
  for (int i =0;i<3;i++){

    int pid =fork();

    if (pid ==0){
      if(i==0){
        cpu_bound();
      }
      else if(i==1){
        syscall_heavy();
      }
      else{
        mixed_workload();
      }
      exit(0);
    }
    pids[i] =pid;
  }


  /* Monitor scheduler behaviour */

  for (int t =0;t<15;t++){
    pause(50);
    printf("\n------------------------------\n");
    printf("Measurement Interval %d\n", t);
    printf("------------------------------\n");
    for (int i=0;i<3;i++){
      printf("%s -> ",names[i]);
      print_stats(pids[i]);
    }
  }


  /* Terminate child processes */
  for (int i = 0;i<3;i++){
    kill(pids[i]);
  }
  for(int i =0;i<3;i++){
    wait(0);
  }
  printf("\nExperiment completed.\n");
  exit(0);
}