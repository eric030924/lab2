
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <strings.h>
#include <time.h>
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

typedef struct {
    char *text;
    char operator;
} Token;

/* 따옴표 안의 |와 >를 파일 이름으로 쓸 수 있도록 토큰의 종류도 저장한다. */
static int getargs(const char *line, char *work, size_t size, Token tokens[]) {
    size_t used = 0;
    int count = 0;

    while (*line) {
        if (isspace((unsigned char)*line)) {
            line++;
            continue;
        }

        if (count == MAX_TOKENS) {
            fprintf(stderr, "Too many tokens\n");
            return -1;
        }

        Token *token = &tokens[count++];
        token->text = &work[used];
        token->operator = 0;

        if (strchr("<>|&", *line)) {
            token->operator = *line++;
            token->text = NULL;
            continue;
        }

        char quote = 0;
        while (*line) {
            char c = *line;
            if (!quote && (isspace((unsigned char)c) || strchr("<>|&", c)))
                break;

            line++;
            if (!quote && (c == '\'' || c == '"')) {
                quote = c;
                continue;
            }
            if (quote && c == quote) {
                quote = 0;
                continue;
            }
            if (c == '\\' && quote != '\'') {
                if (!*line) {
                    fprintf(stderr, "Missing character after backslash\n");
                    return -1;
                }
                if (!quote || *line == '"' || *line == '\\')
                    c = *line++;
            }

            if (used + 1 >= size) {
                fprintf(stderr, "Input too long\n");
                return -1;
            }
            work[used++] = c;
        }

        if (quote) {
            fprintf(stderr, "Unclosed quote\n");
            return -1;
        }
        if (used == size) {
            fprintf(stderr, "Input too long\n");
            return -1;
        }
        work[used++] = '\0';
    }
    return count;
}

/*
 * 분리된 인자들을 실제 실행할 명령어 구조로 변환한다.
 * 파이프, 리다이렉션, 백그라운드 실행 여부를 분석한다.
 */
