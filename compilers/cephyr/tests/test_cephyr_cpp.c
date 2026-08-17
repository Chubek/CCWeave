#include "cephyr_cpp.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    static const char source[] =
        "#define ANSWER 42\n"
        "int answer(void) { /* keep the token stream clean */ return ANSWER; }\n";
    cephyr_cpp_result result = cephyr_cpp_preprocess(
        source, strlen(source), "cpp-test.c", NULL, 0);

    CCW_CHECK(result.error_message == NULL,
              "preprocessing failed: %s",
              result.error_message ? result.error_message : "(no message)");
    CCW_CHECK(result.text != NULL && result.text_len > 0,
              "preprocessing must return a token stream");
    CCW_CHECK(result.text != NULL && strstr(result.text, "42") != NULL,
              "object-like macros must be expanded");
    CCW_CHECK(result.text != NULL && strstr(result.text, "/*") == NULL,
              "comments must not survive preprocessing");
    CCW_CHECK(result.text != NULL && strstr(result.text, "answer") != NULL,
              "identifiers must survive preprocessing");

    const cephyr_line_map_entry *mapping =
        cephyr_cpp_lookup_line(&result, 1);
    CCW_CHECK(mapping != NULL, "preprocessed output must have a line map");
    if (mapping != NULL) {
        CCW_CHECK_STREQ(mapping->source_file, "cpp-test.c");
        CCW_CHECK(mapping->source_line > 0,
                  "line map source line must be one-based");
        CCW_CHECK(mapping->output_line <= 1,
                  "line map must resolve the requested output line");
    }

    cephyr_cpp_result_free(&result);
    CCW_CHECK(result.text == NULL && result.line_map == NULL &&
                  result.line_map_count == 0,
              "freeing a preprocessor result must clear owned storage");

    char *error = NULL;
    CCW_CHECK(cephyr_cpp_external("missing-input.c", NULL, &error) == NULL,
              "external preprocessing without a command must fail");
    CCW_CHECK(error != NULL && strstr(error, "command") != NULL,
              "external preprocessing failure must explain the missing command");
    free(error);

    return ccw_test_report("cephyr-cpp");
}
