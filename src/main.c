#include <stdio.h>
#include <string.h>

static void df_print_usage(FILE *stream) {
    (void)fputs("Usage: doorfast --help\n", stream);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        df_print_usage(stdout);
        return 0;
    }
    df_print_usage(stderr);
    return 2;
}
