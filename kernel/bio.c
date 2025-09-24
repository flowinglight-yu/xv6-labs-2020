// 缓冲区缓存（Buffer Cache）
//
// 缓冲区缓存是一个由 buf 结构体组成的链表，用于保存磁盘块内容的缓存副本。
// 在内存中缓存磁盘块可以减少磁盘读取次数，并为多个进程使用的磁盘块提供同步点。
//
// 接口说明：
// * 要获取特定磁盘块的缓冲区，请调用 bread
// * 修改缓冲区数据后，调用 bwrite 将其写入磁盘
// * 使用完缓冲区后，调用 brelse 释放
// * 调用 brelse 后不要继续使用缓冲区
// * 一次只能有一个进程使用缓冲区，因此不要不必要地长时间持有缓冲区

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 哈希桶数量，使用质数可以减少哈希冲突
#define NBUFMAP_BUCKET 13
// 缓冲区哈希函数，根据设备号和块号计算哈希值
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

// 缓冲区缓存全局结构（使用哈希表优化版本）
struct {
  struct spinlock eviction_lock;  // 驱逐锁，保护缓冲区驱逐过程
  struct buf buf[NBUF];           // 缓冲区数组，NBUF 个缓冲区
  
  // 哈希表，用于根据 (设备号, 块号) 快速查找缓冲区
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];  // 每个哈希桶的独立锁
} bcache;

// 初始化缓冲区缓存
void
binit(void)
{
  // 初始化哈希表
  for(int i = 0; i < NBUFMAP_BUCKET; i++) {
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap");  // 初始化每个桶的锁
    bcache.bufmap[i].next = 0;  // 初始化桶链表为空
  }

  // 初始化所有缓冲区
  for(int i = 0; i < NBUF; i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");  // 初始化缓冲区的睡眠锁
    b->lastuse = 0;    // 初始化最后使用时间为0
    b->refcnt = 0;     // 初始化引用计数为0
    // 将所有缓冲区暂时放入哈希桶0中
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }

  // 初始化驱逐锁
  initlock(&bcache.eviction_lock, "bcache_eviction");
}

// 在缓冲区缓存中查找指定设备号和块号的缓冲区
// 如果未找到，则分配一个缓冲区
// 无论哪种情况，都返回一个已加锁的缓冲区
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 计算哈希值确定目标桶
  uint key = BUFMAP_HASH(dev, blockno);

  // 获取目标桶的锁
  acquire(&bcache.bufmap_locks[key]);

  // 检查块是否已经被缓存？
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      // 找到缓存的缓冲区，增加引用计数
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);  // 释放桶锁
      acquiresleep(&b->lock);              // 获取缓冲区的睡眠锁
      return b;
    }
  }

  // 未缓存，需要释放桶锁以避免死锁
  release(&bcache.bufmap_locks[key]);
  
  // 获取驱逐锁，防止多个CPU同时进行驱逐操作
  acquire(&bcache.eviction_lock);

  // 再次检查块是否已经被缓存？（防止竞争条件）
  // 在持有驱逐锁的情况下，没有其他驱逐和重用操作会发生
  // 这意味着任何桶的链表结构都不会改变，因此可以安全地遍历
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      // 再次检查时发现已被缓存，增加引用计数
      acquire(&bcache.bufmap_locks[key]);  // 必须获取桶锁来修改引用计数
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 仍然未缓存，需要找到一个可重用的缓冲区
  // 现在只持有驱逐锁，没有持有任何桶锁，可以安全地获取任何桶锁而不会导致死锁

  // 在所有桶中查找最近最久未使用的缓冲区
  struct buf *before_least = 0;  // 指向最近最久未使用缓冲区的前一个节点
  uint holding_bucket = -1;      // 当前持有锁的桶索引
  
  // 遍历所有哈希桶寻找最近最久未使用的缓冲区
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    // 获取当前桶的锁
    acquire(&bcache.bufmap_locks[i]);
    int newfound = 0;  // 标记是否在当前桶中找到新的最近最久未使用缓冲区
    
    // 遍历当前桶中的缓冲区
    for(b = &bcache.bufmap[i]; b->next; b = b->next) {
      // 查找引用计数为0且最后使用时间最早的缓冲区
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        before_least = b;
        newfound = 1;
      }
    }
    
    // 如果当前桶中没有找到合适的缓冲区，释放其锁
    if(!newfound) {
      release(&bcache.bufmap_locks[i]);
    } else {
      // 如果找到更好的候选缓冲区，释放之前持有的桶锁（如果有）
      if(holding_bucket != -1) release(&bcache.bufmap_locks[holding_bucket]);
      holding_bucket = i;
      // 继续持有当前桶的锁...
    }
  }
  
  // 如果没有找到可用的缓冲区，系统崩溃
  if(!before_least) {
    panic("bget: no buffers");
  }
  
  // 获取找到的缓冲区
  b = before_least->next;
  
  // 如果缓冲区不在目标桶中，需要将其移动到目标桶
  if(holding_bucket != key) {
    // 从原桶中移除缓冲区
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]);  // 释放原桶锁
    
    // 重新哈希并将缓冲区添加到目标桶
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  
  // 重新初始化缓冲区
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;  // 标记数据无效，需要从磁盘读取
  
  // 释放锁并返回
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);  // 获取缓冲区的睡眠锁
  return b;
}

// 返回一个包含指定块内容的已加锁缓冲区
// 如果缓冲区中的数据无效，会从磁盘读取数据
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);      // 获取缓冲区（可能从缓存或重新分配）
  if(!b->valid) {              // 如果缓冲区数据无效
    virtio_disk_rw(b, 0);      // 从磁盘读取数据（0 表示读操作）
    b->valid = 1;              // 标记数据有效
  }
  return b;
}

// 将缓冲区内容写入磁盘，缓冲区必须已加锁
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))  // 检查当前进程是否持有缓冲区的锁
    panic("bwrite");
  virtio_disk_rw(b, 1);        // 写入磁盘（1 表示写操作）
}

// 释放一个已加锁的缓冲区
// 更新最后使用时间，但不移动缓冲区在哈希桶中的位置
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))  // 安全检查：必须持有缓冲区锁
    panic("brelse");

  releasesleep(&b->lock);      // 释放缓冲区的睡眠锁

  // 计算缓冲区所在的哈希桶
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  // 获取桶锁来修改引用计数
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;  // 减少引用计数
  if (b->refcnt == 0) {
    // 如果没有进程在使用该缓冲区，更新最后使用时间
    b->lastuse = ticks;  // 使用系统时钟滴答数作为时间戳
  }
  release(&bcache.bufmap_locks[key]);  // 释放桶锁
}

// 增加缓冲区的引用计数
void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);  // 计算哈希桶索引

  acquire(&bcache.bufmap_locks[key]);  // 获取桶锁
  b->refcnt++;                         // 增加引用计数
  release(&bcache.bufmap_locks[key]);  // 释放桶锁
}

// 减少缓冲区的引用计数
void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);  // 计算哈希桶索引

  acquire(&bcache.bufmap_locks[key]);  // 获取桶锁
  b->refcnt--;                         // 减少引用计数
  release(&bcache.bufmap_locks[key]);  // 释放桶锁
}
