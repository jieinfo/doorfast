#include "sip.h"

#include <ctype.h>
#include <string.h>

enum { DF_SIP_MAX_MESSAGE_SIZE = 65535 };

static int df_ascii_equal(char left, char right) {
    return tolower((unsigned char)left) == tolower((unsigned char)right);
}

static int df_header_name_is_call_id(const uint8_t *line, size_t length) {
    static const char name[] = "Call-ID:";
    size_t index;

    if (length < sizeof(name) - 1) {
        return 0;
    }
    for (index = 0; index < sizeof(name) - 1; index++) {
        if (!df_ascii_equal((char)line[index], name[index])) {
            return 0;
        }
    }
    return 1;
}

static int df_read_line(const uint8_t *data, size_t length, size_t *offset,
                        const uint8_t **line, size_t *line_length) {
    size_t index;

    if (*offset >= length) {
        return 0;
    }
    *line = data + *offset;
    for (index = *offset; index + 1 < length; index++) {
        if (data[index] == '\r' && data[index + 1] == '\n') {
            *line_length = index - *offset;
            *offset = index + 2;
            return 1;
        }
    }
    return -1;
}

int df_sip_parse(const uint8_t *data, size_t length, struct df_event *event) {
    const uint8_t *line;
    size_t line_length;
    size_t offset = 0;
    size_t value_offset;
    size_t value_length;

    if (data == NULL || event == NULL || length == 0 || length > DF_SIP_MAX_MESSAGE_SIZE) {
        return DF_ERR_INVALID;
    }
    memset(event, 0, sizeof(*event));
    if (df_read_line(data, length, &offset, &line, &line_length) != 1) {
        return DF_ERR_INVALID;
    }
    if (line_length >= 7 && memcmp(line, "INVITE ", 7) == 0) {
        event->type = DF_EVENT_INCOMING_CALL;
    } else if (line_length >= 4 && memcmp(line, "BYE ", 4) == 0) {
        event->type = DF_EVENT_HANGUP;
    } else {
        event->type = DF_EVENT_UNKNOWN;
    }
    while (df_read_line(data, length, &offset, &line, &line_length) == 1) {
        if (line_length == 0) {
            break;
        }
        if (!df_header_name_is_call_id(line, line_length)) {
            continue;
        }
        value_offset = sizeof("Call-ID:") - 1;
        while (value_offset < line_length && (line[value_offset] == ' ' || line[value_offset] == '\t')) {
            value_offset++;
        }
        value_length = line_length - value_offset;
        if (value_length == 0 || value_length >= sizeof(event->call_id)) {
            return DF_ERR_INVALID;
        }
        memcpy(event->call_id, line + value_offset, value_length);
        event->call_id[value_length] = '\0';
        return DF_OK;
    }
    return DF_ERR_INVALID;
}
