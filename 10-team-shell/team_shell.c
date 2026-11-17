
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MAX_LINE 2048
#define MAX_TOKENS 128
#define MAX_COMMANDS 8
#define MAX_ARGS 32

/* 명령어의 인자와 입출력 리다이렉션 정보를 저장하는 구조체 */
typedef struct {
    char *argv[MAX_ARGS];
    char *input;
    char *output;
} Command;

/* Ctrl+Z로 중지된 작업의 프로세스 그룹 ID 저장 */
static pid_t stopped_job = 0;

/*
 * 교재 Exercise 8의 getargs() 기능
 * 사용자가 입력한 명령어를 인자 단위로 분리한다.
 * 추가로 <, >, |, & 기호를 공백 없이 입력해도 인식하도록 처리한다.
 */
int getargs(const char *line, char *work,
            size_t size, char *argv[]) {
    size_t j = 0;

    for (size_t i = 0; line[i] != '\0'; i++) {
        char c = line[i];

        /* 쉘 연산자를 발견하면 앞뒤에 공백을 추가한다. */
        if (strchr("<>|&", c) != NULL) {
            if (j + 3 >= size)
                return -1;

            work[j++] = ' ';
            work[j++] = c;
            work[j++] = ' ';
        } else {
            if (j + 1 >= size)
                return -1;

            work[j++] = c;
        }
    }

    work[j] = '\0';

    int narg = 0;
    char *save = NULL;
    char *token = strtok_r(work, " \t\r\n", &save);

    /* 공백과 탭을 기준으로 문자열을 분리한다. */
    while (token != NULL) {
        if (narg >= MAX_TOKENS - 1)
            return -1;

        argv[narg++] = token;
        token = strtok_r(NULL, " \t\r\n", &save);
    }

    /* 인자 배열의 마지막을 NULL로 표시한다. */
    argv[narg] = NULL;

    return narg;
}

/* 입력받은 문자열이 쉘 연산자인지 확인한다. */
int is_operator(const char *s) {
    return strlen(s) == 1 &&
           strchr("<>|&", s[0]) != NULL;
}

/*
 * 분리된 인자들을 실제 실행할 명령어 구조로 변환한다.
 * 파이프, 리다이렉션, 백그라운드 실행 여부를 분석한다.
 */
int parse_commands(char *tokens[], int count,
                   Command cmds[], int *background) {
    memset(cmds, 0, sizeof(Command) * MAX_COMMANDS);

    *background = 0;

    int c = 0;
    int a = 0;

    for (int i = 0; i < count; i++) {
        char *t = tokens[i];

        /* 파이프 기호를 만나면 다음 명령어를 준비한다. */
        if (strcmp(t, "|") == 0) {
            if (a == 0 || c >= MAX_COMMANDS - 1)
                return -1;

            c++;
            a = 0;

        /* 입력 또는 출력 리다이렉션 파일을 지정한다. */
        } else if (strcmp(t, "<") == 0 ||
                   strcmp(t, ">") == 0) {
            if (i + 1 >= count ||
                is_operator(tokens[i + 1]))
                return -1;

            if (t[0] == '<')
                cmds[c].input = tokens[++i];
            else
                cmds[c].output = tokens[++i];

        /* 명령어 마지막에 &가 있으면 백그라운드로 실행한다. */
        } else if (strcmp(t, "&") == 0) {
            if (i != count - 1 || a == 0)
                return -1;

            *background = 1;

        /* 일반 문자열은 명령어의 인자로 저장한다. */
        } else {
            if (a >= MAX_ARGS - 1)
                return -1;

            cmds[c].argv[a++] = t;
        }
    }

    if (a == 0)
        return -1;

    return c + 1;
}

/* 파일의 내용을 읽어 다른 파일에 복사하는 공통 함수 */
int copy_stream(FILE *src, FILE *dst) {
    char buffer[4096];
    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, n, dst) != n)
            return 1;
    }

    return ferror(src) || ferror(dst);
}

/*
 * 과제에서 요구하는 리눅스 명령어를 직접 구현한다.
 * 지원하지 않는 명령어는 -1을 반환하여 execvp()로 처리한다.
 */
