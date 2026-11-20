#define _POSIX_C_SOURCE 200809L

#include "commands.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* 임채민 담당: cat, grep, pwd */

/* 임채민 담당: grep의 옵션 해석과 파일·표준 입력의 줄 단위 검색 */
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

int cmd_grep(char *argv[]) {
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

int cmd_cat(char *argv[]) {
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

int cmd_pwd(char *argv[]) {
    (void)argv;
    char path[4096];

    if (getcwd(path, sizeof(path)) == NULL) {
        perror("pwd");
        return 1;
    }

    printf("%s\n", path);
    return 0;
}
