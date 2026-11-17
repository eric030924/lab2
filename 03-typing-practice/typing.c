
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>

int main(void) {
    const char *sentences[] = {
        "Linux is an open source operating system.",
        "A process can communicate through a pipe.",
        "Practice makes programming easier."
    };

    char input[512];
    size_t total_errors = 0;
    size_t total_typed = 0;
    size_t correct_chars = 0;
    double total_time = 0.0;

    printf("=== Typing Practice ===\n\n");

    for (int s = 0; s < 3; s++) {
        struct timespec start, end;

        printf("Sentence %d:\n%s\n", s + 1, sentences[s]);
        printf("Type: ");
        fflush(stdout);

        clock_gettime(CLOCK_MONOTONIC, &start);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            fprintf(stderr, "Input error\n");
            return 1;
        }

        clock_gettime(CLOCK_MONOTONIC, &end);

        total_time += (end.tv_sec - start.tv_sec)
                    + (end.tv_nsec - start.tv_nsec) / 1000000000.0;

        input[strcspn(input, "\n")] = '\0';

        size_t expected = strlen(sentences[s]);
        size_t actual = strlen(input);
        size_t length = expected > actual ? expected : actual;
        size_t errors = 0;

        for (size_t i = 0; i < length; i++) {
            if (i < expected && i < actual &&
                sentences[s][i] == input[i]) {
                correct_chars++;
            } else {
                errors++;
            }
        }

        total_errors += errors;
        total_typed += actual;

        printf("Errors: %zu\n\n", errors);
    }

    if (total_time < 0.001) {
        total_time = 0.001;
    }

    double cpm = total_typed * 60.0 / total_time;
    double wpm = (correct_chars / 5.0) * 60.0 / total_time;

    printf("=== Result ===\n");
    printf("Total errors: %zu\n", total_errors);
    printf("Total time: %.2f seconds\n", total_time);
    printf("Typing speed: %.2f CPM\n", cpm);
    printf("Corrected speed: %.2f WPM\n", wpm);

    return 0;
}

