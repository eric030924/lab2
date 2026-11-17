
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SHM_NAME "/lab2_file_copy"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <source> <destination>\n",
               argv[0]);
        return 1;
    }

    if (strcmp(argv[1], argv[2]) == 0) {
        fprintf(stderr, "Source and destination must differ.\n");
        return 1;
    }

    FILE *source = NULL;
    FILE *destination = NULL;
    int shm_fd = -1;
    void *shared = MAP_FAILED;
    size_t size = 0;
    int result = 1;
    int shm_created = 0;

    source = fopen(argv[1], "rb");

    if (source == NULL) {
        perror("fopen source");
        goto cleanup;
    }

    if (fseek(source, 0, SEEK_END) != 0) {
        perror("fseek");
        goto cleanup;
    }

    long length = ftell(source);

    if (length < 0) {
        perror("ftell");
        goto cleanup;
    }

    rewind(source);
    size = (size_t)length;

    destination = fopen(argv[2], "wb");

    if (destination == NULL) {
        perror("fopen destination");
        goto cleanup;
    }

    if (size == 0) {
        printf("Copied 0 bytes.\n");
        result = 0;
        goto cleanup;
    }

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);

    if (shm_fd == -1) {
        perror("shm_open");
        goto cleanup;
    }

    shm_created = 1;

    if (ftruncate(shm_fd, (off_t)size) == -1) {
        perror("ftruncate");
        goto cleanup;
    }

    shared = mmap(NULL, size,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED, shm_fd, 0);

    if (shared == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }

    if (fread(shared, 1, size, source) != size) {
        fprintf(stderr, "Read error\n");
        goto cleanup;
    }

    if (fwrite(shared, 1, size, destination) != size) {
        fprintf(stderr, "Write error\n");
        goto cleanup;
    }

    if (fflush(destination) != 0) {
        perror("fflush");
        goto cleanup;
    }

    printf("Source: %s\n", argv[1]);
    printf("Destination: %s\n", argv[2]);
    printf("Copied %zu bytes using POSIX shared memory.\n",
           size);

    result = 0;

cleanup:
    if (shared != MAP_FAILED) {
        munmap(shared, size);
    }

    if (shm_fd != -1) {
        close(shm_fd);
    }

    if (shm_created) {
        shm_unlink(SHM_NAME);
    }

    if (source != NULL) {
        fclose(source);
    }

    if (destination != NULL && fclose(destination) != 0) {
        perror("fclose destination");
        result = 1;
    }

    return result;
}

