#define _POSIX_C_SOURCE 200809L

#include "../driver/cephyr_driver.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *assembler = cephyr_discover_assembler("x86_64-linux-gnu");
    CCW_CHECK(assembler != NULL, "default assembler discovery returned NULL");
    CCW_CHECK(assembler != NULL &&
              (strstr(assembler, "ccwas") != NULL ||
               strcmp(assembler, "ccwas") == 0),
              "default assembler must be ccwas, got '%s'",
              assembler ? assembler : "(null)");
    free((char *)assembler);

    unsetenv("CEPHYR_LD");
    const char *linker = cephyr_discover_linker("x86_64-linux-gnu");
    CCW_CHECK(linker != NULL, "default linker discovery returned NULL");
    CCW_CHECK(linker != NULL &&
              (strstr(linker, "ccwld") != NULL ||
               strcmp(linker, "ccwld") == 0),
              "default linker must be ccwld, got '%s'",
              linker ? linker : "(null)");
    free((char *)linker);

    CCW_CHECK(setenv("CEPHYR_LD", "custom-ld --flag", 1) == 0,
              "could not set CEPHYR_LD");
    linker = cephyr_discover_linker("x86_64-linux-gnu");
    CCW_CHECK_STREQ(linker, "custom-ld --flag");
    free((char *)linker);
    unsetenv("CEPHYR_LD");

    return ccw_test_report("cephyr-driver-tools");
}
