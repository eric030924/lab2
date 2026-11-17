
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

void signal_handler(int signo) {
    (void)signo;

    const char message[] = "Child received SIGUSR1\n";
    write(STDOUT_FILENO, message, sizeof(message) - 1);
}

int main(void) {
    struct sigaction sa = {0};

    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        printf("Child process started. PID: %ld\n",
               (long)getpid());
        fflush(stdout);

        pause();

        printf("Child process finished.\n");
        return 0;
    }

    printf("Parent process. PID: %ld\n", (long)getpid());

    sleep(1);

    printf("Parent sends SIGUSR1 to child.\n");
    fflush(stdout);

    if (kill(pid, SIGUSR1) == -1) {
        perror("kill");
        return 1;
    }

    if (waitpid(pid, NULL, 0) == -1) {
        perror("waitpid");
        return 1;
    }

    printf("Parent: Child process terminated.\n");

    return 0;
}

