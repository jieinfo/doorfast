#include "deployment_report.h"

#include <string.h>

struct df_failure_name {
    uint64_t bit;
    const char *name;
};

static const struct df_failure_name df_failure_names[] = {
    {DF_DEPLOYMENT_MISSING_INTERFACE, "missing_interface"},
    {DF_DEPLOYMENT_BAD_BRIDGE_MEMBERS, "bad_bridge_members"},
    {DF_DEPLOYMENT_BRIDGE_HAS_ADDRESS, "bridge_has_address"},
    {DF_DEPLOYMENT_BRIDGE_MANAGED, "bridge_managed"},
    {DF_DEPLOYMENT_FIREWALL_REFERENCE, "firewall_reference"},
    {DF_DEPLOYMENT_DHCP_RA_REFERENCE, "dhcp_ra_reference"},
    {DF_DEPLOYMENT_STP_ENABLED, "stp_enabled"},
    {DF_DEPLOYMENT_MULTICAST_SNOOPING, "multicast_snooping"},
    {DF_DEPLOYMENT_LLDP_ACTIVE, "lldp_active"},
    {DF_DEPLOYMENT_BAD_MOUNT, "bad_mount"},
    {DF_DEPLOYMENT_LOW_CAPACITY, "low_capacity"},
    {DF_DEPLOYMENT_NOT_PASSIVE, "not_passive"}
};

static int df_json_string(FILE *stream, const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;
    if (fputc('"', stream) == EOF) return DF_ERR_IO;
    while (*cursor) {
        if (*cursor == '"' || *cursor == '\\') {
            if (fputc('\\', stream) == EOF || fputc(*cursor, stream) == EOF)
                return DF_ERR_IO;
        } else if (*cursor < 0x20) {
            if (fprintf(stream, "\\u%04x", *cursor) < 0) return DF_ERR_IO;
        } else if (fputc(*cursor, stream) == EOF) return DF_ERR_IO;
        cursor++;
    }
    return fputc('"', stream) == EOF ? DF_ERR_IO : DF_OK;
}

static int df_json_field(FILE *stream, const char *name, const char *value) {
    if (df_json_string(stream, name) != DF_OK || fputc(':', stream) == EOF ||
        df_json_string(stream, value) != DF_OK) return DF_ERR_IO;
    return DF_OK;
}

int df_deployment_report_write_json(FILE *stream,
                                    const struct df_deployment_config *config,
                                    const struct df_deployment_report *report) {
    bool first = true;
    uint64_t known = 0;

    if (stream == NULL || config == NULL || report == NULL ||
        report->safe != (report->failures == 0)) return DF_ERR_INVALID;
    for (size_t i = 0; i < DF_ARRAY_LEN(df_failure_names); ++i)
        known |= df_failure_names[i].bit;
    if ((report->failures & ~known) != 0) return DF_ERR_INVALID;
    if (fputc('{', stream) == EOF ||
        df_json_field(stream, "bridge", config->bridge) != DF_OK ||
        fputc(',', stream) == EOF ||
        df_json_field(stream, "upstream", config->upstream) != DF_OK ||
        fputc(',', stream) == EOF ||
        df_json_field(stream, "downstream", config->downstream) != DF_OK ||
        fputc(',', stream) == EOF ||
        df_json_field(stream, "management", config->management) != DF_OK ||
        fprintf(stream, ",\"safe\":%s,\"failures\":[",
                report->safe ? "true" : "false") < 0) return DF_ERR_IO;
    for (size_t i = 0; i < DF_ARRAY_LEN(df_failure_names); ++i) {
        if ((report->failures & df_failure_names[i].bit) == 0) continue;
        if (!first && fputc(',', stream) == EOF) return DF_ERR_IO;
        if (df_json_string(stream, df_failure_names[i].name) != DF_OK)
            return DF_ERR_IO;
        first = false;
    }
    if (fputs("]}\n", stream) == EOF || ferror(stream)) return DF_ERR_IO;
    return DF_OK;
}
