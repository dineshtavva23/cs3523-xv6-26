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
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define NFRAMES ((PHYSTOP-KERNBASE)/PGSIZE) //as memory spans from KERNBASE to PHYSTOP
#define MAX_SWAP_PAGES 1024

#define SWAP_START_BLOCK 10000
#define VDISK_SIZE 2000 //capacity of each virtual disk
void freerange(void *pa_start, void *pa_end);

//for testing purposes
int simulated_failed_disk=-1;

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
  // char page[PGSIZE]; PA 4 modification 
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



int swap_out(struct proc *owner,uint64 va,char *page_data){
  acquire(&swap_table.lock);
  int slot=-1;
  for(int i=0;i<MAX_SWAP_PAGES;i++){
    if(swap_table.slots[i].in_use==0){
      swap_table.slots[i].in_use=1;
      swap_table.slots[i].owner=owner;
      swap_table.slots[i].va=va;
      slot=i;
      break;
    }
  }

  release(&swap_table.lock);
  if(slot==-1){
    panic("Swap space full");
  }

  //RAID 0
  // int start_logical_block=slot*4;
  // for(int i=0;i<4;i++){
  //   int b = start_logical_block+i;
  //   int disk_id=b%4;
  //   int disk_block =b/4;
  //   int physical_block=SWAP_START_BLOCK+(disk_id*VDISK_SIZE)+disk_block;

  //   struct buf *buf=bread(1,physical_block);//dev=1;
  //   memmove(buf->data,page_data+(i*BSIZE),BSIZE);
  //   bwrite(buf);
  //   brelse(buf);
  // }

  //RAID-5
  int stripe0=slot*2;
  int stripe1=2*slot+1;
  int pdisk0 =stripe0%4;
  int pdisk1=stripe1%4;
  char pblock0[BSIZE];
  memset(pblock0,0,BSIZE);
  char pblock1[BSIZE];
  memset(pblock1,0,BSIZE);

  //stripe0
  for(int i=0; i<3;i++){
    int disk_id = i;
    if(disk_id >= pdisk0){
      disk_id++;
    }
    int phys_blk=SWAP_START_BLOCK+(disk_id*VDISK_SIZE)+stripe0;

    struct buf *buf=bread(1,phys_blk);
    memmove(buf->data,page_data+(i*BSIZE),BSIZE);
    bwrite(buf);
    brelse(buf);

    for(int j=0;j<BSIZE;j++){
      pblock0[j]^=*(page_data+(i*BSIZE)+j);
    }
  }
  
  //write parity 0
  int pphys0=SWAP_START_BLOCK+(pdisk0*VDISK_SIZE)+stripe0;
  struct buf *p0=bread(1,pphys0);
  memmove(p0->data,pblock0,BSIZE);
  bwrite(p0);
  brelse(p0);


  //stripe 1
  int disk_id1=0;
  if(disk_id1>=pdisk1){
    disk_id1++;
  }
  int phys_blk1=SWAP_START_BLOCK+(disk_id1*VDISK_SIZE)+stripe1;
  
  struct buf *buf1=bread(1,phys_blk1);
  memmove(buf1->data,page_data+(3*BSIZE),BSIZE);
  bwrite(buf1);
  brelse(buf1);

  memmove(pblock1,page_data+(3*BSIZE),BSIZE);


  int pphys1=SWAP_START_BLOCK+(pdisk1*VDISK_SIZE)+stripe1;
  
  struct buf *p1=bread(1,pphys1);
  memmove(p1->data,pblock1,BSIZE);
  bwrite(p1);
  brelse(p1);
  return slot;
}
// OLD VERSION PA-3
// when a frame gets evicted we need to find a frees slot in the swap table
// to store the page
// int swap_out(struct proc *owner, uint64 va, char *page_data){
//   acquire(&swap_table.lock);
//   for(int i=0;i<MAX_SWAP_PAGES;i++){
//     if(swap_table.slots[i].in_use==0){
//       swap_table.slots[i].in_use=1;
//       swap_table.slots[i].owner = owner;
//       swap_table.slots[i].va = va;

//       // copy the page data
//       memmove(swap_table.slots[i].page,page_data,PGSIZE);
//       release(&swap_table.lock);
//       return i;// swap index
//     }
//   }

