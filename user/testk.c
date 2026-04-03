#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_CHILDREN 5
#define CHILD_PAGES 5
#define MAX_FRAMES 64

struct vmstats {
  int page_faults;
  int pages_evicted;
  int resident_pages;
  int pages_swapped_in;
  int pages_swapped_out;
};
// Helper to print stats
void print_vm_stats(char *msg) {
    struct vmstats stats;
    if (getvmstats(getpid(), &stats) < 0) return;
    printf("\n[%s] (PID %d)\n", msg, getpid());
    printf("Faults: %d | Evicted: %d | Swap In: %d | Swap Out: %d | Resident: %d\n", 
           stats.page_faults, stats.pages_evicted, 
           stats.pages_swapped_in, stats.pages_swapped_out, stats.resident_pages);
}

int main() {
    int test_passed = 1;  // Track overall pass/fail
    int child_status;
    
    printf("=== Starting Ultimate VM Stress Test ===\n");
    printf("MAX_FRAMES = %d\n", MAX_FRAMES);

    // ==========================================
    // PHASE 1: The 'uvmcopy' Swap Test
    // ==========================================
    printf("\n--- Phase 1: Parent Swapping & Forking ---\n");
    printf("plevel-%d",getlevel());
    // Allocate 40 pages (forces some memory pressure early)
    char *parent_mem = sbrklazy(40 * PGSIZE);
    if(parent_mem == (char*)-1) {
        printf("FAIL: Parent sbrk failed!\n");
        test_passed = 0;
        exit(1);
    }

    // Write identifiable data
    for(int i = 0; i < 40; i++) {
        parent_mem[i * PGSIZE] = 'P'; 
    }

    // for(int i=0;i<300000000;i++);
    
    // Fork while the parent is huge (tests uvmcopy handling PTE_S)
    int pid = fork();
    if(pid < 0) {
        printf("FAIL: Phase 1 fork failed!\n");
        test_passed = 0;
        exit(1);
    }

    if(pid == 0) {
        // CHILD 1 checks if it inherited the swapped data perfectly
        for(int i = 0; i < 40; i++) {
            if(parent_mem[i * PGSIZE] != 'P') {
                printf("FAIL: Child 1 inherited corrupted data at page %d!\n", i);
                exit(1);  // Exit with error
            }
        }
        printf("Phase 1: PASS (uvmcopy swap test)\n");
        exit(0);  // Exit success
    }
    wait(&child_status);
    if(child_status != 0) {
        printf("Phase 1: FAIL\n");
        test_passed = 0;
    }

    // ==========================================
    // PHASE 2: Multi-Process Thrashing
    // ==========================================
    printf("\n--- Phase 2: Mass Forking & Memory Thrashing ---\n");
    int phase2_failures = 0;
    
    for(int i = 0; i < NUM_CHILDREN; i++) {
        int cpid = fork();
        if(cpid == 0) {
            // Each child allocates 10 pages (5 * 5 = 25 pages total demand)
            char *child_mem = sbrklazy(CHILD_PAGES * PGSIZE);
            if(child_mem == (char*)-1) {
                printf("FAIL: Child %d sbrk failed\n", i);
                exit(1);
            }

            // Write child-specific data
            for(int j = 0; j < CHILD_PAGES; j++) {
                child_mem[j * PGSIZE] = (char)i;
            }

            // Sleep forces MLFQ up/down and guarantees eviction
            pause(10); 

            // Wake up and verify data survived the eviction storm
            for(int j = 0; j < CHILD_PAGES; j++) {
                if(child_mem[j * PGSIZE] != (char)i) {
                    printf("FAIL: Child %d data corrupted at page %d!\n", i, j);
                    exit(1);
                }
            }
            exit(0);  // Success
        }
    }

    // Parent sleeps while the 15 children battle for the 64 physical frames
    // for(int i=0;i<200000000;i++);
    pause(50);

    for(int i = 0; i < NUM_CHILDREN; i++) {
        wait(&child_status);
        if(child_status != 0) {
            phase2_failures++;
        }
    }
    
    if(phase2_failures == 0) {
        printf("Phase 2: PASS (All %d children completed successfully)\n", NUM_CHILDREN);
    } else {
        printf("Phase 2: FAIL (%d children failed)\n", phase2_failures);
        test_passed = 0;
    }

    // ==========================================
    // PHASE 3: Parent Wakeup & Verification
    // ==========================================
    printf("\n--- Phase 3: Parent Wakeup & Data Integrity ---\n");
    printf("plevel-%d",getlevel());
    print_vm_stats("Parent After Sleeping");

    // Parent verifies its original 40 pages are still 'P'
    int phase3_ok = 1;
    for(int i = 0; i < 40; i++) {
        if(parent_mem[i * PGSIZE] != 'P') {
            printf("FAIL: Parent data corrupted after wakeup at page %d!\n", i);
            phase3_ok = 0;
            test_passed = 0;
            break;
        }
    }
    if(phase3_ok) {
        printf("Phase 3: PASS (Parent data intact after eviction/swap-in)\n");
    } else {
        printf("Phase 3: FAIL\n");
    }

    // ==========================================
    // PHASE 4: Heap Shrink / Ghost Page Cleanup
    // ==========================================
    printf("\n--- Phase 4: uvmunmap and Ghost Page Cleanup ---\n");
    // Shrink heap by 20 pages (tests uvmunmap and kfree_user)
    sbrklazy(-20 * PGSIZE);
    
    // Allocate 10 pages (should succeed instantly if frame table is correct)
    char *new_mem = sbrklazy(10 * PGSIZE);
    if(new_mem == (char*)-1) {
        printf("Phase 4: FAIL (Final sbrk failed - frame table leak)\n");
        test_passed = 0;
    } else {
        printf("Phase 4: PASS (Memory reclaimed and reallocated)\n");
    }

    // ==========================================
    // PHASE 5: Verify resident_pages <= MAX_FRAMES
    // ==========================================
    printf("\n--- Phase 5: Resident Pages Constraint Check ---\n");
    struct vmstats final_stats;
    if(getvmstats(getpid(), &final_stats) == 0) {
        printf("Final Resident Pages: %d (Max: %d)\n", final_stats.resident_pages, MAX_FRAMES);
        if(final_stats.resident_pages <= MAX_FRAMES) {
            printf("Phase 5: PASS (Resident pages within limit)\n");
        } else {
            printf("Phase 5: FAIL (Resident pages %d > MAX_FRAMES %d)\n", 
                   final_stats.resident_pages, MAX_FRAMES);
            test_passed = 0;
        }
    }
    
    // ==========================================
    // FINAL VERDICT
    // ==========================================
    print_vm_stats("Final Stats");
    printf("\n========================================\n");
    if(test_passed) {
        printf("=== TEST RESULT: PASS ===\n");
    } else {
        printf("=== TEST RESULT: FAIL ===\n");
    }
    printf("========================================\n");

    exit(test_passed ? 0 : 1);
}