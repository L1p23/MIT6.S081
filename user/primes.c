#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void primes(int readf) {
    int num;
    int len = read(readf, &num, 4);
    if (len == 0)
        exit(0);
    else if (len < 0) {
        fprintf(2, "primes: pipe read failed\n");
        exit(1);
    }
    int div = num;
    fprintf(1, "prime %d\n", div);
    int fds[2];
    if (pipe(fds) < 0) {
        fprintf(2, "primes: pipe failed\n");
        exit(1);
    }
    int pid = fork();
    if (pid == 0) {
        close(fds[1]);
        primes(fds[0]);
    }
    else if (pid < 0) {
        fprintf(2, "primes: fork failed\n");
        exit(1);
    }
    else {
        close(fds[0]);
        len = read(readf, &num, 4);
        while (len != 0) {
            if (len < 0) {
                fprintf(2, "primes: pipe read failed\n");
                exit(1);
            }
            if (num % div == 0) {
                len = read(readf, &num, 4);
                continue;
            }
            if (write(fds[1], &num, 4) < 0) {
                fprintf(2, "primes: pipe write failed\n");
                exit(1);
            }
            len = read(readf, &num, 4);
        }
        close(fds[1]);
        wait(0);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    int fds[2];
    if (pipe(fds) < 0) {
        fprintf(2, "primes: pipe failed\n");
        exit(1);
    }
    int pid = fork();
    if (pid == 0) {
        close(fds[1]);
        primes(fds[0]);
    }
    else if (pid < 0) {
        fprintf(2, "primes: fork failed\n");
        exit(1);
    }
    else {
        close(fds[0]);
        fprintf(1, "prime 2\n");
        for (int i = 3; i <= 35; i++) {
            if (i % 2 == 0)
                continue;
            if (write(fds[1], &i, 4) < 0) {
                fprintf(2, "primes: pipe write failed\n");
                exit(1);
            }
        }
        close(fds[1]);
        wait(0);
    }
    exit(0);
}