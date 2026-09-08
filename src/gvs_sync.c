#include "gvs_sync.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct df_gvs_json_writer {
    uint8_t *data;
    size_t capacity;
    size_t length;
    bool failed;
};

struct df_gvs_json_parser {
    const uint8_t *data;
    size_t length;
    size_t offset;
};

static void df_gvs_json_bytes(struct df_gvs_json_writer *writer,
                              const char *text, size_t length) {
    if (writer->failed || length > writer->capacity - writer->length) {
        writer->failed = true;
        return;
    }
    memcpy(writer->data + writer->length, text, length);
    writer->length += length;
}

static void df_gvs_json_text(struct df_gvs_json_writer *writer,
                             const char *text) {
    df_gvs_json_bytes(writer, text, strlen(text));
}

static void df_gvs_json_string(struct df_gvs_json_writer *writer,
                               const char *text) {
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *cursor = (const unsigned char *)text;

    df_gvs_json_text(writer, "\"");
    while (!writer->failed && *cursor != '\0') {
        char escaped[6];

        if (*cursor > 0x7fU) {
            writer->failed = true;
        } else if (*cursor == '"' || *cursor == '\\') {
            char pair[2] = {'\\', (char)*cursor};
            df_gvs_json_bytes(writer, pair, sizeof(pair));
        } else if (*cursor < 0x20U) {
            memcpy(escaped, "\\u00", 4);
            escaped[4] = hex[*cursor >> 4U];
            escaped[5] = hex[*cursor & 0x0fU];
            df_gvs_json_bytes(writer, escaped, sizeof(escaped));
        } else {
            df_gvs_json_bytes(writer, (const char *)cursor, 1);
        }
        cursor++;
    }
    df_gvs_json_text(writer, "\"");
}

static int df_gvs_sync_json(const struct df_gvs_sync_entry *entries,
                            size_t count, const char *type, uint8_t *output,
                            size_t capacity, size_t *output_length) {
    struct df_gvs_json_writer writer = {
        .data = output,
        .capacity = capacity,
    };
    char count_text[24];
    size_t index;
    int count_length;

    if (output == NULL || output_length == NULL || type == NULL ||
        (count > 0U && entries == NULL) ||
        count > DF_GVS_SYNC_CHUNK_ENTRIES) {
        return DF_ERR_INVALID;
    }
    *output_length = 0;
    count_length = snprintf(count_text, sizeof(count_text), "%zu", count);
    if (count_length < 1 || (size_t)count_length >= sizeof(count_text)) {
        return DF_ERR_INVALID;
    }
    df_gvs_json_text(&writer, "{\"TYPE\":");
    df_gvs_json_string(&writer, type);
    df_gvs_json_text(&writer, ",\"COUNT\":");
    df_gvs_json_bytes(&writer, count_text, (size_t)count_length);
    df_gvs_json_text(&writer, ",\"INFO\":[");
    for (index = 0; index < count; index++) {
        if (index > 0U) {
            df_gvs_json_text(&writer, ",");
        }
        df_gvs_json_text(&writer, "{\"KEY\":");
        df_gvs_json_string(&writer, entries[index].key);
        df_gvs_json_text(&writer, ",\"VALUE\":");
        df_gvs_json_string(&writer, entries[index].value);
        df_gvs_json_text(&writer, "}");
    }
    df_gvs_json_text(&writer, "]}");
    if (writer.failed) {
        return DF_ERR_INVALID;
    }
    *output_length = writer.length;
    return DF_OK;
}

static const struct df_gvs_sync_entry *df_gvs_sync_find(
    const struct df_gvs_sync_store *store, const char *key) {
    size_t index;

    if (store == NULL || key == NULL) {
        return NULL;
    }
    for (index = 0; index < store->count; index++) {
        if (strcmp(store->entries[index].key, key) == 0) {
            return &store->entries[index];
        }
    }
    return NULL;
}

static int df_gvs_sync_copy_text(char *output, size_t capacity,
                                 const char *input) {
    size_t length;

    if (output == NULL || input == NULL) {
        return DF_ERR_INVALID;
    }
    length = strlen(input);
    if (length >= capacity) {
        return DF_ERR_INVALID;
    }
    memcpy(output, input, length + 1U);
    return DF_OK;
}

