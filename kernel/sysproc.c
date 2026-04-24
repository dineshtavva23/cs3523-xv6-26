#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "stat.h"

extern struct spinlock wait_lock;
extern struct proc proc[NPROC];
int disk_sched_policy = 0; // 0 for FCFS and 1 for SSTF

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
// always prints "Hello from the kernel!" and returns 0 always
uint64
sys_hello(void){
  printf("Hello from the kernel!\n");
  return 0;
}

// returns the pid of calling process
uint64
sys_getpid2(void){
  return myproc()->pid;
}

//return parent pid if it exists else -1 if no parent exists
uint64
sys_getppid(void){
  struct proc *p = myproc();

  acquire(&wait_lock);
  struct proc *par = p->parent;
  int ppid = -1;
  if(par!=0){
    ppid = par->pid;
  }

  release(&wait_lock);
  return ppid;
}

//returns the number of currently alive processes of calling process.
//Zombie processes must not be counted
uint64
sys_getnumchild(void){

  return myproc()->num_children;

}

//returns the number of system calls a process has invoked
uint64
sys_getsyscount(void){
  return myproc()->sys_call_count-1;
}

//returns the syscallcounter of a child process with a given pid, if not found returns -1
uint64
sys_getchildsyscount(void){
  int pid;
  argint(0,&pid);
  struct proc *p = myproc();
  int cnt = -1;
  // we use wait lock to protech parent pointer
  acquire(&wait_lock);
  for(struct proc *it=proc;it<&proc[NPROC];it++){
    // we use it lock to protect pid of the proces
    acquire(&it->lock);

    if(it->parent==p&&it->pid==pid){
      cnt = it->sys_call_count;
    }
    release(&it->lock);
    if(cnt!=-1){
      release(&wait_lock);
      return cnt;
    }
    
  }
  release(&wait_lock);

  return cnt;

}
//returns the current MLFQ levle of calling process
uint64
sys_getlevel(void){
  return myproc()->curr_level;
}

// Retrieves detailed scheduling statistics of the process with the given PID. 
uint64
sys_getmlfqinfo(void){
  int pid;
  uint64 info_addr;

  argint(0,&pid);

  argaddr(1,&info_addr);

  struct proc *p;
  struct proc *candidate  =0;

  // searching for the process 
  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(p->pid==pid&&p->state!=UNUSED){
      candidate = p;
      break;
    }
    release(&p->lock);
  }

  if(candidate == 0){
    return -1;
  }

  struct mlfqinfo info;

  // fill the struct (local)
  info.level = candidate->curr_level;
  info.times_scheduled = candidate->times_scheduled;
  info.total_syscalls = candidate->sys_call_count;

  for(int i=0;i<MLFQ_LEVELS;i++){
    info.ticks[i]=candidate->total_ticks[i];
  }

  release(&candidate->lock);
  // copying to user space
  if(copyout(myproc()->pagetable,info_addr,(char *)&info,sizeof(info))<0){
    return -1;
  }

  // release(&candidate->lock);
  return 0;

}

// retrieves vmstats of a process
uint64
sys_getvmstats(void){
  int pid;
  uint64 addr;

  argint(0,&pid);
  argaddr(1,&addr);

  struct proc *p;
  struct proc *candidate =0;

  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(pid==p->pid&&p->state!=UNUSED){
      candidate=p;
      break;
    }
    release(&p->lock);
  }
  if(candidate==0){
    return -1;
  }
  struct vmstats stats;
  memset(&stats, 0,sizeof(stats));

  stats.page_faults = candidate->page_faults;
  stats.pages_swapped_in = candidate->pages_swapped_in;
  stats.pages_swapped_out = candidate->pages_swapped_out;
  stats.resident_pages = candidate->resident_pages;
  stats.pages_evicted = candidate->pages_evicted;
  
  // PA 4 stats
  stats.disk_reads =candidate->disk_reads;
  stats.disk_writes =candidate->disk_writes;
  if(candidate->disk_reads+candidate->disk_writes>0){
    stats.avg_disk_latency =candidate->total_disk_latency/(candidate->disk_reads+candidate->disk_writes);
  }else{
    stats.avg_disk_latency =0;
  }

  release(&candidate->lock);
  if(copyout(myproc()->pagetable,addr,(char *)&stats,sizeof(stats))<0){
    return -1;
  }
  return 0;
}

//sets the disk_sched_policy => 0 for FCFS and 1 for SSTF
uint64
sys_setdisksched(void){
  int policy;
  argint(0,&policy);
  if(policy!=0&&policy!=1){
    //invalid input
    return -1;
  }
  disk_sched_policy= policy;
  return 0;
}

uint64
sys_getdiskstats(void){
  int pid;
  uint64 addr;

  argint(0,&pid);
  argaddr(1,&addr);

  struct proc *p;
  struct proc *candidate =0;

  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(pid==p->pid&&p->state!=UNUSED){
      candidate=p;
      break;
    }
    release(&p->lock);
  }
  if(candidate==0){
    return -1;
  }
  struct diskstats stats;
  memset(&stats, 0,sizeof(stats));
  
  // PA 4 stats
  stats.disk_reads =candidate->disk_reads;
  stats.disk_writes =candidate->disk_writes;
  if(candidate->disk_reads+candidate->disk_writes>0){
    stats.avg_disk_latency =candidate->total_disk_latency/(candidate->disk_reads+candidate->disk_writes);
  }else{
    stats.avg_disk_latency =0;
  }

  release(&candidate->lock);
  if(copyout(myproc()->pagetable,addr,(char *)&stats,sizeof(stats))<0){
    return -1;
  }
  return 0;
}