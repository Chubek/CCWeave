#include "ccw_test.h"
#include "../toolchain/ccwld/ccwld.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    ccwld_error error = {0};
    ccwld_plan *plan = NULL;
    char script[1024];
    char *serialized = NULL;
    size_t length = 0;

    snprintf(script, sizeof(script), "%s/ccwld-basic.lua",
             CCWLD_FIXTURE_DIR);
    CCW_CHECK(ccwld_run_lua(script, "x86_64-linux-gnu", &plan, &error),
              "Lua frontend failed: %s", error.message);
    CCW_CHECK(plan != NULL, "Lua frontend returned no plan");
    if (plan != NULL) {
        CCW_CHECK(ccwld_plan_serialize(plan, &serialized, &length, &error),
                  "serialization failed: %s", error.message);
        CCW_CHECK(length > 0 && serialized != NULL,
                  "Lua plan serialization was empty");
        CCW_CHECK(serialized != NULL && strstr(serialized, "main.o") != NULL,
                  "Lua input was not preserved in serialized plan");
    }
    free(serialized);
    ccwld_plan_free(plan);
    return ccw_test_report("ccwld-lua");
}