int parse_commands(Token tokens[], int count,
                   Command cmds[], int *background) {
    memset(cmds, 0, sizeof(Command) * MAX_COMMANDS);

    *background = 0;

    int c = 0;
    int a = 0;

    for (int i = 0; i < count; i++) {
        Token token = tokens[i];

        /* 파이프 기호를 만나면 다음 명령어를 준비한다. */
        if (token.operator == '|') {
            if (a == 0 || c >= MAX_COMMANDS - 1)
                return -1;

            c++;
            a = 0;

        /* 입력 또는 출력 리다이렉션 파일을 지정한다. */
        } else if (token.operator == '<' || token.operator == '>') {
            if (i + 1 >= count ||
                tokens[i + 1].operator)
                return -1;

            if (token.operator == '<')
                cmds[c].input = tokens[++i].text;
            else
                cmds[c].output = tokens[++i].text;

        /* 명령어 마지막에 &가 있으면 백그라운드로 실행한다. */
        } else if (token.operator == '&') {
            if (i != count - 1 || a == 0)
                return -1;

            *background = 1;

        /* 일반 문자열은 명령어의 인자로 저장한다. */
        } else {
            if (a >= MAX_ARGS - 1)
                return -1;

            cmds[c].argv[a++] = token.text;
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

/* 디렉터리 안의 항목을 stat할 때 사용할 경로를 만든다. */
static char *join_path(const char *directory, const char *name) {
    size_t length = strlen(directory);
    int slash = length > 0 && directory[length - 1] != '/';
    size_t capacity = length + slash + strlen(name) + 1;
    char *joined = malloc(capacity);

    if (joined == NULL) {
        perror("path");
        return NULL;
    }
    snprintf(joined, capacity, "%s%s%s", directory, slash ? "/" : "", name);
    return joined;
}

static char file_type(mode_t mode) {
    if (S_ISDIR(mode))  return 'd';
    if (S_ISLNK(mode))  return 'l';
    if (S_ISFIFO(mode)) return 'p';
    if (S_ISCHR(mode))  return 'c';
    if (S_ISBLK(mode))  return 'b';
    if (S_ISSOCK(mode)) return 's';
    return '-';
}

/* ls -l에 표시할 파일 종류와 소유자·그룹·기타 사용자의 권한 */
static void format_mode(mode_t mode, char result[11]) {
    static const mode_t bits[] = {
        S_IRUSR, S_IWUSR, S_IXUSR,
        S_IRGRP, S_IWGRP, S_IXGRP,
        S_IROTH, S_IWOTH, S_IXOTH
    };
    result[0] = file_type(mode);
    for (size_t i = 0; i < 9; i++)
        result[i + 1] = (mode & bits[i]) ? "rwx"[i % 3] : '-';

    if (mode & S_ISUID)
        result[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID)
        result[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & 01000)
        result[9] = (mode & S_IXOTH) ? 't' : 'T';
    result[10] = '\0';
}

static void print_long_entry(const char *label, const struct stat *info) {
    char permissions[11];
    char date[32];
    struct tm modified;
    format_mode(info->st_mode, permissions);
    if (!localtime_r(&info->st_mtime, &modified) ||
        !strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &modified))
        strcpy(date, "?");

    /* 계정 이름을 알 수 없는 환경에서도 소유자를 표시하도록 UID/GID를 사용한다. */
    printf("%s %ju %ju %ju %jd %s %s\n", permissions,
           (uintmax_t)info->st_nlink, (uintmax_t)info->st_uid,
           (uintmax_t)info->st_gid, (intmax_t)info->st_size, date, label);
}

static int list_path(const char *path, int all, int detailed) {
    struct stat info;
    if (lstat(path, &info) == -1) {
        perror(path);
        return 1;
    }

    if (!detailed && S_ISLNK(info.st_mode)) {
        struct stat target;
        if (stat(path, &target) == 0)
            info = target;
    }

    if (!S_ISDIR(info.st_mode)) {
        if (detailed)
            print_long_entry(path, &info);
        else
            puts(path);
        return 0;
    }

    struct dirent **entries;
    int count = scandir(path, &entries, NULL, alphasort);
    if (count < 0) {
        perror(path);
        return 1;
    }

    int result = 0;
    for (int i = 0; i < count; i++) {
        const char *name = entries[i]->d_name;
        if (all || name[0] != '.') {
            if (!detailed) {
                puts(name);
            } else {
                char *full = join_path(path, name);
                if (!full) {
                    result = 1;
                } else if (lstat(full, &info) == -1) {
                    perror(full);
                    result = 1;
                } else {
                    print_long_entry(name, &info);
                }
                free(full);
            }
        }
        free(entries[i]);
    }
    free(entries);
    return result;
}

static int run_ls(char *argv[]) {
    int all = 0, detailed = 0;
    int i = 1;

    for (; argv[i] && argv[i][0] == '-' && argv[i][1]; i++) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (const char *flag = argv[i] + 1; *flag; flag++) {
            switch (*flag) {
            case 'a':
                all = 1;
                break;
            case 'l':
                detailed = 1;
                break;
            default:
                fprintf(stderr, "Usage: ls [-al] [--] [path ...]\n");
                return 1;
            }
        }
    }
    if (!argv[i])
        return list_path(".", all, detailed);

    int result = 0;
    int multiple = argv[i + 1] != NULL;
    int first = i;
    for (; argv[i]; i++) {
        if (multiple) {
            if (i != first)
                putchar('\n');
            printf("%s:\n", argv[i]);
        }
        if (list_path(argv[i], all, detailed))
            result = 1;
    }
    return result;
}

typedef struct {
    int number_lines;
    int ignore_case;
    int invert;
    const char *pattern;
} GrepOptions;

static int contains_text(const char *line, const GrepOptions *options) {
    if (!options->ignore_case)
        return strstr(line, options->pattern) != NULL;

    size_t length = strlen(options->pattern);
    if (length == 0)
        return 1;

    for (; *line; line++) {
        if (!strncasecmp(line, options->pattern, length))
            return 1;
    }
    return 0;
}

/* 표준 입력과 파일에 같은 줄 번호·문자열 비교 규칙을 적용한다. */
static int grep_stream(FILE *input, const char *label, int show_name,
                       const GrepOptions *options) {
    char *line = NULL;
    size_t capacity = 0, number = 0;
    int found = 0;

    while (getline(&line, &capacity, input) >= 0) {
        number++;
        if (contains_text(line, options) == options->invert)
            continue;
        if (show_name)
            printf("%s:", label);
        if (options->number_lines)
            printf("%zu:", number);
        fputs(line, stdout);
        found = 1;
    }
    free(line);

    if (ferror(input)) {
        perror(label);
        return 2;
    }
    return found ? 0 : 1;
}

static int run_grep(char *argv[]) {
    GrepOptions options = {0};
    int i = 1;
    for (; argv[i] && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        for (const char *flag = argv[i] + 1; *flag; flag++) {
            switch (*flag) {
            case 'n':
                options.number_lines = 1;
                break;
            case 'i':
                options.ignore_case = 1;
                break;
            case 'v':
                options.invert = 1;
                break;
            default:
                fprintf(stderr, "Usage: grep [-niv] [--] word [file ...]\n");
                return 2;
            }
        }
    }

    if (!argv[i]) {
        fprintf(stderr, "grep: pattern required\n");
        return 2;
    }
    options.pattern = argv[i++];
    if (!argv[i])
        return grep_stream(stdin, "(standard input)", 0, &options);

    int multiple = argv[i + 1] != NULL;
    int result = 1;
    for (; argv[i]; i++) {
        int use_stdin = !strcmp(argv[i], "-");
        FILE *input = use_stdin ? stdin : fopen(argv[i], "r");
        if (!input) {
            perror(argv[i]);
            result = 2;
            continue;
        }

        const char *label = use_stdin ? "(standard input)" : argv[i];
        int status = grep_stream(input, label, multiple, &options);
        if (!use_stdin)
            fclose(input);
        if (status == 2 || (status == 0 && result == 1))
            result = status;
    }
    return result;
}

/* 대상 파일을 열고 같은 inode인지 확인한 뒤 내용을 덮어쓴다. */
static int copy_file(const char *source, const char *destination) {
    FILE *input = fopen(source, "rb");
    if (!input) {
        perror(source);
        return 1;
    }

    struct stat from, to;
    if (fstat(fileno(input), &from) < 0) {
        perror(source);
        fclose(input);
        return 1;
    }
    if (S_ISDIR(from.st_mode)) {
        fprintf(stderr, "cp: %s: source is a directory\n", source);
        fclose(input);
        return 1;
    }

    int fd = open(destination, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
        perror(destination);
        fclose(input);
        return 1;
    }

    int result = 1;
    if (fstat(fd, &to) < 0) {
        perror(destination);
    } else if (from.st_dev == to.st_dev && from.st_ino == to.st_ino) {
        fprintf(stderr, "cp: same file\n");
    } else if (S_ISREG(to.st_mode) && ftruncate(fd, 0) < 0) {
        perror(destination);
    } else {
        FILE *output = fdopen(fd, "wb");
        if (!output) {
            perror(destination);
        } else {
            result = copy_stream(input, output);
            if (fclose(output) != 0)
                result = 1;
            if (result)
                fprintf(stderr, "cp: copy failed: %s\n", destination);
            fd = -1;
        }
    }

    if (fd >= 0)
        close(fd);
    fclose(input);
    return result;
}

/* 마지막 인자가 디렉터리이면 원본 이름을 그대로 붙여 목적지를 정한다. */
static char *destination_in(const char *directory, const char *source) {
    size_t end = strlen(source);
    while (end && source[end - 1] == '/')
        end--;
    if (!end) {
        fprintf(stderr, "Missing source name\n");
        return NULL;
    }
    size_t start = end;
    while (start && source[start - 1] != '/')
        start--;

    char *name = strndup(source + start, end - start);
    if (!name) {
        perror("source name");
        return NULL;
    }
    char *destination = join_path(directory, name);
    free(name);
    return destination;
}

static int run_transfer(char *argv[], int move) {
    int first = argv[1] && !strcmp(argv[1], "--") ? 2 : 1;
    int count = first;
    while (argv[count])
        count++;

    if (count - first < 2) {
        fprintf(stderr, "Usage: %s source ... destination\n", argv[0]);
        return 1;
    }

    const char *target = argv[count - 1];
    struct stat info;
    int directory = stat(target, &info) == 0 && S_ISDIR(info.st_mode);
    if (count - first > 2 && !directory) {
        fprintf(stderr, "%s: destination must be a directory\n", argv[0]);
        return 1;
    }

    int result = 0;
    for (int i = first; i < count - 1; i++) {
        char *joined = NULL;
        if (directory) {
            joined = destination_in(target, argv[i]);
            if (!joined) {
                result = 1;
                continue;
            }
        }

        const char *destination = directory ? joined : target;
        if (move) {
            if (rename(argv[i], destination) == -1) {
                perror(argv[i]);
                result = 1;
            }
        } else if (copy_file(argv[i], destination)) {
            result = 1;
        }
        free(joined);
    }
    return result;
}

/* 경로 하나에 실패해도 나머지 경로는 계속 처리한다. */
static int change_paths(char *argv[]) {
    int first = argv[1] && !strcmp(argv[1], "--") ? 2 : 1;
    if (!argv[first]) {
        fprintf(stderr, "%s: path required\n", argv[0]);
        return 1;
    }

    int create = !strcmp(argv[0], "mkdir");
    int remove_dir = !strcmp(argv[0], "rmdir");
    int result = 0;
    for (int i = first; argv[i]; i++) {
        int status;
        if (create)
            status = mkdir(argv[i], 0755);
        else if (remove_dir)
            status = rmdir(argv[i]);
        else
            status = unlink(argv[i]);

        if (status == -1) {
            perror(argv[i]);
            result = 1;
        }
    }
    return result;
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

        if (argv[1] && argv[2]) {
            fprintf(stderr, "cd: too many arguments\n");
            return 1;
        }
        if (!path) {
            fprintf(stderr, "cd: HOME is not set\n");
            return 1;
        }
        if (chdir(path) == -1) {
            perror("cd");
            return 1;
        }

        return 0;
    }

    if (!strcmp(argv[0], "ls"))
        return run_ls(argv);

    if (!strcmp(argv[0], "mkdir") || !strcmp(argv[0], "rmdir") ||
        !strcmp(argv[0], "rm"))
        return change_paths(argv);

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

    if (!strcmp(argv[0], "mv"))
        return run_transfer(argv, 1);

    if (!strcmp(argv[0], "cp"))
        return run_transfer(argv, 0);

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

    if (!strcmp(argv[0], "grep"))
        return run_grep(argv);

    /* 직접 구현하지 않은 명령어는 외부 실행 대상으로 처리한다. */
    return -1;
}