//   release(&swap_table.lock);
//   panic("Swap space full"); // if no space is left

//   return -1;
// }

int swap_in(struct proc *owner,uint64 va,char *target_page_data){
  acquire(&swap_table.lock);
  int slot =-1;
  for(int i=0;i<MAX_SWAP_PAGES;i++){
    if(swap_table.slots[i].in_use==1&&swap_table.slots[i].owner ==owner&&swap_table.slots[i].va==va){
      slot =i;
      //free up the swap table
      swap_table.slots[i].in_use=0;
      swap_table.slots[i].owner=0;
      swap_table.slots[i].va=0;
      break;
    }

  }
  release(&swap_table.lock);
  if(slot==-1){
    return 0;//not found
  }

  //RAID 0
  // int start_logical_block=slot*4;
  // for(int i=0;i<4;i++){
  //   int b =start_logical_block+i;
  //   int disk_id=b%4;
  //   int disk_block=b/4;
  //   int physical_block=SWAP_START_BLOCK+(disk_id*VDISK_SIZE)+disk_block;

  //   struct buf *buf =bread(1,physical_block);
  //   memmove(target_page_data+(i*BSIZE),buf->data,BSIZE);
  //   brelse(buf);
  // }

  //RAID 5
  int stripe0=2*slot;

  int stripe1=2*slot+1;
  int pdisk0 =stripe0%4;
  int pdisk1 =stripe1%4;

  int missing_data_idx0=-1;
  
  //stripe 0
  for(int i=0;i<3;i++){
    int disk_id=i;
    if(disk_id>=pdisk0){
      disk_id++;
    }

    if(disk_id==simulated_failed_disk){
      missing_data_idx0=i;
      continue;

    }

    int phys_blk=SWAP_START_BLOCK+(disk_id*VDISK_SIZE)+stripe0;
    struct buf *buf = bread(1,phys_blk);
    memmove(target_page_data+(i*BSIZE),buf->data,BSIZE);
    brelse(buf);
  }

  // reconstruct stripe 0
  if(missing_data_idx0!=-1){
    int pphys0 =SWAP_START_BLOCK+(pdisk0*VDISK_SIZE)+stripe0;
    struct buf *p0 =bread(1,pphys0);
    
    
    char pcontent[BSIZE];
    memmove(pcontent,p0->data,BSIZE);
    brelse(p0);

    for(int i=0;i<BSIZE;i++){
      for(int j =0;j<3;j++){
    
        if(j!=missing_data_idx0) {
          pcontent[i]^=*(target_page_data+(j*BSIZE)+i);
        }
    
      }
      *(target_page_data+(missing_data_idx0 *BSIZE)+i)=pcontent[i];
    
    }
  }

  //stripe 1
  int missing_data_idx1=-1;
  int disk_id1= 0>=pdisk1 ? 1:0;
  
  if(disk_id1==simulated_failed_disk){
  
    missing_data_idx1 =0;
  }else{
  
    int phys_blk1 =SWAP_START_BLOCK +(disk_id1*VDISK_SIZE)+stripe1;
    struct buf *buf1 = bread(1, phys_blk1);
    
    memmove(target_page_data+(3*BSIZE),buf1->data,BSIZE);
    brelse(buf1);
  }

  if(missing_data_idx1!=-1){
    int pphys1 =SWAP_START_BLOCK+(pdisk1*VDISK_SIZE)+stripe1;
  
    struct buf *p1 =bread(1,pphys1);
    memmove(target_page_data+(3*BSIZE),p1->data,BSIZE);
    brelse(p1);
  }
  return 1;
}

// OLD VERSION PA3
//when a page is swapped in we shold find free fame for the page
// if not found we evict the page
// int swap_in(struct proc *owner,uint64 va,char *target_page_data){
//   acquire(&swap_table.lock);
//   for(int i=0;i<MAX_SWAP_PAGES;i++){
//     if(swap_table.slots[i].in_use==1&&swap_table.slots[i].owner ==owner&&swap_table.slots[i].va==va){
//       // copy the data
//       memmove(target_page_data,swap_table.slots[i].page,PGSIZE);
//       //free up the swap_slot
//       swap_table.slots[i].in_use=0;
//       swap_table.slots[i].owner=0;
//       swap_table.slots[i].va=0;

