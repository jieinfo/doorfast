/* Compile without DF_ALLOW_TEST_ROOT: exercise the production UID boundary. */
#include "access_credentials.h"
#include "doorfast.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    if (geteuid() == 0) {
        puts("SKIP: non-root owner fixture requires an unprivileged process");
        return 0;
    }
    char path[] = "/tmp/doorfast-credential-owner-XXXXXX";
    struct df_access_credentials credentials = {0};
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    int failed = fchmod(fd, 0600) != 0 ||
        write(fd, "gate_main inherit\n", 18) != 18;
    if (close(fd) != 0) failed = 1;
    if (df_access_credentials_load(&credentials, path) != DF_ERR_INVALID) failed = 1;
    unlink(path);
    if (!failed) puts("production root-owner guard rejects non-root owned 0600 store");
    return failed;
}
