#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DF_MAX_LEGACY_CONFIG_BYTES 65536

static void df_print_usage(FILE *stream) {
    (void)fputs("Usage: doorfast --help | --import-legacy <path>\n", stream);
}

static int df_import_legacy_file(const char *path) {
    char *contents;
    FILE *file;
    size_t length;
    struct df_legacy_import imported;

    file = fopen(path, "rb");
    if (file == NULL) {
        (void)fprintf(stderr, "doorfast: cannot read legacy configuration: %s\n", path);
        return 2;
    }
    contents = calloc(DF_MAX_LEGACY_CONFIG_BYTES + 1, sizeof(*contents));
    if (contents == NULL) {
        (void)fclose(file);
        (void)fputs("doorfast: cannot allocate configuration buffer\n", stderr);
        return 2;
    }
    length = fread(contents, 1, DF_MAX_LEGACY_CONFIG_BYTES, file);
    if (ferror(file) || (length == DF_MAX_LEGACY_CONFIG_BYTES && fgetc(file) != EOF)) {
        free(contents);
        (void)fclose(file);
        (void)fputs("doorfast: legacy configuration is unreadable or too large\n", stderr);
        return 2;
    }
    (void)fclose(file);

    if (df_config_import_legacy(contents, &imported) != DF_OK) {
        free(contents);
        (void)fputs("doorfast: legacy configuration is not a supported Dnake configuration\n", stderr);
        return 2;
    }
    free(contents);

    (void)printf("config settings 'main'\n"
                 "\toption enabled '%d'\n"
                 "\toption brand '%s'\n"
                 "\toption mode 'transparent'\n"
                 "\toption capture_auto '1'\n"
                 "\toption capture_promiscuous '0'\n"
                 "\toption unlock '-1'\n"
                 "\toption hangup '-1'\n"
                 "\toption call_elev '0'\n",
                 imported.config.enabled ? 1 : 0,
                 imported.brand);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        df_print_usage(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--import-legacy") == 0) {
        return df_import_legacy_file(argv[2]);
    }
    df_print_usage(stderr);
    return 2;
}