//       release(&swap_table.lock);
//       return 1;//found
//     }
//   }
//   release(&swap_table.lock);
//   return 0;// not found
// }

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
  release(&frame_table.lock);

  // dump the page to swap table
  swap_out(owner,va,(char *)pa);

  acquire(&frame_table.lock);

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


int swap_read(struct proc *owner,uint64 va,char *target_page_data){
  acquire(&swap_table.lock);
  int slot =-1;
  for(int i=0;i<MAX_SWAP_PAGES;i++){
    if(swap_table.slots[i].in_use==1&&swap_table.slots[i].owner==owner&&swap_table.slots[i].va==va){
      slot = i;
      break;
    }
  }
  release(&swap_table.lock);

  if(slot==-1){
    return 0;//not found
  }

  // int start_logical_block=slot*4;
  // for(int i=0;i<4;i++){
  //   int b=start_logical_block+i;
  //   int disk_id=b%4;
  //   int disk_block=b/4;
  //   int physical_block=SWAP_START_BLOCK+(disk_id*VDISK_SIZE)+disk_block;

  //   struct buf *buf =bread(1,physical_block);
  //   memmove(target_page_data+(i*BSIZE),buf->data,BSIZE);
  //   brelse(buf);
  // }

  //RAID5
  int stripe0=2*slot;
  int stripe1=2*slot+1;

  int pdisk0=stripe0%4;
  int pdisk1 =stripe1%4;

  int missing_data_idx0=-1;
  
  //stripe 0
  for(int i =0;i<3;i++){
  
    int disk_id =i;
    if(disk_id>=pdisk0){
  
      disk_id++;
    }

    if(disk_id==simulated_failed_disk){
      missing_data_idx0=i;
  
      continue;
    }
    int phys_blk =SWAP_START_BLOCK+(disk_id*VDISK_SIZE)+stripe0;
  
  
    struct buf *buf=bread(1,phys_blk);
    memmove(target_page_data+(i*BSIZE),buf->data,BSIZE);
  
  
    brelse(buf);
  }



  
  if(missing_data_idx0 != -1) {
    int pphys0=SWAP_START_BLOCK+(pdisk0*VDISK_SIZE)+stripe0;
    struct buf *p0=bread(1,pphys0);
  
  
    char pcontent[BSIZE];
    memmove(pcontent,p0->data,BSIZE);
  
  
    brelse(p0);

    for(int i =0;i<BSIZE;i++){
      for(int j =0;j<3;j++){
  
        if(j!=missing_data_idx0){
          pcontent[i]^=*(target_page_data+(j*BSIZE)+i);
        }
      }
  
  
      *(target_page_data+(missing_data_idx0*BSIZE) +i) =pcontent[i];
    }
  }

  //stripe1
  int missing_data_idx1=-1;
  int disk_id1= 0 >=pdisk1 ?1:0;
  
  if(disk_id1==simulated_failed_disk){
    missing_data_idx1 =0;
  }else{
    int phys_blk1 =SWAP_START_BLOCK+(disk_id1*VDISK_SIZE) +stripe1;
    struct buf *buf1=bread(1,phys_blk1);
    memmove(target_page_data+(3*BSIZE),buf1->data,BSIZE);
  
  
    brelse(buf1);
  }

  if(missing_data_idx1!=-1){
    int pphys1 =SWAP_START_BLOCK+(pdisk1*VDISK_SIZE)+stripe1;
  
  
    struct buf *p1=bread(1,pphys1);
    memmove(target_page_data+(3*BSIZE),p1->data,BSIZE);
    brelse(p1);
  }
  
  return 1;
}
// OLD VERSION PA3
// int swap_read(struct proc *owner, uint64 va, char *target_page_data){
//   acquire(&swap_table.lock);
//   for(int i=0;i<MAX_SWAP_PAGES;i++){


    
//     if(swap_table.slots[i].in_use==1&&swap_table.slots[i].owner==owner&&swap_table.slots[i].va==va){

//       memmove(target_page_data,swap_table.slots[i].page,PGSIZE);
//       release(&swap_table.lock);
    
//       return 1;//found
//     }

//   }
//   release(&swap_table.lock);
//   return 0;//not found
// }