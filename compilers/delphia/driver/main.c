/* Delphia command line driver. */
#include "delphia_driver.h"
#include <stdio.h>
#include <string.h>

static void usage(const char *name)
{
    printf("Delphia Delphi compiler v0.1.0\n"
           "Usage: %s [options] source.pas\n"
           "  -o FILE       output assembly/IR (default: stdout)\n"
           "  -S             emit assembly\n"
           "  --emit-ir     emit Weave IR\n"
           "  -O0|-O1|-O2   optimization level\n"
           "  --target TRIPLE\n"
           "  --list-triples\n", name);
}

int main(int argc, char **argv)
{
    delphia_options options;
    const char *input = NULL;
    if (argc < 2) { usage(argv[0]); return 2; }
    delphia_options_init(&options, NULL);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        if (!strcmp(argv[i], "--list-triples")) {
            delphia_list_target_triples(); return 0;
        } else if (!strcmp(argv[i], "--emit-ir")) options.emit_ir = true;
        else if (!strcmp(argv[i], "-S")) options.stop_stage = DELPHIA_STOP_ASSEMBLY;
        else if (!strncmp(argv[i], "-O", 2) && argv[i][2] >= '0' &&
                 argv[i][2] <= '2') {
            options.opt_level = (delphia_opt_level)(argv[i][2] - '0');
            options.opt_level_explicit = true;
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) options.output_path = argv[++i];
        else if (!strcmp(argv[i], "--target") && i + 1 < argc) {
            options.target_triple = argv[++i]; options.target_explicit = true;
        } else if (argv[i][0] != '-') input = argv[i];
        else { fprintf(stderr, "delphia: unknown option %s\n", argv[i]); return 2; }
    }
    if (!input) { fprintf(stderr, "delphia: no input file\n"); return 2; }
    options.source_path = input;
    delphia_result result = delphia_compile(&options);
    if (result != DELPHIA_SUCCESS)
        fprintf(stderr, "delphia: %s\n", delphia_result_string(result));
    return result == DELPHIA_SUCCESS ? 0 : (int)result;
}
