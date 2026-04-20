#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

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

void print_stats(char *tag, int pid){
  struct vmstats st;
  if(getvmstats(pid, &st) == 0){
    printf("\n[%s] VM Stats (PID %d):\n", tag, pid);
    printf("  Page Faults: %d\n", st.page_faults);
    printf("  Evicted:     %d\n", st.pages_evicted);
    printf("  Resident:    %d\n", st.resident_pages);
    printf("  Swapped Out: %d\n", st.pages_swapped_out);
    printf("  Swapped In:  %d\n", st.pages_swapped_in);
    printf("  Disk Reads:  %d\n", st.disk_reads);
    printf("  Disk Writes: %d\n", st.disk_writes);
    printf("####################################\n");
  } else {
    printf("Could not get stats for PID %d\n", pid);
  }
}

void run_workload(){
  int pid = getpid();
  int num_pages = 800; // MUST BE > 768 pages to trigger EVICITON natively!
  uint64 size = num_pages * 4096;
  
  char *mem = sbrk(size);
  if(mem == (char*)-1){
     printf("Memory allocation failed!\n");
     exit(1);
  }

  printf("\n[Workload] Writing data sequentially to %d pages...\n", num_pages);
  
  // Trigger Swap Out
  for(int i = 0; i < num_pages; i++){
    mem[i*4096] = 'X'; 
    // Small delay to let MLFQ tick
    for(volatile int k=0; k<100; k++){}
  }

  print_stats("After Writing (Triggered Swap Outs)", pid);

  // Trigger Swap In and Verify RAID 5 Reconstruction
  printf("\n[Workload] Reading memory identically back to trigger Swap In...\n");
  printf("[Workload] (Simulated Disk 1 is down, triggering RAID 5 XOR Recovery)\n");
  for(int i = 0; i < num_pages; i++){
    char expected = mem[i*4096];
    if(expected != 'X'){
      printf("ERROR: Memory corrupted! RAID 5 Reconstruction failed at page %d\n", i);
      exit(1);
    }
  }

  print_stats("After Reading (Triggered Swap Ins + XOR Recovery)", pid);
}

int main(){
  printf("Starting Programming Assignment 4: Experimental Evaluation\n");
  printf("========================================================\n\n");

  // TEST 1: FCFS
  printf("\n>>> PHASE 1: First Come First Serve (FCFS) Disk Scheduling <<<\n");
  setdisksched(0); // 0 = FCFS
  int start_time_fcfs = uptime();
  
  int child1 = fork();
  if(child1 == 0){
     run_workload();
     exit(0);
  }
  wait(0);
  int end_time_fcfs = uptime();


  // TEST 2: SSTF
  printf("\n\n>>> PHASE 2: Shortest Seek Time First (SSTF) Disk Scheduling <<<\n");
  setdisksched(1); // 1 = SSTF
  int start_time_sstf = uptime();

  int child2 = fork();
  if(child2 == 0){
     run_workload();
     exit(0);
  }
  wait(0);
  int end_time_sstf = uptime();


  // RESULTS SUMMARY
  printf("\n\n=============== EXPERIMENTAL RESULTS ===============\n");
  printf("Total Execution Ticks (FCFS): %d\n", (end_time_fcfs - start_time_fcfs));
  printf("Total Execution Ticks (SSTF): %d\n", (end_time_sstf - start_time_sstf));
  
  if((end_time_sstf - start_time_sstf) < (end_time_fcfs - start_time_fcfs)){
     printf("CONCLUSION: SSTF outperformed FCFS as expected!\n");
  } else {
     printf("CONCLUSION: SSTF and FCFS performed similarly due to sequential workload block patterns.\n");
  }
  printf("====================================================\n");

  exit(0);
}
