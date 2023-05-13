#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    char buf;
    char *args[argc];
    for (int i = 1; i < argc; i++)
        args[i - 1] = argv[i];
    args[argc - 1] = (char *)malloc(512);
    memset(args[argc - 1], 0, 512);
    int len = 0;
    while (read(0, &buf, 1) > 0) {
        if (buf != '\n') {
            args[argc - 1][len++] = buf;
            continue;
        }
        else
            args[argc - 1][len] = 0;
        int pid = fork();
        if (pid == 0) {
            exec(argv[1], args);
        }
        else if (pid < 0) {
            fprintf(2, "xargs: fork failed\n");
            exit(1);
        }
        else {
            wait(0);
        }
        len = 0;
    }
    exit(1);
}