static int df_gvs_sync_frame(const struct df_gvs_sync_entry *entries,
                             size_t count, const char *type,
                             const uint8_t destination[6],
                             const uint8_t source[6], uint16_t version,
                             uint8_t *output, size_t capacity,
                             size_t *output_length,
                             df_gvs_header_fields_fn provide_fields,
                             void *fields_context) {
    uint8_t payload[DF_GVS_SYNC_MAX_PACKET_SIZE -
                    DF_GVS_CONTROL_HEADER_SIZE];
    size_t json_length;

    if (output_length != NULL) {
        *output_length = 0;
    }
    payload[0] = (uint8_t)(version & 0xffU);
    payload[1] = (uint8_t)(version >> 8U);
    if (df_gvs_sync_json(entries, count, type, payload + 2,
                         sizeof(payload) - 2U, &json_length) != DF_OK ||
        json_length > UINT16_MAX - 2U) {
        return DF_ERR_INVALID;
    }
    return df_gvs_control_serialize(
        output, capacity, output_length, destination, source, 0x91, 0x03,
        payload, (uint16_t)(json_length + 2U), provide_fields,
        fields_context);
}

static void df_gvs_json_skip_space(struct df_gvs_json_parser *parser) {
    while (parser->offset < parser->length &&
           (parser->data[parser->offset] == ' ' ||
            parser->data[parser->offset] == '\t' ||
            parser->data[parser->offset] == '\r' ||
            parser->data[parser->offset] == '\n')) {
        parser->offset++;
    }
}

static int df_gvs_json_expect(struct df_gvs_json_parser *parser,
                              uint8_t expected) {
    df_gvs_json_skip_space(parser);
    if (parser->offset >= parser->length ||
        parser->data[parser->offset] != expected) {
        return DF_ERR_INVALID;
    }
    parser->offset++;
    return DF_OK;
}

