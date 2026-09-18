#include "station_registry.h"

#include "gvs_station.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DF_STATION_CONFIG_MAX_BYTES 65536U
#define DF_STATION_LINE_MAX 512U
#define DF_STATION_ADDRESS_TEXT_MAX 18U
#define DF_STATION_IPV4_TEXT_MAX 16U

#define DF_STATION_SEEN_ENABLED (1U << 0)
#define DF_STATION_SEEN_NAME (1U << 1)
#define DF_STATION_SEEN_ADDRESS (1U << 2)
#define DF_STATION_SEEN_IPV4 (1U << 3)
#define DF_STATION_SEEN_ROUTE (1U << 4)
#define DF_STATION_SEEN_STREAM (1U << 5)
#define DF_STATION_REQUIRED (DF_STATION_SEEN_ENABLED | DF_STATION_SEEN_NAME | \
    DF_STATION_SEEN_ADDRESS | DF_STATION_SEEN_STREAM)

#define DF_LEGACY_SEEN_ADDRESS (1U << 0)
#define DF_LEGACY_SEEN_IPV4 (1U << 1)
#define DF_LEGACY_SEEN_STREAM (1U << 2)

enum df_station_section {
    DF_STATION_SECTION_OTHER = 0,
    DF_STATION_SECTION_MAIN,
    DF_STATION_SECTION_STATION,
};

struct df_station_legacy {
    char address[DF_STATION_ADDRESS_TEXT_MAX];
    char ipv4[DF_STATION_IPV4_TEXT_MAX];
    char stream_name[65];
    unsigned seen;
};

