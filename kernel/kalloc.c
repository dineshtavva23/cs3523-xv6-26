// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define NFRAMES ((PHYSTOP-KERNBASE)/PGSIZE) //as memory spans from KERNBASE to PHYSTOP
#define MAX_SWAP_PAGES 1024

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct frame{
  int in_use; // 0 -> free, 1 -> used 
  struct proc *owner;// pointer to owner process
  uint64 pa;//physical address of the page
  uint64 va;//virtual address of the page

};

struct{
  struct frame frames[NFRAMES];//table
  struct spinlock lock;// for the race conditions
}frame_table;


struct swap_slot{
  int in_use;
  struct proc *owner;
  uint64 va;
  char page[PGSIZE];
};

struct{
  struct spinlock lock;
  struct swap_slot slots[MAX_SWAP_PAGES];

}swap_table;

int clock_hand=0; // to know where the clock hand points
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&frame_table.lock, "frame_table"); //initialsing the lock for the frame table
  for(int i=0;i<NFRAMES;i++){
    frame_table.frames[i].in_use=0;// initialising the in_use flags to 0
    frame_table.frames[i].pa=0;//initilaising physical, virtual address and pointer to null pointer
    frame_table.frames[i].va=0;
    frame_table.frames[i].owner = 0;
  }
  initlock(&swap_table.lock,"swap_table");
  for(int i=0;i<MAX_SWAP_PAGES;i++){
    swap_table.slots[i].in_use=0;
    swap_table.slots[i].owner=0;
    swap_table.slots[i].va=0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&frame_table.lock);
  for(int i=0;i<NFRAMES;i++){
    if(frame_table.frames[i].pa==(uint64)pa){
      if(frame_table.frames[i].owner!=0){
        frame_table.frames[i].owner->resident_pages--;
      }
      frame_table.frames[i].in_use=0;
      frame_table.frames[i].pa=0;
      frame_table.frames[i].va=0;
      frame_table.frames[i].owner = 0;
      break;
    }
  }
  release(&frame_table.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  // if memory is full, evict the page to free
  if(!r){
    r = evict_frame();
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


// assinging a frame to a process p
void assign_frame(void *pa,struct proc *p,uint64 va){
  acquire(&frame_table.lock);
  for(int i=0;i<NFRAMES;i++){
    if(frame_table.frames[i].in_use==0){
      frame_table.frames[i].in_use=1;
      frame_table.frames[i].pa =(uint64)pa;
      frame_table.frames[i].va=va;
      frame_table.frames[i].owner =p;
      break;
    }
  }
  release(&frame_table.lock);
}

// when a frame gets evicted we need to find a frees slot in the swap table
// to store the page
int swap_out(struct proc *owner, uint64 va, char *page_data){
  acquire(&swap_table.lock);
  for(int i=0;i<MAX_SWAP_PAGES;i++){
    if(swap_table.slots[i].in_use==0){
      swap_table.slots[i].in_use=1;
      swap_table.slots[i].owner = owner;
      swap_table.slots[i].va = va;

      // copy the page data
      memmove(swap_table.slots[i].page,page_data,PGSIZE);
      release(&swap_table.lock);
      return i;// swap index
    }
  }

  release(&swap_table.lock);
  panic("Swap space full"); // if no space is left

  return -1;
}
//when a page is swapped in we shold find free fame for the page
// if not found we evict the page
int swap_in(struct proc *owner,uint64 va,char *target_page_data){
  acquire(&swap_table.lock);
  for(int i=0;i<MAX_SWAP_PAGES;i++){
    if(swap_table.slots[i].in_use==1&&swap_table.slots[i].owner ==owner&&swap_table.slots[i].va==va){
      // copy the data
      memmove(target_page_data,swap_table.slots[i].page,PGSIZE);
      //free up the swap_slot
      swap_table.slots[i].in_use=0;
      swap_table.slots[i].owner=0;
      swap_table.slots[i].va=0;

      release(&swap_table.lock);
      return 1;//found
    }
  }
  release(&swap_table.lock);
  return 0;// not found
}

// when a process exits free up the swap slots
void free_process_swap(struct proc *owner){
  acquire(&swap_table.lock);
  for(int i=0;i<MAX_SWAP_PAGES;i++){
    if(swap_table.slots[i].in_use==1&&swap_table.slots[i].owner==owner){
      swap_table.slots[i].in_use=0;
      swap_table.slots[i].owner =0;
      swap_table.slots[i].va=0;
    }


  }
  release(&swap_table.lock);
}

void* evict_frame(){
  acquire(&frame_table.lock);

  int victim_idx = -1;
  int least_priority = -1;

  int num_scanned = 0;

  // find the victim
  while(1){  
    int j = clock_hand;
    clock_hand = (clock_hand+1)%NFRAMES;

    num_scanned++;

    // consider the frames in user process
    if(frame_table.frames[j].in_use==1&&frame_table.frames[j].owner!=0){
      pte_t *pte = get_pte(frame_table.frames[j].owner->pagetable,frame_table.frames[j].va);

      if(pte&&(*pte & PTE_V)){
        if((*pte&PTE_A)){
          // accesses recently, clear the bit 
          *pte = (*pte&(~PTE_A));
            
        }else{
          // not accesses recently, candidate for eviction
          // check priority
          int proc_level = frame_table.frames[j].owner->curr_level;

          if(victim_idx==-1||proc_level>least_priority){
            victim_idx = j;
            least_priority = proc_level;

            if(proc_level==3){// MLFQ_LEVELs-1
              break;
            }
          }
        }
      }
    }
    
    //if we have num_scanned all frames at 
    // least once above and found victim, stop
    if (num_scanned>=NFRAMES && victim_idx!=-1) {
      break;
    }


  }

  // update the clock to point to right after the victim to satify clock algorithm
  clock_hand = (victim_idx + 1) % NFRAMES;

  // we found the victim
  struct frame *f = &frame_table.frames[victim_idx];

  struct proc *owner = f->owner;
  uint64 pa =f->pa;

  uint64 va = f->va;

  // mar the pte as swapped
  
  pte_t *pte = get_pte(owner->pagetable,va);
  
  *pte = ((*pte)&(~PTE_V)); // unset the valid bit
  *pte = ((*pte)|(PTE_S));//set the swapped bit

  // clear from cpu TLB cache
  sfence_vma();
  // dump the page to swap table
  swap_out(owner,va,(char *)pa);

  // update stats
  owner->pages_evicted++;
  owner->resident_pages--;
  owner->pages_swapped_out++;

  //free the frame table
  f->in_use=0;
  f->owner=0;
  f->va=0;

  f->pa=0;

  //fill memory with junk
  memset((void*)pa,1,PGSIZE);//debugging useful
  release(&frame_table.lock);

  return (void*)pa;
}

int swap_read(struct proc *owner, uint64 va, char *target_page_data){
  acquire(&swap_table.lock);
  for(int i=0;i<MAX_SWAP_PAGES;i++){


    
    if(swap_table.slots[i].in_use==1&&swap_table.slots[i].owner==owner&&swap_table.slots[i].va==va){

      memmove(target_page_data,swap_table.slots[i].page,PGSIZE);
      release(&swap_table.lock);
    
      return 1;//found
    }

  }
  release(&swap_table.lock);
  return 0;//not found
}