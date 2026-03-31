#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Define the struct locally for the test program
struct vmstats {
  int page_faults;
  int pages_evicted;
  int resident_pages;
  int pages_swapped_in;
  int pages_swapped_out;
};

void print_stats(char *tag, int pid) {
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
  
  // 1. Allocate 1500 pages (Approx 6MB of memory). 
  // With an 8MB PHYSTOP, this will absolutely exhaust physical memory and trigger Swapping!
  int num_pages = 1500;
  uint64 size = num_pages * 4096;
  
  printf("Target: Allocating %d bytes (%d pages)...\n", (int)size, num_pages);
  char *mem = sbrk(size);
  if((long)mem == -1) {
    printf("sbrk failed!\n");
    exit(1);
  }

  print_stats("After sbrk (No Faults yet)", pid);
  
  // 2. Triggering Page faults & Forcing Replaements
  printf("\nWriting data sequentially to trigger lazy allocation & swapping...\n");
  for(int i = 0; i < num_pages; i++) {
    mem[i * 4096] = 'X'; // Writing to the page causes a page fault
    if (i > 0 && i % 500 == 0) {
      printf("  ...Wrote %d pages\n", i);
    }
  }
  
  print_stats("After Writing (Memory Exhausted, Evictions Occurred)", pid);
  
  // 3. Reusing previously evicted pages (Swap-in)
  printf("\nReading data back to force pages to be restored from Swap Space...\n");
  for(int i = 0; i < num_pages; i++) {
    char expected = mem[i * 4096];
    if (expected != 'X') {
      printf("ERROR: Memory corrupted at page %d! Expected 'X', got '%c'\n", i, expected);
      exit(1);
    }
  }
  
  print_stats("After Reading (Pages Restored, Swap-ins Occurred)", pid);

  // 4. Integrating with MLFQ (Priority-based eviction)
  printf("\nForking Background process to demonstrate priority eviction...\n");
  int child_pid = fork();
  
  if (child_pid == 0) {
     int cpid = getpid();
     // Child process: Burn CPU to drop to lowest MLFQ priority!
     for(volatile int j = 0; j < 50000000; j++) {} 
     
     printf("Child dropped in priority. Allocating massive memory block...\n");
     char *child_mem = sbrk(1000 * 4096);
     
     // Write to it to force it into RAM. 
     // Because the child is low priority, Clock algorithm will victimize 
     // the child's own pages over the sleeping parent's pages!
     for(int i = 0; i < 1000; i++) {
       child_mem[i * 4096] = 'Z';
     }
     
     print_stats("Child Final Stats (Should have massive Evictions)", cpid);
     exit(0);
  } else {
     // Parent just sleeps and waits for child
     wait(0);
     print_stats("Parent Final Stats (Protected by high priority)", pid);
     printf("=== Evaluation Complete! ===\n");
  }
  
  exit(0);
}