static int df_gvs_json_hex(uint8_t value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int df_gvs_json_parse_string(struct df_gvs_json_parser *parser,
                                    char *output, size_t capacity) {
    size_t output_length = 0;

    if (output == NULL || capacity == 0U ||
        df_gvs_json_expect(parser, '"') != DF_OK) {
        return DF_ERR_INVALID;
    }
    while (parser->offset < parser->length) {
        uint8_t value = parser->data[parser->offset++];

        if (value == '"') {
            output[output_length] = '\0';
            return DF_OK;
        }
        if (value == '\\') {
            if (parser->offset >= parser->length) {
                return DF_ERR_INVALID;
            }
            value = parser->data[parser->offset++];
            if (value == 'b') value = '\b';
            else if (value == 'f') value = '\f';
            else if (value == 'n') value = '\n';
            else if (value == 'r') value = '\r';
            else if (value == 't') value = '\t';
            else if (value == 'u') {
                int high;
                int low;

                if (parser->length - parser->offset < 4U ||
                    parser->data[parser->offset] != '0' ||
                    parser->data[parser->offset + 1U] != '0' ||
                    (high = df_gvs_json_hex(
                         parser->data[parser->offset + 2U])) < 0 ||
                    (low = df_gvs_json_hex(
                         parser->data[parser->offset + 3U])) < 0) {
                    return DF_ERR_INVALID;
                }
                value = (uint8_t)((high << 4) | low);
                parser->offset += 4U;
            } else if (value != '"' && value != '\\' && value != '/') {
                return DF_ERR_INVALID;
            }
        } else if (value < 0x20U || value > 0x7fU) {
            return DF_ERR_INVALID;
        }
        if (value == '\0' || output_length + 1U >= capacity) {
            return DF_ERR_INVALID;
        }
        output[output_length++] = (char)value;
    }
    return DF_ERR_INVALID;
}

static int df_gvs_json_member_name(struct df_gvs_json_parser *parser,
                                   const char *expected) {
    char name[16];

    return df_gvs_json_parse_string(parser, name, sizeof(name)) == DF_OK &&
                   strcmp(name, expected) == 0 &&
                   df_gvs_json_expect(parser, ':') == DF_OK
               ? DF_OK
               : DF_ERR_INVALID;
}

static int df_gvs_json_count(struct df_gvs_json_parser *parser,
                             size_t *count) {
    size_t value = 0;
    size_t digits = 0;

    df_gvs_json_skip_space(parser);
    while (parser->offset < parser->length &&
           parser->data[parser->offset] >= '0' &&
           parser->data[parser->offset] <= '9') {
        value = value * 10U + (size_t)(parser->data[parser->offset] - '0');
        parser->offset++;
        digits++;
        if (value > DF_GVS_SYNC_CHUNK_ENTRIES) {
            return DF_ERR_INVALID;
        }
    }
    if (digits == 0U) {
        return DF_ERR_INVALID;
    }
    *count = value;
    return DF_OK;
}

static int df_gvs_sync_parse_json(const uint8_t *data, size_t length,
                                  enum df_gvs_sync_data_type *type,
                                  struct df_gvs_sync_entry entries[
                                      DF_GVS_SYNC_CHUNK_ENTRIES],
                                  size_t *entry_count) {
    struct df_gvs_json_parser parser = {.data = data, .length = length};
    char type_name[8];
    size_t declared_count;
    size_t index;

    if (data == NULL || type == NULL || entries == NULL ||
        entry_count == NULL ||
        df_gvs_json_expect(&parser, '{') != DF_OK ||
        df_gvs_json_member_name(&parser, "TYPE") != DF_OK ||
        df_gvs_json_parse_string(&parser, type_name, sizeof(type_name)) !=
            DF_OK ||
        df_gvs_json_expect(&parser, ',') != DF_OK ||
        df_gvs_json_member_name(&parser, "COUNT") != DF_OK ||
        df_gvs_json_count(&parser, &declared_count) != DF_OK ||
        df_gvs_json_expect(&parser, ',') != DF_OK ||
        df_gvs_json_member_name(&parser, "INFO") != DF_OK ||
        df_gvs_json_expect(&parser, '[') != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (strcmp(type_name, "Normal") == 0) {
        *type = DF_GVS_SYNC_DATA_NORMAL;
    } else if (strcmp(type_name, "Period") == 0) {
        *type = DF_GVS_SYNC_DATA_PERIOD;
    } else {
        return DF_ERR_INVALID;
    }
    memset(entries, 0,
           sizeof(struct df_gvs_sync_entry) * DF_GVS_SYNC_CHUNK_ENTRIES);
    for (index = 0; index < declared_count; index++) {
        if ((index > 0U && df_gvs_json_expect(&parser, ',') != DF_OK) ||
            df_gvs_json_expect(&parser, '{') != DF_OK ||
            df_gvs_json_member_name(&parser, "KEY") != DF_OK ||
            df_gvs_json_parse_string(&parser, entries[index].key,
                                     sizeof(entries[index].key)) != DF_OK ||
            entries[index].key[0] == '\0' ||
            df_gvs_json_expect(&parser, ',') != DF_OK ||
            df_gvs_json_member_name(&parser, "VALUE") != DF_OK ||
            df_gvs_json_parse_string(&parser, entries[index].value,
                                     sizeof(entries[index].value)) != DF_OK ||
            df_gvs_json_expect(&parser, '}') != DF_OK) {
            return DF_ERR_INVALID;
        }
    }
    if (df_gvs_json_expect(&parser, ']') != DF_OK ||
        df_gvs_json_expect(&parser, '}') != DF_OK) {
        return DF_ERR_INVALID;
    }
    df_gvs_json_skip_space(&parser);
    if (parser.offset != parser.length ||
        (*type == DF_GVS_SYNC_DATA_NORMAL && declared_count != 1U)) {
        return DF_ERR_INVALID;
    }
    *entry_count = declared_count;
    return DF_OK;
}

void df_gvs_sync_store_init(struct df_gvs_sync_store *store) {
    if (store != NULL) {
        memset(store, 0, sizeof(*store));
    }
}

int df_gvs_sync_store_register(struct df_gvs_sync_store *store,
                               const char *key, const char *initial_value) {
    struct df_gvs_sync_entry *entry;

    if (store == NULL || key == NULL || initial_value == NULL || key[0] == '\0') {
        return DF_ERR_INVALID;
    }
    if (df_gvs_sync_find(store, key) != NULL) {
        return DF_OK;
    }
    if (store->count >= DF_GVS_SYNC_MAX_ENTRIES) {
        return DF_ERR_INVALID;
    }
    entry = &store->entries[store->count];
    if (df_gvs_sync_copy_text(entry->key, sizeof(entry->key), key) != DF_OK ||
        df_gvs_sync_copy_text(entry->value, sizeof(entry->value),
                              initial_value) != DF_OK) {
        memset(entry, 0, sizeof(*entry));
        return DF_ERR_INVALID;
    }
    store->count++;
    return DF_OK;
}

int df_gvs_sync_store_update_local(struct df_gvs_sync_store *store,
                                   uint16_t *version, const char *key,
                                   const char *value) {
    struct df_gvs_sync_entry *entry;
    size_t index;
    char next_value[DF_GVS_SYNC_VALUE_SIZE] = {0};

    if (store == NULL || version == NULL || key == NULL || value == NULL ||
        df_gvs_sync_copy_text(next_value, sizeof(next_value), value) != DF_OK) {
        return DF_ERR_INVALID;
    }
    entry = NULL;
    for (index = 0; index < store->count; index++) {
        if (strcmp(store->entries[index].key, key) == 0) {
            entry = &store->entries[index];
            break;
        }
    }
    if (entry == NULL) {
        return DF_ERR_INVALID;
    }
    memcpy(entry->value, next_value, sizeof(next_value));
    *version = *version >= 60000U ? 1U : (uint16_t)(*version + 1U);
    return DF_OK;
}

size_t df_gvs_sync_periodic_chunk_count(const struct df_gvs_sync_store *store) {
    if (store == NULL || store->count == 0U) {
        return 0U;
    }
    return (store->count + DF_GVS_SYNC_CHUNK_ENTRIES - 1U) /
           DF_GVS_SYNC_CHUNK_ENTRIES;
}

int df_gvs_sync_periodic_serialize(
    const struct df_gvs_sync_store *store, size_t chunk_index,
    const struct df_gvs_presence_action *action, const uint8_t source[6],
    uint16_t version, uint8_t *output, size_t capacity,
    size_t *output_length, df_gvs_header_fields_fn provide_fields,
    void *fields_context) {
    size_t offset;
    size_t count;

    if (output_length != NULL) {
        *output_length = 0;
    }
    if (store == NULL || action == NULL || source == NULL ||
        action->type != DF_GVS_PRESENCE_PERIODIC_SYNC ||
        chunk_index >= df_gvs_sync_periodic_chunk_count(store)) {
        return DF_ERR_INVALID;
    }
    offset = chunk_index * DF_GVS_SYNC_CHUNK_ENTRIES;
    count = store->count - offset;
    if (count > DF_GVS_SYNC_CHUNK_ENTRIES) {
        count = DF_GVS_SYNC_CHUNK_ENTRIES;
    }
    return df_gvs_sync_frame(store->entries + offset, count, "Period",
                             action->target, source, version, output, capacity,
                             output_length, provide_fields, fields_context);
}

int df_gvs_sync_normal_serialize(
    const struct df_gvs_sync_store *store, const char *key,
    const uint8_t destination[6], const uint8_t source[6], uint16_t version,
    uint8_t *output, size_t capacity, size_t *output_length,
    df_gvs_header_fields_fn provide_fields, void *fields_context) {
    const struct df_gvs_sync_entry *entry = df_gvs_sync_find(store, key);

    if (output_length != NULL) {
        *output_length = 0;
    }
    if (entry == NULL) {
        return DF_ERR_INVALID;
    }
    return df_gvs_sync_frame(entry, 1, "Normal", destination, source,
                             version, output, capacity, output_length,
                             provide_fields, fields_context);
}

int df_gvs_sync_receive(struct df_gvs_sync_store *store,
                        struct df_gvs_presence *presence,
                        const uint8_t *data, size_t length, uint64_t now_ms,
                        bool *resend_local) {
    struct df_gvs_sync_entry received[DF_GVS_SYNC_CHUNK_ENTRIES];
    struct df_gvs_sync_store next_store;
    struct df_gvs_presence next_presence;
    struct df_gvs_frame frame;
    struct df_event event;
    enum df_gvs_sync_data_type type;
    uint16_t remote_version;
    size_t received_count;
    size_t index;
    bool apply_values;
    bool resend;

    if (resend_local != NULL) {
        *resend_local = false;
    }
    if (store == NULL || presence == NULL || data == NULL ||
        resend_local == NULL ||
        df_gvs_frame_parse(data, length, &frame, &event) != DF_OK ||
        frame.family != 0x91 || frame.opcode != 0x03 ||
        frame.payload_length < 2U ||
        memcmp(frame.destination, presence->identity, 6) != 0 ||
        df_gvs_sync_parse_json(frame.payload + 2,
                               frame.payload_length - 2U, &type, received,
                               &received_count) != DF_OK) {
        return DF_ERR_INVALID;
    }
    remote_version = (uint16_t)(frame.payload[0] |
                                ((uint16_t)frame.payload[1] << 8U));
    next_store = *store;
    next_presence = *presence;
    if (df_gvs_presence_observe_sync_data(
            &next_presence, frame.source, type, remote_version, now_ms,
            &apply_values, &resend) != DF_OK) {
        return DF_ERR_INVALID;
    }
    if (apply_values) {
        for (index = 0; index < received_count; index++) {
            struct df_gvs_sync_entry *local = NULL;
            size_t local_index;

            for (local_index = 0; local_index < next_store.count;
                 local_index++) {
                if (strcmp(next_store.entries[local_index].key,
                           received[index].key) == 0) {
                    local = &next_store.entries[local_index];
                    break;
                }
            }
            if (local != NULL &&
                (type == DF_GVS_SYNC_DATA_NORMAL ||
                 strcmp(local->value, received[index].value) != 0)) {
                memcpy(local->value, received[index].value,
                       sizeof(local->value));
            }
        }
    }
    *store = next_store;
    *presence = next_presence;
    *resend_local = resend;
    return DF_OK;
}