int builtin_command(char *argv[]) {
    if (argv[0] == NULL)
        return 0;

    /* pwd: 현재 작업 디렉터리의 경로를 출력한다. */
    if (strcmp(argv[0], "pwd") == 0) {
        char path[4096];

        if (getcwd(path, sizeof(path)) == NULL) {
            perror("pwd");
            return 1;
        }

        printf("%s\n", path);
        return 0;
    }

    /* cd: 현재 작업 디렉터리를 변경한다. */
    if (strcmp(argv[0], "cd") == 0) {
        const char *path = argv[1] ? argv[1] : getenv("HOME");

        if (path == NULL || chdir(path) == -1) {
            perror("cd");
            return 1;
        }

        return 0;
    }

    /* ls: 디렉터리 내부의 파일과 디렉터리 목록을 출력한다. */
    if (strcmp(argv[0], "ls") == 0) {
        const char *path = argv[1] ? argv[1] : ".";
        struct dirent **entries;

        int count = scandir(path, &entries, NULL, alphasort);

        if (count < 0) {
            perror("ls");
            return 1;
        }

        for (int i = 0; i < count; i++) {
            /* 숨김 파일은 출력하지 않는다. */
            if (entries[i]->d_name[0] != '.')
                printf("%s\n", entries[i]->d_name);

            free(entries[i]);
        }

        free(entries);
        return 0;
    }

    /* mkdir: 새로운 디렉터리를 생성한다. */
    if (strcmp(argv[0], "mkdir") == 0) {
        if (!argv[1]) {
            fprintf(stderr, "mkdir: path required\n");
            return 1;
        }

        if (mkdir(argv[1], 0755) == -1) {
            perror("mkdir");
            return 1;
        }

        return 0;
    }

    /* rmdir: 비어 있는 디렉터리를 삭제한다. */
    if (strcmp(argv[0], "rmdir") == 0) {
        if (!argv[1]) {
            fprintf(stderr, "rmdir: path required\n");
            return 1;
        }

        if (rmdir(argv[1]) == -1) {
            perror("rmdir");
            return 1;
        }

        return 0;
    }

    /* ln: 하드 링크 또는 심볼릭 링크를 생성한다. */
    if (strcmp(argv[0], "ln") == 0) {
        int symbolic = argv[1] &&
                       strcmp(argv[1], "-s") == 0;

        char *src = symbolic ? argv[2] : argv[1];
        char *dst = symbolic ? argv[3] : argv[2];

        if (!src || !dst) {
            fprintf(stderr, "Usage: ln [-s] source link\n");
            return 1;
        }

        int result = symbolic ? symlink(src, dst)
                              : link(src, dst);

        if (result == -1) {
            perror("ln");
            return 1;
        }

        return 0;
    }

    /* rm: 지정한 파일을 삭제한다. */
    if (strcmp(argv[0], "rm") == 0) {
        if (!argv[1]) {
            fprintf(stderr, "rm: path required\n");
            return 1;
        }

        if (unlink(argv[1]) == -1) {
            perror("rm");
            return 1;
        }

        return 0;
    }

    /* mv: 파일 이름을 변경하거나 파일을 이동한다. */
    if (strcmp(argv[0], "mv") == 0) {
        if (!argv[1] || !argv[2]) {
            fprintf(stderr, "Usage: mv source destination\n");
            return 1;
        }

        if (rename(argv[1], argv[2]) == -1) {
            perror("mv");
            return 1;
        }

        return 0;
    }

    /* cp: 원본 파일의 내용을 새로운 파일로 복사한다. */
    if (strcmp(argv[0], "cp") == 0) {
        if (!argv[1] || !argv[2]) {
            fprintf(stderr, "Usage: cp source destination\n");
            return 1;
        }

        struct stat a, b;

        /* 원본과 복사 대상이 같은 파일인지 확인한다. */
        if (stat(argv[1], &a) == 0 &&
            stat(argv[2], &b) == 0 &&
            a.st_dev == b.st_dev &&
            a.st_ino == b.st_ino) {
            fprintf(stderr, "cp: same file\n");
            return 1;
        }

        FILE *src = fopen(argv[1], "rb");

        if (!src) {
            perror("cp source");
            return 1;
        }

        FILE *dst = fopen(argv[2], "wb");

        if (!dst) {
            perror("cp destination");
            fclose(src);
            return 1;
        }

        int result = copy_stream(src, dst);

        if (fclose(dst) != 0)
            result = 1;

        fclose(src);

        if (result)
            fprintf(stderr, "cp: copy failed\n");

        return result;
    }

    /* cat: 파일의 내용을 터미널에 출력한다. */
    if (strcmp(argv[0], "cat") == 0) {
        /* 파일명이 없으면 표준 입력을 출력한다. */
        if (!argv[1])
            return copy_stream(stdin, stdout);

        int result = 0;

        for (int i = 1; argv[i]; i++) {
            FILE *file = fopen(argv[i], "rb");

            if (!file) {
                perror("cat");
                result = 1;
                continue;
            }

            if (copy_stream(file, stdout))
                result = 1;

            fclose(file);
        }

        return result;
    }

    /* grep: 지정한 문자열이 포함된 줄을 검색한다. */
    if (strcmp(argv[0], "grep") == 0) {
        if (!argv[1]) {
            fprintf(stderr, "Usage: grep word [file]\n");
            return 1;
        }

        FILE *file = stdin;

        if (argv[2]) {
            file = fopen(argv[2], "r");

            if (!file) {
                perror("grep");
                return 1;
            }
        }

        char *line = NULL;
        size_t capacity = 0;
        int found = 0;

        while (getline(&line, &capacity, file) != -1) {
            if (strstr(line, argv[1]) != NULL) {
                fputs(line, stdout);
                found = 1;
            }
        }

        free(line);

        if (file != stdin)
            fclose(file);

        return found ? 0 : 1;
    }

    /* 직접 구현하지 않은 명령어는 외부 실행 대상으로 처리한다. */
    return -1;
}

