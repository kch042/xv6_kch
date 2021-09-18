#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
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
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  backtrace();

  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
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

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
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

uint64
sys_sigalarm() {
    struct proc *p = myproc();

    int interval;
    uint64 handler;
    if (argint(0, &interval) < 0)
        return -1;
    if (argaddr(1, &handler) < 0)
        return -1;
    p->siginterval = interval;
    p->handler = (void (*)()) handler;  // cast as function pointer
        
    return 0;
}

uint64
sys_sigreturn() {

    // restore registers
    struct proc *p = myproc();
    
    p->trapframe->epc = p->_epc;
    p->trapframe->ra = p->_ra;
    p->trapframe->sp = p->_sp;
    p->trapframe->gp = p->_gp;
    p->trapframe->tp = p->_tp;
    p->trapframe->a0 = p->_a0;
    p->trapframe->a1 = p->_a1;
    p->trapframe->a2 = p->_a2;
    p->trapframe->a3 = p->_a3;
    p->trapframe->a4 = p->_a4;
    p->trapframe->a5 = p->_a5;
    p->trapframe->a6 = p->_a6;
    p->trapframe->a7 = p->_a7;
    p->trapframe->s0 = p->_s0;
    p->trapframe->s1 = p->_s1;
    p->trapframe->s2 = p->_s2;
    p->trapframe->s3 = p->_s3;
    p->trapframe->s4 = p->_s4;
    p->trapframe->s5 = p->_s5;
    p->trapframe->s6 = p->_s6;
    p->trapframe->s7 = p->_s7;
    p->trapframe->s8 = p->_s8;
    p->trapframe->s9 = p->_s9;
    p->trapframe->s10 = p->_s10;
    p->trapframe->s11 = p->_s11;
    p->trapframe->t0 = p->_t0;
    p->trapframe->t1 = p->_t1;    
    p->trapframe->t2 = p->_t2;       
    p->trapframe->t3 = p->_t3;
    p->trapframe->t4 = p->_t4;       
    p->trapframe->t5 = p->_t5;       
    p->trapframe->t6 = p->_t6;       
    
    p->in_handler = 0;
    return 0;
}


