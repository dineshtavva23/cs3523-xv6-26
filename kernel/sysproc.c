#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"


extern struct spinlock wait_lock;
extern struct proc proc[NPROC];

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
      return cnt;
    }
    
  }
  release(&wait_lock);

  return cnt;

}