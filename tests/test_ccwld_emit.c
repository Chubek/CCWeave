#define _POSIX_C_SOURCE 200809L

#include "ccw_test.h"
#include "../toolchain/ccwld/ccwld.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    ccwld_error error = {0};
    ccwld_output output = {"reloc", "elf", NULL, NULL, NULL};
    ccwld_plan *plan = ccwld_plan_new("x86_64-linux-gnu");
    char path[] = "/tmp/ccwld-test-XXXXXX";
    char buffer[256] = {0};
    FILE *file;
    int fd;

    CCW_CHECK(plan != NULL, "plan allocation failed");
    if (plan == NULL) return ccw_test_report("ccwld-emit");
    CCW_CHECK(ccwld_plan_output(plan, &output, &error),
              "output declaration failed: %s", error.message);
    CCW_CHECK(ccwld_plan_seal(plan, &error),
              "plan sealing failed: %s", error.message);
    fd = mkstemp(path);
    CCW_CHECK(fd >= 0, "could not create output path");
    if (fd >= 0) {
        close(fd);
        CCW_CHECK(ccwld_link_run(plan, path, &error),
                  "link emission failed: %s", error.message);
        file = fopen(path, "rb");
        CCW_CHECK(file != NULL, "emitted object could not be opened");
        if (file != NULL) {
            size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);
            buffer[n] = '\0';
            fclose(file);
            CCW_CHECK(strstr(buffer, "CCWLD-OBJECT") != NULL,
                      "emitted object header missing");
            CCW_CHECK(strstr(buffer, ".note.ccw=") != NULL,
                      "producer note missing");
        }
        unlink(path);
    }
    ccwld_plan_free(plan);
    return ccw_test_report("ccwld-emit");
}
