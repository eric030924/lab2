#include <stdio.h>
#include <dirent.h>
#include <string.h>

int main(void) {
    DIR *dir = opendir("sample");

    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {

    if (strcmp(entry->d_name, ".") == 0 ||
        strcmp(entry->d_name, "..") == 0) {
        continue;
    }

    if (entry->d_type == DT_DIR) {
        printf("[DIR] %s\n", entry->d_name);
    } else {
        printf("[FILE] %s\n", entry->d_name);
    }
}

    closedir(dir);
    return 0;
}
