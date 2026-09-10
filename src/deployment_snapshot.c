#include "deployment_snapshot.h"
#include "runtime_config.h"

#include <ctype.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define DF_SNAPSHOT_PATH_MAX 1024
#define DF_SNAPSHOT_FILE_MAX 65536

static int df_root_valid(const char *root) {
    const char *p;

    if (root == NULL || root[0] == '\0') return 0;
    for (p = root; (p = strstr(p, "..")) != NULL; p += 2) {
        if ((p == root || p[-1] == '/') && (p[2] == '\0' || p[2] == '/')) return 0;
    }
    return 1;
}

static int df_path(char *output, size_t size, const char *root,
                   const char *absolute) {
    int written;

    if (strcmp(root, "/") == 0)
        written = snprintf(output, size, "%s", absolute);
    else
        written = snprintf(output, size, "%s%s", root, absolute);
    return written >= 0 && (size_t)written < size ? DF_OK : DF_ERR_INVALID;
}

static int df_read_file(const char *path, char *output, size_t size) {
    FILE *file;
    size_t length;

    if (size < 2) return DF_ERR_INVALID;
    file = fopen(path, "rb");
    if (file == NULL) return DF_ERR_IO;
    length = fread(output, 1, size - 1, file);
    if (ferror(file) || (length == size - 1 && fgetc(file) != EOF)) {
        (void)fclose(file);
        return DF_ERR_IO;
    }
    output[length] = '\0';
    return fclose(file) == 0 ? DF_OK : DF_ERR_IO;
}

static int df_interface_exists(const char *root, const char *name,
                               bool require_ethernet) {
    char path[DF_SNAPSHOT_PATH_MAX], type_path[DF_SNAPSHOT_PATH_MAX];
    char text[32];
    struct stat status;

    if (df_path(path, sizeof(path), root, "/sys/class/net/") != DF_OK ||
        strlen(path) + strlen(name) >= sizeof(path)) return 0;
    strcat(path, name);
    if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) return 0;
    if (!require_ethernet) return 1;
    if (snprintf(type_path, sizeof(type_path), "%s/%s", path,
                 strcmp(root, "/") == 0 ? "type" : "doorfast_type") >=
        (int)sizeof(type_path) ||
        df_read_file(type_path, text, sizeof(text)) != DF_OK) return 0;
    text[strcspn(text, "\r\n")] = '\0';
    return strcmp(text, "1") == 0 || strcmp(text, "ethernet") == 0;
}

static int df_read_flag(const char *root, const char *bridge,
                        const char *leaf, bool *value) {
    char relative[256], path[DF_SNAPSHOT_PATH_MAX], text[32], extra;

    if (snprintf(relative, sizeof(relative), "/sys/class/net/%s/bridge/%s",
                 bridge, leaf) >= (int)sizeof(relative) ||
        df_path(path, sizeof(path), root, relative) != DF_OK ||
        df_read_file(path, text, sizeof(text)) != DF_OK ||
        sscanf(text, " %c %c", &extra, &extra) != 1 ||
        (text[strspn(text, " \t\r\n")] != '0' &&
         text[strspn(text, " \t\r\n")] != '1')) return DF_ERR_IO;
    *value = text[strspn(text, " \t\r\n")] == '1';
    return DF_OK;
}

static int df_member_compare(const void *left, const void *right) {
    return strcmp((const char *)left, (const char *)right);
}

static void df_collect_members(const struct df_deployment_config *config,
                               const char *root,
                               struct df_deployment_snapshot *snapshot) {
    char relative[256], path[DF_SNAPSHOT_PATH_MAX];
    DIR *directory;
    struct dirent *entry;

    if (snprintf(relative, sizeof(relative), "/sys/class/net/%s/brif",
                 config->bridge) >= (int)sizeof(relative) ||
        df_path(path, sizeof(path), root, relative) != DF_OK ||
        (directory = opendir(path)) == NULL) return;
    while ((entry = readdir(directory)) != NULL) {
        size_t length;
        if (entry->d_name[0] == '.') continue;
        length = strlen(entry->d_name);
        if (length == 0 || length >= DF_DEPLOYMENT_IFNAME_MAX) continue;
        if (snapshot->bridge_member_count <
            DF_ARRAY_LEN(snapshot->bridge_members)) {
            memcpy(snapshot->bridge_members[snapshot->bridge_member_count],
                   entry->d_name, length + 1);
            snapshot->bridge_member_count++;
        }
    }
    (void)closedir(directory);
    qsort(snapshot->bridge_members, snapshot->bridge_member_count,
          sizeof(snapshot->bridge_members[0]), df_member_compare);
}

