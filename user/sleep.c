#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[])
{
  if (argc != 2) { //参数错误
    fprintf(2, "usage: sleep <time>\n");//将错误信息输出到标准错误
    exit(1);
  }
  sleep(atoi(argv[1]));//argv[1]是传入的命令行参数，表示要睡眠的时间（秒），将其转换为整数后传递给sleep函数
  exit(0);
}