static const char *df_station_skip_space(const char *cursor) {
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static int df_station_parse_word(const char **cursor, char *output,
                                 size_t output_size) {
    const char *start = df_station_skip_space(*cursor);
    const char *end = start;
    size_t length;

    while (*end != '\0' && !isspace((unsigned char)*end)) end++;
    length = (size_t)(end - start);
    if (length == 0U || length >= output_size) return DF_ERR_INVALID;
    memcpy(output, start, length);
    output[length] = '\0';
    *cursor = end;
    return DF_OK;
}

static int df_station_parse_quoted(const char **cursor, char *output,
                                   size_t output_size) {
    const char *start = df_station_skip_space(*cursor);
    const char *end;
    char quote;
    size_t length;

    if (*start != '\'' && *start != '"') return DF_ERR_INVALID;
    quote = *start++;
    end = strchr(start, quote);
    if (end == NULL) return DF_ERR_INVALID;
    length = (size_t)(end - start);
    if (length >= output_size) return DF_ERR_INVALID;
    memcpy(output, start, length);
    output[length] = '\0';
    end = df_station_skip_space(end + 1);
    if (*end != '\0' && *end != '#') return DF_ERR_INVALID;
    *cursor = end;
    return DF_OK;
}

static int df_station_parse_directive(const char *line, const char *directive,
                                      char *name, size_t name_size,
                                      char *value, size_t value_size) {
    const char *cursor = df_station_skip_space(line);
    size_t directive_length = strlen(directive);

    if (strncmp(cursor, directive, directive_length) != 0 ||
        !isspace((unsigned char)cursor[directive_length]))
        return DF_ERR_INVALID;
    cursor += directive_length;
    if (df_station_parse_word(&cursor, name, name_size) != DF_OK ||
        df_station_parse_quoted(&cursor, value, value_size) != DF_OK)
        return DF_ERR_INVALID;
    return DF_OK;
}

static int df_station_next_line(const char **cursor, char line[DF_STATION_LINE_MAX]) {
    const char *line_end;
    size_t line_length;

    if (**cursor == '\0') return 0;
    line_end = strchr(*cursor, '\n');
    line_length = line_end == NULL ? strlen(*cursor) :
        (size_t)(line_end - *cursor);
    if (line_length >= DF_STATION_LINE_MAX) return -1;
    memcpy(line, *cursor, line_length);
    line[line_length] = '\0';
    *cursor = line_end == NULL ? *cursor + line_length : line_end + 1;
    return 1;
}

static bool df_station_id_is_valid(const char *id) {
    size_t index;
    size_t length;

    if (id == NULL) return false;
    length = strlen(id);
    if (length == 0U || length > 32U || id[0] < 'a' || id[0] > 'z')
        return false;
    for (index = 1U; index < length; index++) {
        if (!((id[index] >= 'a' && id[index] <= 'z') ||
              (id[index] >= '0' && id[index] <= '9') || id[index] == '_'))
            return false;
    }
    return true;
}

static bool df_station_text_is_valid(const char *text, size_t maximum_length,
                                     bool allow_space) {
    size_t index;
    size_t length;

    if (text == NULL) return false;
    length = strlen(text);
    if (length == 0U || length > maximum_length) return false;
    for (index = 0U; index < length; index++) {
        unsigned char value = (unsigned char)text[index];

        if (value < (allow_space ? 0x20U : 0x21U) || value > 0x7eU)
            return false;
        if (!allow_space && !(value == '_' || value == '-' ||
            (value >= '0' && value <= '9') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z'))) return false;
    }
    return true;
}

static int df_station_copy(char *destination, size_t destination_size,
                           const char *value) {
    size_t length = strlen(value);

    if (length >= destination_size) return DF_ERR_INVALID;
    memcpy(destination, value, length + 1U);
    return DF_OK;
}

static int df_station_claim(unsigned *seen, unsigned option) {
    if ((*seen & option) != 0U) return DF_ERR_INVALID;
    *seen |= option;
    return DF_OK;
}

static int df_station_parse_boolean(const char *value, bool *output) {
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

static int df_station_parse_ipv4(const char *value, uint32_t *output) {
    struct in_addr address;
    uint32_t host_value;
    unsigned first_octet;

    if (value[0] == '\0') {
        *output = 0U;
        return DF_OK;
    }
    if (inet_pton(AF_INET, value, &address) != 1) return DF_ERR_INVALID;
    host_value = ntohl(address.s_addr);
    first_octet = host_value >> 24U;
    if (host_value == 0U || host_value == 0xffffffffU || first_octet == 0U ||
        first_octet == 127U || first_octet >= 224U) return DF_ERR_INVALID;
    *output = address.s_addr;
    return DF_OK;
}

static int df_station_count_sections(const char *uci_text, size_t *count) {
    const char *cursor = uci_text;

    *count = 0U;
    while (*cursor != '\0') {
        char line[DF_STATION_LINE_MAX];
        char name[64];
        char value[256];
        const char *trimmed;
        int line_result = df_station_next_line(&cursor, line);

        if (line_result < 0) return DF_ERR_INVALID;
        trimmed = df_station_skip_space(line);
        if (*trimmed == '\0' || *trimmed == '#' ||
            strncmp(trimmed, "config", 6U) != 0 ||
            (trimmed[6] != '\0' && !isspace((unsigned char)trimmed[6])))
            continue;
        if (df_station_parse_directive(trimmed, "config", name, sizeof(name),
                                       value, sizeof(value)) != DF_OK)
            return DF_ERR_INVALID;
        if (strcmp(name, "station") == 0) {
            if (*count == SIZE_MAX) return DF_ERR_INVALID;
            (*count)++;
        }
    }
    if (*count > SIZE_MAX / sizeof(struct df_station)) return DF_ERR_INVALID;
    return DF_OK;
}

static int df_station_start(struct df_station_registry *registry,
                            size_t index, const char *id) {
    size_t previous;

    if (!df_station_id_is_valid(id)) return DF_ERR_INVALID;
    for (previous = 0U; previous < index; previous++) {
        if (strcmp(registry->items[previous].id, id) == 0)
            return DF_ERR_INVALID;
    }
    registry->items[index].route_preference =
        DF_STATION_ROUTE_DISCOVER_FIRST;
    return df_station_copy(registry->items[index].id,
                           sizeof(registry->items[index].id), id);
}

static int df_station_apply_option(struct df_station *station, const char *name,
                                   const char *value, unsigned *seen) {
    unsigned option;

    if (strcmp(name, "enabled") == 0) {
        option = DF_STATION_SEEN_ENABLED;
        if (df_station_claim(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_station_parse_boolean(value, &station->enabled);
    }
    if (strcmp(name, "name") == 0) {
        option = DF_STATION_SEEN_NAME;
        if (df_station_claim(seen, option) != DF_OK ||
            !df_station_text_is_valid(value, 64U, true)) return DF_ERR_INVALID;
        return df_station_copy(station->name, sizeof(station->name), value);
    }
    if (strcmp(name, "logical_address") == 0) {
        option = DF_STATION_SEEN_ADDRESS;
        if (df_station_claim(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_gvs_station_parse(value, station->logical_address);
    }
    if (strcmp(name, "ipv4") == 0) {
        option = DF_STATION_SEEN_IPV4;
        if (df_station_claim(seen, option) != DF_OK) return DF_ERR_INVALID;
        return df_station_parse_ipv4(value, &station->configured_ipv4);
    }
    if (strcmp(name, "route_preference") == 0) {
        option = DF_STATION_SEEN_ROUTE;
        if (df_station_claim(seen, option) != DF_OK) return DF_ERR_INVALID;
        if (strcmp(value, "discover_first") == 0)
            station->route_preference = DF_STATION_ROUTE_DISCOVER_FIRST;
        else if (strcmp(value, "fixed") == 0)
            station->route_preference = DF_STATION_ROUTE_FIXED;
        else return DF_ERR_INVALID;
        return DF_OK;
    }
    if (strcmp(name, "stream_name") == 0) {
        option = DF_STATION_SEEN_STREAM;
        if (df_station_claim(seen, option) != DF_OK ||
            !df_station_text_is_valid(value, 64U, false)) return DF_ERR_INVALID;
        return df_station_copy(station->stream_name,
                               sizeof(station->stream_name), value);
    }
    return DF_ERR_INVALID;
}

static int df_station_validate_complete(const struct df_station_registry *registry,
                                        size_t index, unsigned seen) {
    const struct df_station *station = &registry->items[index];
    size_t previous;

    if ((seen & DF_STATION_REQUIRED) != DF_STATION_REQUIRED ||
        (station->route_preference == DF_STATION_ROUTE_FIXED &&
         station->configured_ipv4 == 0U)) return DF_ERR_INVALID;
    for (previous = 0U; previous < index; previous++) {
        if (memcmp(registry->items[previous].logical_address,
                   station->logical_address,
                   sizeof(station->logical_address)) == 0 ||
            strcmp(registry->items[previous].stream_name,
                   station->stream_name) == 0) return DF_ERR_INVALID;
    }
    return DF_OK;
}

static int df_station_apply_legacy_option(struct df_station_legacy *legacy,
                                          const char *name,
                                          const char *value) {
    unsigned option;
    char *destination;
    size_t destination_size;

    if (strcmp(name, "media_station_address") == 0) {
        option = DF_LEGACY_SEEN_ADDRESS;
        destination = legacy->address;
        destination_size = sizeof(legacy->address);
    } else if (strcmp(name, "media_station_ipv4") == 0) {
        option = DF_LEGACY_SEEN_IPV4;
        destination = legacy->ipv4;
        destination_size = sizeof(legacy->ipv4);
    } else if (strcmp(name, "media_stream_name") == 0) {
        option = DF_LEGACY_SEEN_STREAM;
        destination = legacy->stream_name;
        destination_size = sizeof(legacy->stream_name);
    } else {
        return DF_OK;
    }
    if (df_station_claim(&legacy->seen, option) != DF_OK)
        return DF_ERR_INVALID;
    return df_station_copy(destination, destination_size, value);
}

static int df_station_build_legacy(struct df_station_registry *registry,
                                   const struct df_station_legacy *legacy) {
    struct df_station *station;

    if (legacy->address[0] == '\0') return DF_OK;
    registry->items = calloc(1U, sizeof(*registry->items));
    if (registry->items == NULL) return DF_ERR_IO;
    registry->count = 1U;
    station = registry->items;
    station->route_preference = DF_STATION_ROUTE_DISCOVER_FIRST;
    station->enabled = true;
    if (df_station_copy(station->id, sizeof(station->id), "legacy") != DF_OK ||
        df_station_copy(station->name, sizeof(station->name),
                        "Legacy Door Station") != DF_OK ||
        df_gvs_station_parse(legacy->address, station->logical_address) != DF_OK ||
        df_station_parse_ipv4(legacy->ipv4, &station->configured_ipv4) != DF_OK ||
        !df_station_text_is_valid(legacy->stream_name, 64U, false) ||
        df_station_copy(station->stream_name, sizeof(station->stream_name),
                        legacy->stream_name) != DF_OK) return DF_ERR_INVALID;
    return DF_OK;
}

static int df_station_parse_sections(struct df_station_registry *registry,
                                     const char *uci_text,
                                     size_t station_count) {
    const char *cursor = uci_text;
    struct df_station_legacy legacy = {0};
    enum df_station_section section = DF_STATION_SECTION_OTHER;
    size_t station_index = 0U;
    unsigned station_seen = 0U;
    bool station_open = false;

    (void)df_station_copy(legacy.stream_name, sizeof(legacy.stream_name),
                          "doorfast_preview");
    while (*cursor != '\0') {
        char line[DF_STATION_LINE_MAX];
        char name[64];
        char value[256];
        const char *trimmed;
        int line_result = df_station_next_line(&cursor, line);

        if (line_result < 0) return DF_ERR_INVALID;
        trimmed = df_station_skip_space(line);
        if (*trimmed == '\0' || *trimmed == '#') continue;
        if (strncmp(trimmed, "config", 6U) == 0 &&
            (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
            if (station_open && df_station_validate_complete(
                    registry, station_index - 1U, station_seen) != DF_OK)
                return DF_ERR_INVALID;
            station_open = false;
            station_seen = 0U;
            if (df_station_parse_directive(trimmed, "config", name,
                                           sizeof(name), value,
                                           sizeof(value)) != DF_OK)
                return DF_ERR_INVALID;
            if (strcmp(name, "station") == 0) {
                if (station_index >= station_count ||
                    df_station_start(registry, station_index, value) != DF_OK)
                    return DF_ERR_INVALID;
                station_index++;
                station_open = true;
                section = DF_STATION_SECTION_STATION;
            } else if (strcmp(name, "gvs") == 0 &&
                       strcmp(value, "main") == 0) {
                section = DF_STATION_SECTION_MAIN;
            } else {
                section = DF_STATION_SECTION_OTHER;
            }
            continue;
        }
        if (strncmp(trimmed, "option", 6U) == 0 &&
            (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
            if (df_station_parse_directive(trimmed, "option", name,
                                           sizeof(name), value,
                                           sizeof(value)) != DF_OK)
                return DF_ERR_INVALID;
            if (section == DF_STATION_SECTION_STATION) {
                if (df_station_apply_option(&registry->items[station_index - 1U],
                                            name, value,
                                            &station_seen) != DF_OK)
                    return DF_ERR_INVALID;
            } else if (section == DF_STATION_SECTION_MAIN &&
                       station_count == 0U &&
                       df_station_apply_legacy_option(&legacy, name, value) != DF_OK) {
                return DF_ERR_INVALID;
            }
        }
    }
    if (station_open && df_station_validate_complete(
            registry, station_index - 1U, station_seen) != DF_OK)
        return DF_ERR_INVALID;
    if (station_index != station_count) return DF_ERR_INVALID;
    if (station_count == 0U) return df_station_build_legacy(registry, &legacy);
    return DF_OK;
}

int df_station_registry_parse(struct df_station_registry *registry,
                              const char *uci_text) {
    struct df_station_registry parsed = {0};
    size_t station_count;
    int result;

    if (registry == NULL || uci_text == NULL) return DF_ERR_INVALID;
    result = df_station_count_sections(uci_text, &station_count);
    if (result != DF_OK) return result;
    if (station_count > 0U) {
        parsed.items = calloc(station_count, sizeof(*parsed.items));
        if (parsed.items == NULL) return DF_ERR_IO;
        parsed.count = station_count;
    }
    result = df_station_parse_sections(&parsed, uci_text, station_count);
    if (result != DF_OK) {
        df_station_registry_destroy(&parsed);
        return result;
    }
    parsed.revision = 1U;
    df_station_registry_destroy(registry);
    *registry = parsed;
    return DF_OK;
}

int df_station_registry_load(struct df_station_registry *registry,
                             const char *path) {
    FILE *file;
    char *contents;
    size_t length;
    int result;

    if (registry == NULL || path == NULL) return DF_ERR_INVALID;
    file = fopen(path, "rb");
    if (file == NULL) return DF_ERR_IO;
    contents = calloc(DF_STATION_CONFIG_MAX_BYTES + 1U, sizeof(*contents));
    if (contents == NULL) {
        (void)fclose(file);
        return DF_ERR_IO;
    }
    length = fread(contents, 1U, DF_STATION_CONFIG_MAX_BYTES, file);
    if (ferror(file) || (length == DF_STATION_CONFIG_MAX_BYTES &&
                         fgetc(file) != EOF)) {
        free(contents);
        (void)fclose(file);
        return DF_ERR_IO;
    }
    (void)fclose(file);
    contents[length] = '\0';
    result = df_station_registry_parse(registry, contents);
    free(contents);
    return result;
}

const struct df_station *df_station_registry_find(
    const struct df_station_registry *registry, const char *id) {
    size_t index;

    if (registry == NULL || id == NULL) return NULL;
    for (index = 0U; index < registry->count; index++) {
        if (strcmp(registry->items[index].id, id) == 0)
            return &registry->items[index];
    }
    return NULL;
}

void df_station_registry_destroy(struct df_station_registry *registry) {
    if (registry == NULL) return;
    free(registry->items);
    memset(registry, 0, sizeof(*registry));
}
