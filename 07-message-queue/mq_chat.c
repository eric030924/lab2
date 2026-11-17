
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define QUEUE_AB "/lab2_chat_a_to_b"
#define QUEUE_BA "/lab2_chat_b_to_a"
#define SIZE 256

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "cleanup") == 0) {
        mq_unlink(QUEUE_AB);
        mq_unlink(QUEUE_BA);
        printf("Message queues removed.\n");
        return 0;
    }

    if (argc != 2 ||
        (strcmp(argv[1], "a") != 0 &&
         strcmp(argv[1], "b") != 0)) {
        printf("Usage: %s a|b|cleanup\n", argv[0]);
        return 1;
    }

    struct mq_attr attr = {0};
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = SIZE;

    mqd_t ab = mq_open(QUEUE_AB,
                       O_CREAT | O_RDWR, 0600, &attr);

    if (ab == (mqd_t)-1) {
        perror("mq_open AB");
        return 1;
    }

    mqd_t ba = mq_open(QUEUE_BA,
                       O_CREAT | O_RDWR, 0600, &attr);

    if (ba == (mqd_t)-1) {
        perror("mq_open BA");
        mq_close(ab);
        return 1;
    }

    int is_a = strcmp(argv[1], "a") == 0;

    mqd_t send_q = is_a ? ab : ba;
    mqd_t recv_q = is_a ? ba : ab;

    printf("Connected as user %c.\n", is_a ? 'A' : 'B');
    printf("Type /exit to quit.\n");
    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        mq_close(ab);
        mq_close(ba);
        return 1;
    }

    if (pid == 0) {
        mq_close(send_q);

        char buffer[SIZE];

        while (1) {
            ssize_t n = mq_receive(recv_q, buffer,
                                    sizeof(buffer), NULL);

            if (n < 0) {
                perror("mq_receive");
                break;
            }

            if (n >= SIZE) {
                n = SIZE - 1;
            }

            buffer[n] = '\0';

            printf("\nPeer: %s\n", buffer);
            fflush(stdout);
        }

        mq_close(recv_q);
        _exit(0);
    }

    mq_close(recv_q);

    char message[SIZE];

    while (fgets(message, sizeof(message), stdin) != NULL) {
        message[strcspn(message, "\n")] = '\0';

        if (strcmp(message, "/exit") == 0) {
            break;
        }

        if (message[0] == '\0') {
            continue;
        }

        if (mq_send(send_q, message,
                    strlen(message) + 1, 0) == -1) {
            perror("mq_send");
            break;
        }
    }

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    mq_close(send_q);

    printf("Chat closed.\n");
    return 0;
}

