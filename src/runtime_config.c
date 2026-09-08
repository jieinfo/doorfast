#include "runtime_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DF_RUNTIME_CONFIG_MAX_BYTES 65536
#define DF_RUNTIME_LINE_MAX 512

enum df_runtime_option {
    DF_SEEN_ENABLED = 1U << 0,
    DF_SEEN_BRAND = 1U << 1,
    DF_SEEN_GVS_INTERFACE = 1U << 2,
    DF_SEEN_GVS_ADDRESS = 1U << 3,
    DF_SEEN_UPLINK_INTERFACE = 1U << 4,
    DF_SEEN_PASSIVE_ONLY = 1U << 5,
    DF_SEEN_PROMISCUOUS = 1U << 6,
    DF_SEEN_CAPTURE_AUTO = 1U << 7,
    DF_SEEN_UNLOCK_DELAY = 1U << 8,
    DF_SEEN_HANGUP_DELAY = 1U << 9,
    DF_SEEN_CALL_ELEV = 1U << 10,
    DF_SEEN_SYNC_STATE_PATH = 1U << 11
};

static void df_runtime_config_defaults(struct df_runtime_config *runtime) {
    memset(runtime, 0, sizeof(*runtime));
    (void)snprintf(runtime->brand, sizeof(runtime->brand), "%s", "gvs");
    runtime->config.brand = runtime->brand;
    runtime->config.capture_interface = runtime->gvs_interface;
    runtime->config.gvs_interface = runtime->gvs_interface;
    runtime->config.gvs_local_address = runtime->gvs_local_address;
    runtime->config.uplink_interface = runtime->uplink_interface;
    runtime->config.sync_state_path = runtime->sync_state_path;
    (void)snprintf(runtime->sync_state_path, sizeof(runtime->sync_state_path),
                   "%s", "/etc/config/doorfast-sync");
    runtime->config.passive_only = true;
    runtime->config.unlock_delay_seconds = -1;
    runtime->config.hangup_delay_seconds = -1;
}