/* 부모에서 실행하는 cd는 표준 입출력을 쓰지 않으므로 파일만 열고 닫는다. */
static int redirect_file(const char *path, int flags, int target_fd,
                         int connect_stream) {
    if (path == NULL)
        return 0;

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    if (connect_stream && fd == target_fd)
        return 0;

    if (connect_stream && dup2(fd, target_fd) == -1) {
        perror("dup2");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int apply_redirection(Command *cmd, int connect_stream) {
    if (redirect_file(cmd->input, O_RDONLY, STDIN_FILENO, connect_stream) < 0)
        return -1;

    return redirect_file(cmd->output, O_WRONLY | O_CREAT | O_TRUNC,
                         STDOUT_FILENO, connect_stream);
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

/* 파이프 생성 도중 실패하면 이 명령행에서 이미 시작한 자식들을 회수한다. */
static void cancel_pipeline(pid_t children[], int count) {
    for (int i = 0; i < count; i++)
        kill(children[i], SIGKILL);

    for (int i = 0; i < count; i++) {
        while (waitpid(children[i], NULL, 0) == -1 && errno == EINTR) {
        }
    }
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
    pid_t children[MAX_COMMANDS];

    for (int i = 0; i < count; i++) {
        int fd[2] = {-1, -1};

        /* 두 명령어 사이에 필요한 파이프를 생성한다. */
        if (i < count - 1 && pipe(fd) == -1) {
            perror("pipe");
            if (previous_read != -1)
                close(previous_read);
            cancel_pipeline(children, i);
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

            cancel_pipeline(children, i);
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
            if (apply_redirection(&cmds[i], 1) == -1)
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

        children[i] = pid;

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
    Token tokens[MAX_TOKENS];

    /* 자식의 표준 입력에 다음 셸 명령이 미리 읽히지 않도록 한다. */
    setvbuf(stdin, NULL, _IONBF, 0);
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

        size_t length = strlen(line);
        if (length && line[length - 1] == '\n') {
            line[length - 1] = '\0';
        } else if (length == sizeof(line) - 1) {
            int next = getchar();
            if (next != '\n' && next != EOF) {
                while ((next = getchar()) != '\n' && next != EOF) {
                }
                fprintf(stderr, "Input too long\n");
                continue;
            }
        }

        /* 입력한 문자열을 명령어와 인자로 분리한다. */
        int narg = getargs(line, work,
                           sizeof(work), tokens);

        if (narg < 0)
            continue;

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
            if (apply_redirection(&cmds[0], 0) == 0)
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
