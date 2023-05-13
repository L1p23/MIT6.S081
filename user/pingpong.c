#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int p2c[2], c2p[2];
    char buf = '1';
    if (pipe(p2c) < 0) {
        fprintf(2, "pingpong: pipe failed\n");
        exit(1);
    }
    if (pipe(c2p) < 0) {
        fprintf(2, "pingpong: pipe failed\n");
        exit(1);
    }
    int pid = fork();
    if (pid == 0) { // child
        close(c2p[0]);
        close(p2c[1]);
        if (read(p2c[0], &buf, 1) < 0) {
            fprintf(2, "pingpong: pipe read failed\n");
            exit(1);
        }
        fprintf(1, "%d: received ping\n", getpid());
        if (write(c2p[1], &buf, 1) < 0) {
            fprintf(2, "pingpong: pipe write failed\n");
            exit(1);
        }
        close(c2p[1]);
        close(p2c[0]);
        exit(0);
    }
    else if (pid < 0) {
        fprintf(2, "pingpong: fork failed\n");
        exit(1);
    }
    else {  //parent
        close(p2c[0]);
        close(c2p[1]);
        if (write(p2c[1], &buf, 1) < 0) {
            fprintf(2, "pingpong: pipe write failed\n");
            exit(1);
        }
        if (read(c2p[0], &buf, 1) < 0) {
            fprintf(2, "pingpong: pipe read failed\n");
            exit(1);
        }
        fprintf(1, "%d: received pong\n", getpid());
        wait(0);
    }
    close(p2c[1]);
    close(c2p[0]);
    exit(0);
}