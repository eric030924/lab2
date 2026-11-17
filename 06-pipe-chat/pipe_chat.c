
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define FIFO_AB "/tmp/lab2_chat_ab"
#define FIFO_BA "/tmp/lab2_chat_ba"
#define BUFFER_SIZE 512

int create_fifo(const char *path) {
    if (mkfifo(path, 0600) == -1 && errno != EEXIST) {
        perror("mkfifo");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2 ||
        (strcmp(argv[1], "a") != 0 &&
         strcmp(argv[1], "b") != 0)) {
        printf("Usage: %s a|b\n", argv[0]);
        return 1;
    }

    if (create_fifo(FIFO_AB) == -1 ||
        create_fifo(FIFO_BA) == -1) {
        return 1;
    }

    int is_a = strcmp(argv[1], "a") == 0;
    int read_fd, write_fd;

    printf("Waiting for another user...\n");
    fflush(stdout);

    if (is_a) {
        write_fd = open(FIFO_AB, O_WRONLY);
        if (write_fd == -1) {
            perror("open write");
            return 1;
        }

        read_fd = open(FIFO_BA, O_RDONLY);
    } else {
        read_fd = open(FIFO_AB, O_RDONLY);
        if (read_fd == -1) {
            perror("open read");
            return 1;
        }

        write_fd = open(FIFO_BA, O_WRONLY);
    }

    if (read_fd == -1 || write_fd == -1) {
        perror("open FIFO");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        close(read_fd);
        close(write_fd);
        return 1;
    }

    if (pid == 0) {
        close(write_fd);

        char buffer[BUFFER_SIZE];
        ssize_t n;

        while ((n = read(read_fd, buffer,
                         sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            printf("\nPeer: %s", buffer);
            fflush(stdout);
        }

        close(read_fd);
        _exit(0);
    }

    close(read_fd);

    printf("Connected as user %c.\n", is_a ? 'A' : 'B');
    printf("Type /exit to quit.\n");

    char message[BUFFER_SIZE];

    while (fgets(message, sizeof(message), stdin) != NULL) {
        if (strcmp(message, "/exit\n") == 0 ||
            strcmp(message, "/exit") == 0) {
            break;
        }

        if (write(write_fd, message,
                  strlen(message)) == -1) {
            perror("write");
            break;
        }
    }

    close(write_fd);

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    printf("Chat closed.\n");
    return 0;
}

