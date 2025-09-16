#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define NBUCKET 5        // 定义哈希桶的数量
#define NKEYS 100000     // 定义键的数量
pthread_mutex_t lock [NBUCKET];  // 定义互斥锁

// 定义哈希表节点结构
struct entry {
  int key;               // 键
  int value;             // 值
  struct entry *next;    // 下一个节点的指针
};
struct entry *table[NBUCKET];  // 哈希表数组
int keys[NKEYS];         // 存储所有键的数组
int nthread = 1;         // 线程数，默认为1

// 获取当前时间函数
double 
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// 向哈希表中插入新节点
static void 
insert(int key, int value, struct entry **p, struct entry *n)
{
  struct entry *e = malloc(sizeof(struct entry));  // 分配新节点内存
  e->key = key;            // 设置键
  e->value = value;        // 设置值
  e->next = n;             // 设置下一个节点
  *p = e;                  // 将新节点链接到哈希表
}

// 插入或更新键值对
static 
void 
put(int key, int value)
{
  
  int i = key % NBUCKET;   // 计算哈希值，确定桶的位置
  pthread_mutex_lock(&lock[i]);// 给散列桶加锁，确保线程安全
  // 检查键是否已存在
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // 如果键已存在，更新值
    e->value = value;
  } else {
    // 如果键不存在，插入新节点
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&lock[i]);// 解锁
}

// 根据键查找值
static struct entry*
get(int key)
{ 
  int i = key % NBUCKET;   // 计算哈希值，确定桶的位置
  pthread_mutex_lock(&lock[i]);// 给散列桶加锁，确保线程安全
  // 在对应的桶中查找键
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) break;
  }

  pthread_mutex_unlock(&lock[i]);// 解锁
  return e;  // 返回找到的节点指针，如果没找到返回NULL
}

// 线程函数：执行插入操作
static void *
put_thread(void *xa)
{
  int n = (int) (long) xa; // 线程编号
  int b = NKEYS/nthread;   // 每个线程处理的键数量

  // 每个线程插入分配给它的键
  for (int i = 0; i < b; i++) {
    put(keys[b*n + i], n);
  }

  return NULL;
}

// 线程函数：执行查找操作
static void *
get_thread(void *xa)
{
  int n = (int) (long) xa; // 线程编号
  int missing = 0;          // 记录未找到的键数量

  // 查找所有键
  for (int i = 0; i < NKEYS; i++) {
    struct entry *e = get(keys[i]);
    if (e == 0) missing++;  // 如果键未找到，计数增加
  }
  printf("%d: %d keys missing\n", n, missing);  // 输出未找到的键数量
  return NULL;
}

// 主函数
int
main(int argc, char *argv[])
{
  pthread_t *tha;     // 线程数组指针
  void *value;        // 线程返回值
  double t1, t0;      // 时间记录变量

  // 检查命令行参数
  if (argc < 2) {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);  // 获取线程数
  tha = malloc(sizeof(pthread_t) * nthread);  // 分配线程数组内存
  srandom(0);            // 初始化随机数生成器
  assert(NKEYS % nthread == 0);  // 确保键数能被线程数整除
  // 生成随机键
  for (int i = 0; i < NKEYS; i++) {
    keys[i] = random();
  }

  // 初始化互斥锁, 每个桶一个锁
  for(int i = 0; i < NBUCKET; i++) {
    pthread_mutex_init(&lock[i], NULL);
  }
  
  // 首先执行插入操作
  t0 = now();  // 记录开始时间
  // 创建线程执行插入操作
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) (long) i) == 0);
  }
  // 等待所有插入线程完成
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();  // 记录结束时间

  // 输出插入性能统计
  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));

  
  // 然后执行查找操作
  t0 = now();  // 记录开始时间
  // 创建线程执行查找操作
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *) (long) i) == 0);
  }
  // 等待所有查找线程完成
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();  // 记录结束时间

  // 输出查找性能统计
  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         NKEYS*nthread, t1 - t0, (NKEYS*nthread) / (t1 - t0));
}