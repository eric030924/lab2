
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(void) {
    int fd[2];

    if (pipe(fd) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return 1;
    }

    if (pid == 0) {
        close(fd[1]);

        char buffer[256];
        ssize_t n = read(fd[0], buffer, sizeof(buffer) - 1);

        if (n < 0) {
            perror("read");
            close(fd[0]);
            return 1;
        }

        buffer[n] = '\0';

        printf("Child PID: %ld\n", (long)getpid());
        printf("Child received: %s\n", buffer);

        close(fd[0]);
        return 0;
    }

    close(fd[0]);

    const char *message = "Hello from parent process";

    printf("Parent PID: %ld\n", (long)getpid());
    printf("Parent sends: %s\n", message);
    fflush(stdout);

    if (write(fd[1], message, strlen(message)) == -1) {
        perror("write");
        close(fd[1]);
        waitpid(pid, NULL, 0);
        return 1;
    }

    close(fd[1]);
    waitpid(pid, NULL, 0);

    printf("IPC completed successfully.\n");

    return 0;
}

