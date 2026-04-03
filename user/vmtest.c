#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

struct vmstats {
  int page_faults;
  int pages_evicted;
  int resident_pages;
  int pages_swapped_in;
  int pages_swapped_out;
};

void print_stats(char *tag, int pid) {
  // [Keep your existing print_stats function exact same]
  struct vmstats st;
  if(getvmstats(pid, &st) == 0) {
    printf("\n[%s] VM Stats (PID %d):\n", tag, pid);
    printf("  Page Faults: %d\n", st.page_faults);
    printf("  Evicted:     %d\n", st.pages_evicted);
    printf("  Resident:    %d\n", st.resident_pages);
    printf("  Swapped Out: %d\n", st.pages_swapped_out);
    printf("  Swapped In:  %d\n", st.pages_swapped_in);
    printf("------------------------------------\n");
  } else {
    printf("Could not get stats for PID %d\n", pid);
  }
}

int main() {
  int pid = getpid();
  printf("=== Starting VM Experimental Evaluation ===\n");
  
  int num_pages = 500;
  uint64 size = num_pages * 4096;
  
  printf("Target: Allocating %d bytes (%d pages)...\n", (int)size, num_pages);
  char *mem = sbrk(size);

  print_stats("After sbrk (No Faults yet)", pid);
  
  // 2. Triggering Page faults
  printf("\nWriting data sequentially... (Using syscalls to maintain high MLFQ Priority!)\n");
  for(int i = 0; i < num_pages; i++) {
    mem[i * 4096] = 'X'; 
    
    // ARTIFICIAL SYSCALL SPAM! (This forces delta_s > delta_t in your trap.c MLFQ)
    for(int k=0; k<40; k++){ getpid(); }

    if (i > 0 && i % 500 == 0) { printf("  ...Wrote %d pages\n", i); }
  }
  
  print_stats("After Writing", pid);
  
  // 4. Integrating with MLFQ (Priority-based eviction)
  printf("\nForking Background process to demonstrate priority eviction...\n");
  int child_pid = fork();
  
  if (child_pid == 0) {
     int cpid = getpid();
     // Child process: Burn CPU with NO SYSCALLS to drop to lowest MLFQ priority!
     for(volatile int j = 0; j < 5000000; j++) {} 
     
     printf("Child dropped in priority. Allocating massive memory block...\n");
     char *child_mem = sbrk(400 * 4096);
     
     // Write to it to force out-of-memory.
     for(int i = 0; i < 400; i++) {
       child_mem[i * 4096] = 'Z';
     }
     print_stats("Child Final Stats (Should have massive Evictions)", cpid);
     exit(0);
  } else {
     wait(0);
     print_stats("Parent Stats After Child Dies (Protected by high priority!)", pid);
     
     // 5. NEW PHASE: Re-access memory to trigger Swap In!
     printf("\nParent waking up! Reading memory back to trigger Swap In...\n");
     for(int i = 0; i < num_pages; i++) {
        char expected = mem[i * 4096];
        if (expected != 'X') {
           printf("ERROR: Memory corrupted!\n");
           exit(1);
        }
        for(int k=0; k<40; k++){ getpid(); } // stay high priority
     }
     
     print_stats("Final Parent Stats (Should now show Swap Ins)", pid);
     printf("=== Evaluation Complete! ===\n");
  }
  
  exit(0);
}