/*
 * 입출력 리다이렉션 처리 함수
 * < 기호는 입력 파일을 지정하고,
 * > 기호는 출력 파일을 지정한다.
 */
int apply_redirection(Command *cmd) {
    if (cmd->input != NULL) {
        int fd = open(cmd->input, O_RDONLY);

        if (fd < 0) {
            perror("input");
            return -1;
        }

        /* 표준 입력을 지정한 파일로 변경한다. */
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2 input");
            close(fd);
            return -1;
        }

        close(fd);
    }

    if (cmd->output != NULL) {
        int fd = open(cmd->output,
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd < 0) {
            perror("output");
            return -1;
        }

        /* 표준 출력을 지정한 파일로 변경한다. */
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2 output");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

/* 포그라운드 프로세스 그룹의 종료 또는 중지를 기다린다. */
void wait_foreground(pid_t pgid, pid_t shell_pgid,
                     int interactive) {
    int status;

    while (1) {
        pid_t result = waitpid(-pgid, &status, WUNTRACED);

        if (result < 0) {
            if (errno == EINTR)
                continue;

            break;
        }

        /* Ctrl+Z 등으로 프로세스가 중지되면 작업 정보를 저장한다. */
        if (WIFSTOPPED(status)) {
            stopped_job = pgid;

            printf("\n[Stopped] Job %ld\n", (long)pgid);
            break;
        }
    }

    /* 종료 또는 중지 후 터미널 제어권을 쉘로 돌려준다. */
    if (interactive)
        tcsetpgrp(STDIN_FILENO, shell_pgid);
}

/*
 * 교재의 fork(), execvp(), wait() 구조를 확장한 함수
 * 자식 프로세스를 생성하고 명령어를 실행한다.
 * 파이프, 백그라운드 실행 및 시그널 제어를 지원한다.
 */
void execute_commands(Command cmds[], int count,
                      int background, int interactive,
                      pid_t shell_pgid) {
    int previous_read = -1;
    pid_t pgid = 0;

    for (int i = 0; i < count; i++) {
        int fd[2] = {-1, -1};

        /* 두 명령어 사이에 필요한 파이프를 생성한다. */
        if (i < count - 1 && pipe(fd) == -1) {
            perror("pipe");
            return;
        }

        fflush(NULL);

        /* 자식 프로세스를 생성한다. */
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");

            if (previous_read != -1)
                close(previous_read);

            if (fd[0] != -1)
                close(fd[0]);

            if (fd[1] != -1)
                close(fd[1]);

            return;
        }

        if (pid == 0) {
            /* 자식 프로세스를 해당 프로세스 그룹에 포함한다. */
            setpgid(0, pgid ? pgid : 0);

            /* 자식 프로세스에는 기본 시그널 처리를 적용한다. */
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

            /* 이전 명령어의 출력을 현재 명령어의 입력에 연결한다. */
            if (previous_read != -1 &&
                dup2(previous_read, STDIN_FILENO) < 0)
                _exit(1);

            /* 현재 명령어의 출력을 다음 파이프로 전달한다. */
            if (fd[1] != -1 &&
                dup2(fd[1], STDOUT_FILENO) < 0)
                _exit(1);

            /* 사용하지 않는 파일 디스크립터를 닫는다. */
            if (previous_read != -1)
                close(previous_read);

            if (fd[0] != -1)
                close(fd[0]);

            if (fd[1] != -1)
                close(fd[1]);

            /* 입력 또는 출력 리다이렉션을 적용한다. */
            if (apply_redirection(&cmds[i]) == -1)
                _exit(1);

            /* 먼저 직접 구현한 명령어인지 확인한다. */
            int result = builtin_command(cmds[i].argv);

            if (result >= 0) {
                fflush(NULL);
                _exit(result);
            }

            /* 그 외 명령어는 execvp()로 실행한다. */
            execvp(cmds[i].argv[0], cmds[i].argv);

            perror(cmds[i].argv[0]);
            _exit(127);
        }

        /* 부모 프로세스에서 프로세스 그룹을 관리한다. */
        if (pgid == 0)
            pgid = pid;

        setpgid(pid, pgid);

        if (previous_read != -1)
            close(previous_read);

        if (fd[1] != -1)
            close(fd[1]);

        previous_read = fd[0];
    }

    if (previous_read != -1)
        close(previous_read);

    if (background) {
        /* 백그라운드 실행 시 기다리지 않고 쉘로 복귀한다. */
        printf("[Background] Job %ld\n", (long)pgid);
    } else {
        /* 포그라운드 실행 시 자식 프로세스의 종료를 기다린다. */
        if (interactive)
            tcsetpgrp(STDIN_FILENO, pgid);

        wait_foreground(pgid, shell_pgid, interactive);
    }
}

