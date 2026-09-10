#include "deployment_config.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

static int valid_name(const char *name) {
    size_t n = strnlen(name, DF_DEPLOYMENT_IFNAME_MAX);
    if (n == DF_DEPLOYMENT_IFNAME_MAX || !strcmp(name, ".") ||
        !strcmp(name, "..")) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!isalnum((unsigned char)name[i]) && name[i] != '-' &&
            name[i] != '_' && name[i] != '.') return 0;
    return 1;
}

int df_deployment_config_validate(const struct df_deployment_config *c) {
    if (!c) return DF_ERR_INVALID;
    const char *names[] = {c->bridge, c->upstream, c->downstream, c->management};
    if (strnlen(c->evidence_root, sizeof(c->evidence_root)) == sizeof(c->evidence_root) ||
        strcmp(c->evidence_root, "/mnt/doorfast") ||
        (uint64_t)c->recent_budget_mib + c->control_budget_mib + c->log_budget_mib > 23552 ||
        c->reserve_mib < 6144 || (c->recording_enabled && !c->enabled))
        return DF_ERR_INVALID;
    for (size_t i = 0; i < 4; ++i) {
        if (!valid_name(names[i]) || (c->enabled && !names[i][0])) return DF_ERR_INVALID;
        for (size_t j = 0; j < i; ++j)
            if (names[i][0] && !strcmp(names[i], names[j])) return DF_ERR_INVALID;
    }
    if (!valid_name(c->observation) || (c->enabled && !c->observation[0]) ||
        (c->observation[0] && strcmp(c->observation, c->bridge) &&
         strcmp(c->observation, c->upstream) &&
         strcmp(c->observation, c->downstream))) return DF_ERR_INVALID;
    return DF_OK;
}

static void skip(const char **p) {
    while (isspace((unsigned char)**p)) ++*p;
}

static int token(const char **p, char *out, size_t size) {
    size_t n = 0;
    skip(p);
    char quote = (**p == '\'' || **p == '"') ? *(*p)++ : 0;
    while (**p && (quote ? **p != quote : !isspace((unsigned char)**p))) {
        if (n + 1 >= size || **p == '\\') return DF_ERR_INVALID;
        out[n++] = *(*p)++;
    }
    if (quote) {
        if (**p != quote) return DF_ERR_INVALID;
        ++*p;
        if (**p && !isspace((unsigned char)**p)) return DF_ERR_INVALID;
    } else if (!n) return DF_ERR_INVALID;
    out[n] = 0;
    return DF_OK;
}

int df_deployment_config_parse(const char *input, struct df_deployment_config *out) {
    static const char *keys[] = {"enabled", "recording_enabled", "bridge", "upstream",
        "downstream", "management", "evidence_root", "recent_budget_mib",
        "control_budget_mib", "log_budget_mib", "reserve_mib", "observation"};
    struct df_deployment_config c = {.evidence_root = "/mnt/doorfast",
        .recent_budget_mib = 14336, .control_budget_mib = 8192,
        .log_budget_mib = 1024, .reserve_mib = 6144};
    unsigned seen = 0;
    bool section = false;
    if (!input || !out || strnlen(input, 65537) > 65536) return DF_ERR_INVALID;
    while (*input) {
        char line[512], command[32], name[64], value[256];
        size_t n = strcspn(input, "\n");
        if (n >= sizeof(line)) return DF_ERR_INVALID;
        memcpy(line, input, n); line[n] = 0;
        input += n;
        if (*input) ++input;
        const char *p = line;
        skip(&p);
        if (!*p || *p == '#') continue;
        if (token(&p, command, sizeof(command)) || token(&p, name, sizeof(name)) ||
            token(&p, value, sizeof(value))) return DF_ERR_INVALID;
        skip(&p);
        if (*p && *p != '#') return DF_ERR_INVALID;
        if (!strcmp(command, "config")) {
            if (section || strcmp(name, "inline") || strcmp(value, "main")) return DF_ERR_INVALID;
            section = true;
            continue;
        }
        if (!section || strcmp(command, "option")) return DF_ERR_INVALID;
        size_t k;
        for (k = 0; k < DF_ARRAY_LEN(keys); ++k) if (!strcmp(name, keys[k])) break;
        if (k == DF_ARRAY_LEN(keys) || (seen & (1U << k))) return DF_ERR_INVALID;
        seen |= 1U << k;
        if (k < 2) {
            if (strcmp(value, "0") && strcmp(value, "1")) return DF_ERR_INVALID;
            if (k == 0) c.enabled = value[0] == '1';
            else c.recording_enabled = value[0] == '1';
        } else if (k < 7) {
            char *dest[] = {c.bridge, c.upstream, c.downstream, c.management, c.evidence_root};
            size_t limit = k == 6 ? sizeof(c.evidence_root) : sizeof(c.bridge);
            if (strlen(value) >= limit) return DF_ERR_INVALID;
            strcpy(dest[k - 2], value);
        } else if (k < 11) {
            uint32_t number = 0;
            uint32_t *dest[] = {&c.recent_budget_mib, &c.control_budget_mib,
                &c.log_budget_mib, &c.reserve_mib};
            if (!value[0]) return DF_ERR_INVALID;
            for (size_t i = 0; value[i]; ++i) {
                if (value[i] < '0' || value[i] > '9') return DF_ERR_INVALID;
                unsigned digit = (unsigned)(value[i] - '0');
                if (number > (UINT32_MAX - digit) / 10) return DF_ERR_INVALID;
                number = number * 10 + digit;
            }
            *dest[k - 7] = number;
        } else {
            if (strlen(value) >= sizeof(c.observation)) return DF_ERR_INVALID;
            strcpy(c.observation, value);
        }
    }
    if (c.enabled && !(seen & (1U << 11))) return DF_ERR_INVALID;
    if (!section || df_deployment_config_validate(&c)) return DF_ERR_INVALID;
    *out = c;
    return DF_OK;
}