static int df_token_match(const char *text, const char *token) {
    size_t length = strlen(token);
    const char *match = text;

    if (length == 0) return 0;

    while ((match = strstr(match, token)) != NULL) {
        unsigned char before = match == text ? 0 : (unsigned char)match[-1];
        unsigned char after = (unsigned char)match[length];
        int before_word = isalnum(before) || before == '_' || before == '-' ||
                          before == '.';
        int after_word = isalnum(after) || after == '_' || after == '-' ||
                         after == '.';
        if (!before_word && !after_word) return 1;
        match += length;
    }
    return 0;
}

static int df_references_role(const char *text,
                              const struct df_deployment_config *config) {
    return df_token_match(text, config->bridge) ||
           df_token_match(text, config->upstream) ||
           df_token_match(text, config->downstream);
}

static int df_network_interface_reference(
    const char *text, const struct df_deployment_config *config) {
    const char *line = text;
    int in_interface = 0;

    while (*line) {
        size_t length = strcspn(line, "\n");
        char copy[512];
        if (length >= sizeof(copy)) return 1;
        memcpy(copy, line, length);
        copy[length] = '\0';
        const char *p = copy;
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "config ", 7) == 0) {
            in_interface = df_token_match(p + 7, "interface");
            if (in_interface && df_references_role(p + 7, config)) return 1;
        } else if (in_interface && df_references_role(p, config))
            return 1;
        line += length;
        if (*line == '\n') line++;
    }
    return 0;
}

static int df_read_config(const char *root, const char *name,
                          char *text, size_t size) {
    char relative[128], path[DF_SNAPSHOT_PATH_MAX];
    if (snprintf(relative, sizeof(relative), "/etc/config/%s", name) >=
        (int)sizeof(relative) || df_path(path, sizeof(path), root, relative) != DF_OK)
        return DF_ERR_IO;
    return df_read_file(path, text, size);
}

static bool df_read_passive(const char *root) {
    char text[DF_SNAPSHOT_FILE_MAX];
    struct df_runtime_config runtime;

    if (df_read_config(root, "doorfast", text, sizeof(text)) != DF_OK) return false;
    return df_runtime_config_parse(text, &runtime) == DF_OK &&
           runtime.config.passive_only;
}

static void df_collect_addresses(const struct df_deployment_config *config,
                                 const char *root,
                                 struct df_deployment_snapshot *snapshot) {
    if (strcmp(root, "/") == 0) {
        struct ifaddrs *addresses = NULL, *item;
        if (getifaddrs(&addresses) != 0) {
            snapshot->bridge_has_ipv4 = true;
            snapshot->bridge_has_ipv6 = true;
            return;
        }
        for (item = addresses; item != NULL; item = item->ifa_next) {
            if (item->ifa_addr == NULL || strcmp(item->ifa_name, config->bridge)) continue;
            if (item->ifa_addr->sa_family == AF_INET) snapshot->bridge_has_ipv4 = true;
            if (item->ifa_addr->sa_family == AF_INET6) snapshot->bridge_has_ipv6 = true;
        }
        freeifaddrs(addresses);
    } else {
        char relative[256], path[DF_SNAPSHOT_PATH_MAX], text[32];
        int ipv4, ipv6, consumed = 0;
        if (snprintf(relative, sizeof(relative),
                     "/sys/class/net/%s/doorfast_address_state", config->bridge) >=
                (int)sizeof(relative) ||
            df_path(path, sizeof(path), root, relative) != DF_OK ||
            df_read_file(path, text, sizeof(text)) != DF_OK ||
            sscanf(text, " %d %d %n", &ipv4, &ipv6, &consumed) != 2 ||
            text[consumed] != '\0' || (ipv4 != 0 && ipv4 != 1) ||
            (ipv6 != 0 && ipv6 != 1)) {
            snapshot->bridge_has_ipv4 = true;
            snapshot->bridge_has_ipv6 = true;
        } else {
            snapshot->bridge_has_ipv4 = ipv4 != 0;
            snapshot->bridge_has_ipv6 = ipv6 != 0;
        }
    }
}

static void df_collect_lldp(const char *root,
                            struct df_deployment_snapshot *snapshot) {
    char path[DF_SNAPSHOT_PATH_MAX];
    DIR *proc;
    struct dirent *entry;
    bool evidence = false;

    snapshot->lldp_active = true;
    if (df_path(path, sizeof(path), root, "/proc") != DF_OK ||
        (proc = opendir(path)) == NULL) return;
    while ((entry = readdir(proc)) != NULL) {
        char comm_path[DF_SNAPSHOT_PATH_MAX], text[64];
        size_t i;
        for (i = 0; entry->d_name[i] && isdigit((unsigned char)entry->d_name[i]); ++i) {}
        if (i == 0 || entry->d_name[i] != '\0') continue;
        if (snprintf(comm_path, sizeof(comm_path), "%s/%s/comm", path,
                     entry->d_name) >= (int)sizeof(comm_path) ||
            df_read_file(comm_path, text, sizeof(text)) != DF_OK) continue;
        evidence = true;
        text[strcspn(text, "\r\n")] = '\0';
        if (strcmp(text, "lldpd") == 0) {
            (void)closedir(proc);
            return;
        }
    }
    (void)closedir(proc);
    snapshot->lldp_active = !evidence;
}

