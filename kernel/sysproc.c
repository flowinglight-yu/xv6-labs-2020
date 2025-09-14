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

  if(argint(0, &n) < 0)//从用户空间获取参数 n，这个参数表示要增加（正数）或减少（负数）的字节数。
    return -1;
  addr = myproc()->sz;
  //if(growproc(n) < 0)
  //  return -1;
  //实现惰性分配
  struct proc *p = myproc();

  if(n > 0)
    p->sz += n;//n>0代表内存增加，先更新进程大小,等到访问时再分配物理内存(惰性分配)
  else if(p->sz + n > 0)//n<0代表内存减小，但是请注意如果内存减小了,要检查减去内存后是否大于0
    p->sz = uvmdealloc(p->pagetable, p->sz, p->sz + n);//减少内存时直接释放物理内存
  else
    return -1;

  return addr;
}

uint64
sys_sleep(void)
{
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
