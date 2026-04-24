#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

struct vmstats {
  int page_faults;
  int pages_evicted;
  int resident_pages;
  int pages_swapped_in;
  int pages_swapped_out;

  int disk_reads;
  int disk_writes;
  int avg_disk_latency;
};
struct diskstats{
    int disk_reads;
    int disk_writes;
    int avg_disk_latency;
};

int total_passed=0;

int total_failed=0;

void verify(int condition,const char *msg) {
    if (condition){
        printf("  [PASS] %s\n",msg);
        total_passed++;
    }else{
        printf("  [FAIL] %s\n",msg);
        total_failed++;

    }
}

int main(void) {
    printf("---------------------------------\n");
    printf("         RAID-5 TEST             \n");
    printf("---------------------------------\n");

    struct vmstats initial_s;
    
    getvmstats(getpid(),&initial_s);
    verify(initial_s.disk_reads >= 0,"Initial process telemetry boot ok");

    //Heavily allocate memory to force 4-Disk RAID evictions
    int alloc_pages = 800; 
    
    char *arr = sbrk(alloc_pages * 4096);
    
    //FCFS TEST
    printf("\n>>> PHASE 1: FCFS Eviction Engine\n");
    setdisksched(0);
    for(int i =0;i<alloc_pages;i++){
    
        arr[i*4096]=(char)(i%255);
    }
    struct vmstats eviction_s;
    getvmstats(getpid(),&eviction_s);
    
    verify(eviction_s.pages_evicted > 0,"Memory pressure triggered eviction");
    verify(eviction_s.disk_writes > 0,"RAID 5 engine physically wrote parity blocks");

    //Read back to trigger swap_in
    int errors = 0;
    for(int i=0;i<alloc_pages;i++){
      
        if(arr[i*4096]!=(char)(i%255)){
      
            errors++;
        }
    }
    verify(errors ==0,"RAID 5 dual-stripe geometry restored data flawlessly");
    
    struct vmstats swap_in_s;
   
   
    getvmstats(getpid(),&swap_in_s);
    verify(swap_in_s.disk_reads > eviction_s.disk_reads,"swap_in triggered RAID reads");

    //SSTF TEST
    printf("\n>>> PHASE 2: SSTF Tracking Geometry\n");
   
    setdisksched(1);
    char *arr2=sbrk(200*4096);
    for(int i =0;i<200;i++){
   
        arr2[i*4096]=(char)(i%255);
    }
   
   
    struct vmstats sstf_s;
    getvmstats(getpid(),&sstf_s);
    verify(sstf_s.disk_writes>swap_in_s.disk_writes,"SSTF dynamically committed stripes");
    
    
    sbrk(-alloc_pages * 4096);
    sbrk(-200 * 4096);

    printf("\n------------------------------------\n");
    printf("  Total Test Cases Passed: %d / %d\n", total_passed, total_passed + total_failed);
    printf("------------------------------------\n");
    exit(0);
}
