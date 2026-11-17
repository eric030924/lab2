
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

void list_dir(const char *path) {
    struct dirent **entries;

    int count = scandir(path, &entries, NULL, alphasort);

    if (count < 0) {
        perror("scandir");
        return;
    }

    printf("%s:\n", path);

    // First pass: print sorted entries
    for (int i = 0; i < count; i++) {
        if (entries[i]->d_name[0] == '.') {
            continue;
        }

        printf("%s\n", entries[i]->d_name);
    }

    // Second pass: visit subdirectories
    for (int i = 0; i < count; i++) {
        if (entries[i]->d_name[0] == '.') {
            continue;
        }

        char full_path[1024];

        int n = snprintf(full_path, sizeof(full_path),
                         "%s/%s", path, entries[i]->d_name);

        if (n < 0 || (size_t)n >= sizeof(full_path)) {
            fprintf(stderr, "Path too long\n");
            continue;
        }

        struct stat st;

        if (lstat(full_path, &st) == -1) {
            perror("lstat");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            printf("\n");
            list_dir(full_path);
        }
    }

    // Release allocated memory
    for (int i = 0; i < count; i++) {
        free(entries[i]);
    }

    free(entries);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    list_dir(argv[1]);

    return 0;
}

