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
    return ccw_test_report("cephyr-driver-tools");
}
