#include "moonix.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *source =
        "function add(a, b)\n"
        "  local sum = a + b\n"
        "  return sum\n"
        "end\n"
        "answer = add(20, 22)\n";
    moonix_options options;
    moonix_state *state;
    moonix_chunk first;
    moonix_chunk second;
    moonix_status status;
    char *validation_error = NULL;

    moonix_options_init(&options);
    state = moonix_newstate(&options);
    CCW_CHECK(state != NULL, "could not create Moonix state");
    if (state == NULL) return ccw_test_report("moonix-frontend");

    status = moonix_compile(state, source, strlen(source), "golden", &first);
    CCW_CHECK(status == MOONIX_OK, "compile failed: %s",
              moonix_last_error(state));
    status = moonix_compile(state, source, strlen(source), "golden", &second);
    CCW_CHECK(status == MOONIX_OK, "second compile failed: %s",
              moonix_last_error(state));
    CCW_CHECK(first.size > 12 && memcmp(first.data, "MOONIXBC", 8) == 0,
              "Moonix bytecode header missing");
    CCW_CHECK(first.size == second.size &&
                  memcmp(first.data, second.data, first.size) == 0,
              "bytecode golden must be deterministic");
    CCW_CHECK(first.source_line_count == 5, "line table source count differs");
    CCW_CHECK(first.on1x_ir != NULL, "supported source must lower to On1x");
    if (first.on1x_ir != NULL)
        CCW_CHECK(ccw_ir_validate(first.on1x_ir, &validation_error) == CCW_OK,
                  "On1x IR invalid: %s",
                  validation_error ? validation_error : "");
    free(validation_error);

    CCW_CHECK(moonix_load_chunk(state, &first) == MOONIX_OK,
              "bytecode reload failed: %s", moonix_last_error(state));
    CCW_CHECK(moonix_pcall(state, 0, 0) == MOONIX_OK,
              "bytecode execution failed: %s", moonix_last_error(state));

    moonix_chunk_clear(&second);
    moonix_chunk_clear(&first);
    moonix_close(state);
    return ccw_test_report("moonix-frontend");
}
