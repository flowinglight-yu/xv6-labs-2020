// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
// 物理内存分配器，为用户进程、内核栈、页表页和管道缓冲区分配内存。以4096字节的整页为单位分配。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel. defined by kernel.ld.
                   // 内核结束后的第一个地址，由kernel.ld链接脚本定义

struct run {
  struct run *next;  // 链表指针，指向下一个空闲页
};

// 内核内存管理结构
struct {
  struct spinlock lock;    // 保护空闲链表的自旋锁
  struct run *freelist;    // 空闲物理页链表头指针
} kmem[NCPU];

char *
kmem_lock_names[] = {
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};

// 初始化内存分配器
void
kinit()
{
  for(int i=0;i<NCPU;i++) { // 初始化所有锁
    initlock(&kmem[i].lock, kmem_lock_names[i]);// 初始化自旋锁
  }
  freerange(end, (void*)PHYSTOP); // 将[end, PHYSTOP]范围内的内存释放到空闲链表
}

// 释放指定范围内的物理内存到空闲链表
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);  // 将起始地址向上对齐到页边界
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);  // 逐页释放内存
}

// 释放物理内存页
// 释放由v指向的物理内存页，通常这个页应该是由kalloc()分配的
// （唯一的例外是在初始化分配器时，见上面的kinit函数）
void
kfree(void *pa)
{
  struct run *r;

  // 安全检查：地址必须页对齐，且在内核结束之后、物理内存顶部之前
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 用垃圾数据填充页面，用于捕获悬空引用（使用已释放内存的错误）
  memset(pa, 1, PGSIZE);

  // 将物理页转换为链表节点（利用空闲页本身的空间存储链表指针）
  r = (struct run*)pa;

  push_off(); // 关闭中断，防止死锁
  int cpu_id = cpuid(); // 获取当前CPU ID
  // 获取锁保护空闲链表
  acquire(&kmem[cpu_id].lock);
  // 将新释放的页插入链表头部
  r->next = kmem[cpu_id].freelist;  // 新页指向原来的链表头
  kmem[cpu_id].freelist = r;        // 链表头指向新页
  release(&kmem[cpu_id].lock);
  pop_off(); // 恢复中断
}

// 分配一个4096字节的物理内存页
// 返回内核可以使用的指针
// 如果无法分配内存则返回0
void *
kalloc(void)
{
  struct run *r;

  push_off(); // 关闭中断，防止死锁
  int cpu_id = cpuid(); // 获取当前CPU ID
  // 获取锁保护空闲链表
  acquire(&kmem[cpu_id].lock);
  // 如果当前CPU的空闲链表为空，尝试从其他CPU的空闲链表中窃取一个页
  if(!kmem[cpu_id].freelist) { 
    int steal_left = 64;// 最多尝试窃取64个页
    // 尝试从其他CPU的空闲链表中窃取一个页
    for(int i=0;i<NCPU;i++) {
      if(i == cpu_id)
        continue; // 跳过当前CPU
      acquire(&kmem[i].lock);
      struct run *rr = kmem[i].freelist;
      while(rr && steal_left) {
        kmem[i].freelist = rr->next;
        rr->next = kmem[cpu_id].freelist;
        kmem[cpu_id].freelist = rr;
        rr = kmem[i].freelist;
        steal_left--;
      }
      release(&kmem[i].lock);
      if(steal_left == 0) 
      break; // 已经窃取够了
    }
  }

  r = kmem[cpu_id].freelist;  // 获取链表头（第一个空闲页）
  if(r)
    kmem[cpu_id].freelist = r->next;  // 如果链表非空，将链表头指向下一个页
  release(&kmem[cpu_id].lock);

  pop_off(); // 恢复中断
  
  if(r)
    memset((char*)r, 5, PGSIZE); // 用垃圾数据填充新分配的页（用于调试）
  return (void*)r;  // 返回分配的物理页地址
}
