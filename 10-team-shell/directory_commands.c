#define _POSIX_C_SOURCE 200809L

#include "commands.h"
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* 박재석 담당: ls, mkdir, rmdir, rm */

/* 박재석 담당: ls의 파일 종류·권한 표시, 디렉터리 조회 및 옵션 처리 */
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

int cmd_ls(char *argv[]) {
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

/* 박재석 담당: mkdir·rmdir·rm은 한 경로에서 실패해도 나머지 경로를 계속 처리한다. */
int change_paths(char *argv[]) {
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
