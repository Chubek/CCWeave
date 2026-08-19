#include "delphia_driver.h"
#include "ccw_test.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    delphia_options options;
    delphia_options_init(&options, "fixture.pas");
    CCW_CHECK(options.mode_delphi && options.generics &&
              options.class_helpers && options.anonymous_methods,
              "Delphia defaults must enable Delphi dialect gates");
    CCW_CHECK_STREQ(delphia_target_arch("x86_64-linux-gnu"), "x86-64");
    CCW_CHECK(delphia_target_arch("unknown") == NULL,
              "unknown target must be rejected");
    CCW_CHECK_STREQ(delphia_result_string(DELPHIA_ERR_PARSE), "parse error");
    return ccw_test_report("delphia-driver");
}