static const char *df_skip_space(const char *cursor) {
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

static int df_parse_word(const char **cursor, char *output, size_t output_size) {
    const char *start = df_skip_space(*cursor);
    const char *end = start;
    size_t length;

    while (*end != '\0' && !isspace((unsigned char)*end)) {
        end++;
    }
    length = (size_t)(end - start);
    if (length == 0 || length >= output_size) {
        return DF_ERR_INVALID;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    *cursor = end;
    return DF_OK;
}

static int df_parse_quoted(const char **cursor, char *output, size_t output_size) {
    const char *start = df_skip_space(*cursor);
    const char *end;
    char quote;
    size_t length;

    if (*start != '\'' && *start != '"') {
        return DF_ERR_INVALID;
    }
    quote = *start++;
    end = strchr(start, quote);
    if (end == NULL) {
        return DF_ERR_INVALID;
    }
    length = (size_t)(end - start);
    if (length >= output_size) {
        return DF_ERR_INVALID;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    end = df_skip_space(end + 1);
    if (*end != '\0' && *end != '#') {
        return DF_ERR_INVALID;
    }
    *cursor = end;
    return DF_OK;
}

static int df_parse_directive(const char *line, const char *directive,
                              char *name, size_t name_size,
                              char *value, size_t value_size) {
    const char *cursor = df_skip_space(line);
    size_t directive_length = strlen(directive);

    if (strncmp(cursor, directive, directive_length) != 0 ||
        !isspace((unsigned char)cursor[directive_length])) {
        return DF_ERR_INVALID;
    }
    cursor += directive_length;
    if (df_parse_word(&cursor, name, name_size) != DF_OK ||
        df_parse_quoted(&cursor, value, value_size) != DF_OK) {
        return DF_ERR_INVALID;
    }
    return DF_OK;
}

static int df_parse_boolean(const char *value, bool *output) {
    if (strcmp(value, "0") == 0) {
        *output = false;
        return DF_OK;
    }
    if (strcmp(value, "1") == 0) {
        *output = true;
        return DF_OK;
    }
    return DF_ERR_INVALID;
}

static int df_parse_delay(const char *value, int *output) {
    char *end = NULL;
    long parsed = strtol(value, &end, 10);

    if (end == value || *end != '\0' || parsed < -1 || parsed > 9) {
        return DF_ERR_INVALID;
    }
    *output = (int)parsed;
    return DF_OK;
}

static int df_copy_option(char *destination, size_t destination_size, const char *value) {
    size_t length = strlen(value);

    if (length >= destination_size) {
        return DF_ERR_INVALID;
    }
    memcpy(destination, value, length + 1);
    return DF_OK;
}

static int df_claim_option(unsigned int *seen, unsigned int option) {
    if ((*seen & option) != 0U) {
        return DF_ERR_INVALID;
    }
    *seen |= option;
    return DF_OK;
}

static int df_apply_option(struct df_runtime_config *runtime, const char *name,
                           const char *value, unsigned int *seen) {
    unsigned int option = 0;
    int result = DF_OK;

    if (strcmp(name, "enabled") == 0) {
        option = DF_SEEN_ENABLED;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.enabled);
    }
    if (strcmp(name, "brand") == 0) {
        option = DF_SEEN_BRAND;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->brand, sizeof(runtime->brand), value);
    }
    if (strcmp(name, "gvs_interface") == 0) {
        option = DF_SEEN_GVS_INTERFACE;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->gvs_interface, sizeof(runtime->gvs_interface), value);
    }
    if (strcmp(name, "gvs_local_address") == 0) {
        option = DF_SEEN_GVS_ADDRESS;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->gvs_local_address, sizeof(runtime->gvs_local_address), value);
    }
    if (strcmp(name, "uplink_interface") == 0) {
        option = DF_SEEN_UPLINK_INTERFACE;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->uplink_interface, sizeof(runtime->uplink_interface), value);
    }
    if (strcmp(name, "sync_state_path") == 0) {
        option = DF_SEEN_SYNC_STATE_PATH;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_copy_option(runtime->sync_state_path,
                              sizeof(runtime->sync_state_path), value);
    }
    if (strcmp(name, "passive_only") == 0) {
        option = DF_SEEN_PASSIVE_ONLY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.passive_only);
    }
    if (strcmp(name, "capture_promiscuous") == 0) {
        option = DF_SEEN_PROMISCUOUS;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.capture_promiscuous);
    }
    if (strcmp(name, "capture_auto") == 0) {
        option = DF_SEEN_CAPTURE_AUTO;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.capture_auto);
    }
    if (strcmp(name, "unlock") == 0 || strcmp(name, "unlock_delay_seconds") == 0) {
        option = DF_SEEN_UNLOCK_DELAY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_delay(value, &runtime->config.unlock_delay_seconds);
    }
    if (strcmp(name, "hangup") == 0 || strcmp(name, "hangup_delay_seconds") == 0) {
        option = DF_SEEN_HANGUP_DELAY;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_delay(value, &runtime->config.hangup_delay_seconds);
    }
    if (strcmp(name, "call_elev") == 0) {
        option = DF_SEEN_CALL_ELEV;
        if (df_claim_option(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_parse_boolean(value, &runtime->config.call_elev);
    }
    return result;
}

int df_runtime_config_parse(const char *uci_text, struct df_runtime_config *runtime) {
    const char *cursor;
    bool in_main = false;
    bool found_main = false;
    unsigned int seen = 0;

    if (uci_text == NULL || runtime == NULL) {
        return DF_ERR_INVALID;
    }
    df_runtime_config_defaults(runtime);
    cursor = uci_text;
    while (*cursor != '\0') {
        char line[DF_RUNTIME_LINE_MAX];
        char name[64];
        char value[128];
        const char *line_end = strchr(cursor, '\n');
        const char *trimmed;
        size_t line_length = line_end == NULL ? strlen(cursor) : (size_t)(line_end - cursor);

        if (line_length >= sizeof(line)) {
            return DF_ERR_INVALID;
        }
        memcpy(line, cursor, line_length);
        line[line_length] = '\0';
        cursor = line_end == NULL ? cursor + line_length : line_end + 1;
        trimmed = df_skip_space(line);
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        if (strncmp(trimmed, "config", 6) == 0 &&
            (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
            if (df_parse_directive(trimmed, "config", name, sizeof(name),
                                   value, sizeof(value)) != DF_OK) {
                return DF_ERR_INVALID;
            }
            in_main = strcmp(name, "gvs") == 0 && strcmp(value, "main") == 0;
            if (in_main) {
                if (found_main) return DF_ERR_INVALID;
                found_main = true;
            }
            continue;
        }
        if (strncmp(trimmed, "option", 6) == 0 &&
            (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
            if (df_parse_directive(trimmed, "option", name, sizeof(name),
                                   value, sizeof(value)) != DF_OK) {
                return DF_ERR_INVALID;
            }
            if (in_main && df_apply_option(runtime, name, value, &seen) != DF_OK) {
                return DF_ERR_INVALID;
            }
        }
    }
    if (!found_main) {
        return DF_ERR_INVALID;
    }
    return df_config_validate(&runtime->config);
}

int df_runtime_config_load(const char *path, struct df_runtime_config *runtime) {
    FILE *file;
    char *contents;
    size_t length;
    int result;

    if (path == NULL || runtime == NULL) {
        return DF_ERR_INVALID;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return DF_ERR_IO;
    }
    contents = calloc(DF_RUNTIME_CONFIG_MAX_BYTES + 1, sizeof(*contents));
    if (contents == NULL) {
        (void)fclose(file);
        return DF_ERR_IO;
    }
    length = fread(contents, 1, DF_RUNTIME_CONFIG_MAX_BYTES, file);
    if (ferror(file) ||
        (length == DF_RUNTIME_CONFIG_MAX_BYTES && fgetc(file) != EOF)) {
        free(contents);
        (void)fclose(file);
        return DF_ERR_IO;
    }
    (void)fclose(file);
    contents[length] = '\0';
    result = df_runtime_config_parse(contents, runtime);
    free(contents);
    return result;
}