static void df_collect_mount(const struct df_deployment_config *config,
                             const char *root,
                             struct df_deployment_snapshot *snapshot) {
    char path[DF_SNAPSHOT_PATH_MAX], text[DF_SNAPSHOT_FILE_MAX];
    const char *line;

    if (df_path(path, sizeof(path), root, "/proc/self/mountinfo") != DF_OK ||
        df_read_file(path, text, sizeof(text)) != DF_OK) return;
    line = text;
    while (*line) {
        size_t length = strcspn(line, "\n");
        char copy[2048], mountpoint[512], fstype[64];
        char *separator;
        if (length >= sizeof(copy)) return;
        memcpy(copy, line, length); copy[length] = '\0';
        separator = strstr(copy, " - ");
        if (separator != NULL &&
            sscanf(copy, "%*s %*s %*s %*s %511s", mountpoint) == 1 &&
            sscanf(separator + 3, "%63s", fstype) == 1 &&
            strcmp(mountpoint, config->evidence_root) == 0) {
            snapshot->evidence_root_is_mount = true;
            snapshot->evidence_root_is_temporary =
                strcmp(fstype, "tmpfs") == 0 || strcmp(fstype, "ramfs") == 0 ||
                strcmp(fstype, "overlay") == 0;
            break;
        }
        line += length;
        if (*line == '\n') line++;
    }

    if (strcmp(root, "/") == 0) {
        struct statvfs status;
        if (statvfs(config->evidence_root, &status) == 0 &&
            status.f_frsize != 0 &&
            (uint64_t)status.f_blocks <= UINT64_MAX / status.f_frsize &&
            (uint64_t)status.f_bavail <= UINT64_MAX / status.f_frsize) {
            snapshot->evidence_total_bytes =
                (uint64_t)status.f_blocks * status.f_frsize;
            snapshot->evidence_available_bytes =
                (uint64_t)status.f_bavail * status.f_frsize;
        }
    } else {
        char relative[512];
        unsigned long long total, available;
        int consumed = 0;
        if (snprintf(relative, sizeof(relative), "%s/.doorfast-statvfs",
                     config->evidence_root) < (int)sizeof(relative) &&
            df_path(path, sizeof(path), root, relative) == DF_OK &&
            df_read_file(path, text, sizeof(text)) == DF_OK &&
            sscanf(text, " %llu %llu %n", &total, &available, &consumed) == 2 &&
            text[consumed] == '\0') {
            snapshot->evidence_total_bytes = (uint64_t)total;
            snapshot->evidence_available_bytes = (uint64_t)available;
        }
    }
}

int df_deployment_snapshot_collect(const struct df_deployment_config *config,
                                   const char *root,
                                   struct df_deployment_snapshot *out) {
    struct df_deployment_snapshot snapshot = {0};
    char text[DF_SNAPSHOT_FILE_MAX];

    if (out == NULL || !df_root_valid(root) ||
        df_deployment_config_validate(config) != DF_OK) return DF_ERR_INVALID;
    snapshot.bridge_exists = df_interface_exists(root, config->bridge, false);
    snapshot.upstream_exists = df_interface_exists(root, config->upstream, true);
    snapshot.downstream_exists = df_interface_exists(root, config->downstream, true);
    snapshot.management_exists = df_interface_exists(root, config->management, true);
    df_collect_members(config, root, &snapshot);
    if (df_read_flag(root, config->bridge, "stp_state", &snapshot.stp_enabled) != DF_OK)
        snapshot.stp_enabled = true;
    if (df_read_flag(root, config->bridge, "multicast_snooping",
                     &snapshot.multicast_snooping_enabled) != DF_OK)
        snapshot.multicast_snooping_enabled = true;
    df_collect_addresses(config, root, &snapshot);
    if (df_read_config(root, "network", text, sizeof(text)) != DF_OK)
        snapshot.network_interface_reference = true;
    else
        snapshot.network_interface_reference =
            df_network_interface_reference(text, config) != 0;
    if (df_read_config(root, "firewall", text, sizeof(text)) != DF_OK)
        snapshot.firewall_reference = true;
    else snapshot.firewall_reference = df_references_role(text, config) != 0;
    if (df_read_config(root, "dhcp", text, sizeof(text)) != DF_OK)
        snapshot.dhcp_ra_reference = true;
    else snapshot.dhcp_ra_reference = df_references_role(text, config) != 0;
    snapshot.doorfast_passive_only = df_read_passive(root);
    df_collect_lldp(root, &snapshot);
    df_collect_mount(config, root, &snapshot);
    *out = snapshot;
    return DF_OK;
}
