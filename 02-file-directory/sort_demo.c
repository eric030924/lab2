
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

int main(void) {
    struct dirent **entries;

    int count = scandir("sample", &entries, NULL, alphasort);

    if (count < 0) {
        perror("scandir");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        if (entries[i]->d_name[0] != '.') {
            printf("%s\n", entries[i]->d_name);
        }

        free(entries[i]);
    }

    free(entries);
    return 0;
}

