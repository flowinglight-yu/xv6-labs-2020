#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* 线程可能的状态 */
#define FREE        0x0    // 空闲状态
#define RUNNING     0x1    // 正在运行状态
#define RUNNABLE    0x2    // 可运行状态

#define STACK_SIZE  8192   // 每个线程的栈大小
#define MAX_THREAD  4      // 最大线程数

// 线程上下文结构体，用于保存线程切换时的寄存器状态
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};


// 线程结构体定义
struct thread {
  char       stack[STACK_SIZE]; // 线程的栈空间
  int        state;             // 线程状态：FREE, RUNNING, RUNNABLE
  struct     context ctx; // 在 thread 中添加 context 结构体
};

struct thread all_thread[MAX_THREAD]; // 所有线程的数组
struct thread *current_thread;        // 当前运行线程的指针

// 声明线程切换函数（实现在汇编文件中）
extern void thread_switch(struct context* old, struct context* new); // 修改 thread_switch 函数声明

// 线程初始化函数
void thread_init(void)
{
  // main() 函数是线程0，它将第一次调用thread_schedule()
  // 它需要一个栈，以便第一次thread_switch()可以保存线程0的状态
  // thread_schedule() 不会再运行主线程，因为它的状态被设置为RUNNING
  // 而thread_schedule()只选择RUNNABLE状态的线程
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
}

// 线程调度函数
void thread_schedule(void)
{
  struct thread *t, *next_thread;

  /* 查找另一个可运行的线程 */
  next_thread = 0;
  t = current_thread + 1;
  for(int i = 0; i < MAX_THREAD; i++){
    if(t >= all_thread + MAX_THREAD)
      t = all_thread; // 如果超出数组范围，回到开头
    if(t->state == RUNNABLE) {
      next_thread = t; // 找到可运行线程
      break;
    }
    t = t + 1;
  }

  // 如果没有找到可运行线程，报错并退出
  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  // 如果当前线程不是下一个要运行的线程，进行线程切换
  if (current_thread != next_thread) {
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* 调用thread_switch切换线程上下文 */
    thread_switch(&t->ctx, &next_thread->ctx); // 切换线程上下文
  } else {
    next_thread = 0;
  }
}

// 创建线程函数
void thread_create(void (*func)())
{
  struct thread *t;

  // 寻找空闲的线程槽
  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  
  t->ctx.ra = (uint64)func;       // 返回地址
  // thread_switch 的结尾会返回到 ra，从而运行线程代码
  t->ctx.sp = (uint64)&t->stack + (STACK_SIZE - 1);  // 栈指针
  // 将线程的栈指针指向其独立的栈，注意到栈的生长是从高地址到低地址，所以
  // 要将 sp 设置为指向 stack 的最高地址
}

// 线程让出CPU函数
void thread_yield(void)
{
  current_thread->state = RUNNABLE; // 将当前线程状态改为可运行
  thread_schedule();                // 调用调度器选择下一个线程
}

// 全局变量，用于跟踪线程启动和计数
volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

// 线程A的函数
void thread_a(void)
{
  int i;
  printf("thread_a started\n");
  a_started = 1; // 标记线程A已启动
  
  // 等待其他线程启动
  while(b_started == 0 || c_started == 0)
    thread_yield();
  
  // 执行100次循环，每次循环让出CPU
  for (i = 0; i < 100; i++) {
    printf("thread_a %d\n", i);
    a_n += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", a_n);

  // 标记线程为FREE状态并再次调度
  current_thread->state = FREE;
  thread_schedule();
}

// 线程B的函数
void thread_b(void)
{
  int i;
  printf("thread_b started\n");
  b_started = 1; // 标记线程B已启动
  
  // 等待其他线程启动
  while(a_started == 0 || c_started == 0)
    thread_yield();
  
  // 执行100次循环，每次循环让出CPU
  for (i = 0; i < 100; i++) {
    printf("thread_b %d\n", i);
    b_n += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", b_n);

  // 标记线程为FREE状态并再次调度
  current_thread->state = FREE;
  thread_schedule();
}

// 线程C的函数
void thread_c(void)
{
  int i;
  printf("thread_c started\n");
  c_started = 1; // 标记线程C已启动
  
  // 等待其他线程启动
  while(a_started == 0 || b_started == 0)
    thread_yield();
  
  // 执行100次循环，每次循环让出CPU
  for (i = 0; i < 100; i++) {
    printf("thread_c %d\n", i);
    c_n += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", c_n);

  // 标记线程为FREE状态并再次调度
  current_thread->state = FREE;
  thread_schedule();
}

// 主函数
int main(int argc, char *argv[]) 
{
  // 初始化全局变量
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  
  // 初始化线程系统
  thread_init();
  
  // 创建三个线程
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  
  // 开始线程调度
  thread_schedule();
  
  exit(0);
}