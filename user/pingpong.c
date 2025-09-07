#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    char buf = 'p';
    int p_c[2];     //父进程到子进程
    int c_p[2];     //子进程到父进程
    pipe(p_c);
    pipe(c_p);
    int pid = fork();
    if (pid == 0) {
        // 子进程
        /*子进程向父进程接收数据*/
        close(p_c[1]);//关闭父进程到子进程的写入端
        read(p_c[0], &buf, 1); // 子进程从父进程读取数据
        fprintf(1, "%d: received ping\n", getpid());//接收到数据打印received ping
        close(p_c[0]);//关闭父进程到子进程的读取端
        /*子进程向父进程发送数据*/
        close(c_p[0]);//关闭子进程到父进程的读取端
        write(c_p[1], &buf, 1); // 向父进程发送数据
        close(c_p[1]);//关闭子进程到父进程的写入端
        exit(0);
    } else {
        // 父进程
        /*从父进程向子进程发送数据*/
        close(p_c[0]);//关闭父进程到子进程的读取端
        write(p_c[1], &buf, 1); // 向子进程发送数据
        close(p_c[1]);//关闭父进程到子进程的写入端
        /*从子进程向父进程接收数据*/
        close(c_p[1]);//关闭子进程到父进程的写入端
        read(c_p[0], &buf, 1); // 从子进程读取数据
        fprintf(1, "%d: received pong\n", getpid());//接收到数据打印received pong
        close(c_p[0]);//关闭子进程到父进程的读取端
        exit(0);
    }
}