/*
 * 교재 simplesh.c의 main() 구조를 확장한 함수
 * 명령어 입력, 인자 분석, 프로세스 실행을 반복한다.
 */
int main(void) {
    char line[MAX_LINE];
    char work[MAX_LINE * 3];
    char *tokens[MAX_TOKENS];

    int interactive = isatty(STDIN_FILENO);
    pid_t shell_pgid = getpgrp();

    /* 쉘 자체는 Ctrl+C, Ctrl+Z 등에 의해 종료되지 않도록 한다. */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    printf("=== Lab2 Team Shell ===\n");
    printf("Type exit to quit.\n");

    /* 사용자가 exit를 입력할 때까지 명령어를 반복 실행한다. */
    while (1) {
        /* 종료된 백그라운드 프로세스를 정리한다. */
        while (waitpid(-1, NULL, WNOHANG) > 0) {
        }

        printf("shell> ");
        fflush(stdout);

        /* 교재의 gets() 대신 안전한 fgets()를 사용한다. */
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        /* 입력한 문자열을 명령어와 인자로 분리한다. */
        int narg = getargs(line, work,
                           sizeof(work), tokens);

        if (narg < 0) {
            fprintf(stderr, "Input too long\n");
            continue;
        }

        if (narg == 0)
            continue;

        Command cmds[MAX_COMMANDS];
        int background = 0;

        /* 파이프, 리다이렉션, 백그라운드 실행 여부를 확인한다. */
        int count = parse_commands(
            tokens, narg, cmds, &background
        );

        if (count < 0) {
            fprintf(stderr, "Invalid command syntax\n");
            continue;
        }

        char *name = cmds[0].argv[0];

        /* exit 명령어를 입력하면 쉘 프로그램을 종료한다. */
        if (count == 1 && !background &&
            strcmp(name, "exit") == 0) {
            break;
        }

        /* cd는 부모 프로세스에서 실행해야 현재 경로가 유지된다. */
        if (count == 1 && !background &&
            strcmp(name, "cd") == 0) {
            builtin_command(cmds[0].argv);
            continue;
        }

        /* Ctrl+Z로 중지한 작업을 fg 또는 bg 명령으로 재개한다. */
        if (count == 1 && !background &&
            (strcmp(name, "fg") == 0 ||
             strcmp(name, "bg") == 0)) {
            if (stopped_job == 0) {
                printf("No stopped job.\n");
                continue;
            }

            pid_t job = stopped_job;
            stopped_job = 0;

            int foreground = strcmp(name, "fg") == 0;

            /* fg는 터미널 제어권을 해당 작업에 전달한다. */
            if (foreground && interactive)
                tcsetpgrp(STDIN_FILENO, job);

            /* 중지된 프로세스 그룹에 SIGCONT 시그널을 보낸다. */
            if (kill(-job, SIGCONT) == -1) {
                perror("SIGCONT");

                if (foreground && interactive)
                    tcsetpgrp(STDIN_FILENO, shell_pgid);

                continue;
            }

            /* fg 실행 시 작업이 끝날 때까지 기다린다. */
            if (foreground)
                wait_foreground(job, shell_pgid,
                                interactive);

            continue;
        }

        /* 분석한 명령어들을 자식 프로세스에서 실행한다. */
        execute_commands(cmds, count, background,
                         interactive, shell_pgid);
    }

    printf("Shell terminated.\n");

    return 0;
}

