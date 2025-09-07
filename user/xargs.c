// 引入必要的头文件
#include "kernel/types.h"  // 包含基本数据类型定义
#include "user/user.h"    // 包含用户态系统调用接口
#include "kernel/param.h"  // 包含命令行参数相关定义

#define MAXSZ 512  // 定义输入缓冲区最大大小

// 有限状态自动机状态定义，用于解析输入字符
enum state {
  S_WAIT,         // 等待参数输入状态（初始状态或当前字符为空格）
  S_ARG,          // 正在读取参数内容的状态
  S_ARG_END,      // 参数结束状态（遇到空格）
  S_ARG_LINE_END, // 左侧有参数的换行状态（例如"arg\n"）
  S_LINE_END,     // 左侧为空格的换行状态（例如"arg  \n"）
  S_END           // 结束状态（遇到文件结束符EOF）
};

// 字符类型定义，用于状态机判断
enum char_type {
  C_SPACE,    // 空格字符
  C_CHAR,     // 普通字符（非空格、非换行符）
  C_LINE_END  // 换行符
};

/**
 * @brief 获取字符类型
 *
 * @param c 待判定的字符
 * @return enum char_type 字符类型
 */
enum char_type get_char_type(char c)
{
  switch (c) {
  case ' ':       // 空格字符
    return C_SPACE;
  case '\n':      // 换行符
    return C_LINE_END;
  default:        // 其他所有字符视为普通字符
    return C_CHAR;
  }
}

/**
 * @brief 状态转换函数，根据当前状态和输入字符类型确定下一个状态
 *
 * @param cur 当前的状态
 * @param ct 将要读取的字符类型
 * @return enum state 转换后的状态
 */
enum state transform_state(enum state cur, enum char_type ct)
{
  switch (cur) {
  case S_WAIT:  // 当前处于等待参数输入状态
    if (ct == C_SPACE)    return S_WAIT;        // 空格 -> 保持等待状态
    if (ct == C_LINE_END) return S_LINE_END;    // 换行 -> 转换为行结束状态
    if (ct == C_CHAR)     return S_ARG;         // 普通字符 -> 转换为参数内状态
    break;
  case S_ARG:   // 当前处于参数内状态
    if (ct == C_SPACE)    return S_ARG_END;     // 空格 -> 参数结束状态
    if (ct == C_LINE_END) return S_ARG_LINE_END;// 换行 -> 参数行结束状态
    if (ct == C_CHAR)     return S_ARG;         // 普通字符 -> 保持参数内状态
    break;
  case S_ARG_END:      // 参数结束状态
  case S_ARG_LINE_END: // 参数行结束状态
  case S_LINE_END:     // 行结束状态
    // 这些状态后遇到空格、换行或字符的处理方式相同
    if (ct == C_SPACE)    return S_WAIT;        // 空格 -> 等待参数输入状态
    if (ct == C_LINE_END) return S_LINE_END;    // 换行 -> 行结束状态
    if (ct == C_CHAR)     return S_ARG;         // 普通字符 -> 参数内状态
    break;
  default:
    break;
  }
  return S_END;  // 默认返回结束状态
}

/**
 * @brief 将参数列表后面的元素全部置为空指针
 *        用于换行时，重新初始化参数数组
 *
 * @param x_argv 参数指针数组
 * @param beg 要清空的起始下标
 */
void clearArgv(char *x_argv[MAXARG], int beg)
{
  // 从指定位置开始，将后续所有参数指针置为空
  for (int i = beg; i < MAXARG; ++i)
    x_argv[i] = 0;
}

/**
 * @brief xargs命令的主函数
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 * @return int 程序退出码
 */
int main(int argc, char *argv[])
{
  // 检查参数数量是否超过系统限制
  if (argc - 1 >= MAXARG) {
    fprintf(2, "xargs: 参数过多.\n");
    exit(1);
  }
  
  char lines[MAXSZ];          // 输入缓冲区
  char *p = lines;            // 指向缓冲区内当前写入位置的指针
  char *x_argv[MAXARG] = {0}; // 参数指针数组，全部初始化为空指针

  // 存储原有的参数（即xargs后面的命令和其参数）
  for (int i = 1; i < argc; ++i) {
    x_argv[i - 1] = argv[i];
  }
  
  int arg_beg = 0;          // 当前参数的起始位置在缓冲区中的索引
  int arg_end = 0;          // 当前参数的结束位置在缓冲区中的索引
  int arg_cnt = argc - 1;   // 当前已存储的参数个数
  enum state st = S_WAIT;   // 初始状态设置为等待参数输入状态

  // 主循环：处理输入直到结束状态
  while (st != S_END) {
    // 从标准输入读取一个字符
    if (read(0, p, sizeof(char)) != sizeof(char)) {
      st = S_END;  // 读取失败或到达文件末尾，设置为结束状态
    } else {
      // 根据读取的字符类型转换状态
      st = transform_state(st, get_char_type(*p));
    }

    // 检查缓冲区是否已满
    if (++arg_end >= MAXSZ) {
      fprintf(2, "xargs: 参数过长.\n");
      exit(1);
    }

    // 根据当前状态进行相应处理
    switch (st) {
    case S_WAIT:          // 等待参数输入状态
      ++arg_beg;          // 参数起始位置前移，跳过空格
      break;
    case S_ARG_END:       // 参数结束状态（遇到空格）
      x_argv[arg_cnt++] = &lines[arg_beg]; // 将当前参数添加到参数数组
      arg_beg = arg_end;  // 重置参数起始位置为当前位置
      *p = '\0';          // 将当前位置的字符替换为字符串结束符
      break;
    case S_ARG_LINE_END:  // 参数行结束状态（参数后直接换行）
      x_argv[arg_cnt++] = &lines[arg_beg]; // 将当前参数添加到参数数组
      // 注意：这里没有break，会继续执行S_LINE_END的处理逻辑
    case S_LINE_END:      // 行结束状态（空格后换行）
      arg_beg = arg_end;  // 重置参数起始位置为当前位置
      *p = '\0';          // 将当前位置的字符替换为字符串结束符
      
      // 创建子进程执行命令
      if (fork() == 0) {
        // 子进程：执行命令，使用收集到的参数
        exec(argv[1], x_argv);
        // 如果exec返回，说明执行失败
        exit(1);
      }
      
      // 父进程：重置参数计数和参数数组
      arg_cnt = argc - 1;
      clearArgv(x_argv, arg_cnt);
      
      // 等待子进程结束
      wait(0);
      break;
    default:
      break;
    }

    ++p;    // 移动指针到缓冲区的下一个位置
  }
  
  exit(0);  // 正常退出
}