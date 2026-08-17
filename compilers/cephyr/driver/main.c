/* Cephyr CLI entry point.
 *
 * Usage: cephyr [options] <source.c>
 *
 * Options:
 *   -o <file>      Output file (default: a.out)
 *   -O0, -O1, -O2  Optimization level
 *   -I <dir>       Add include path
 *   -D <name>      Define macro
 *   -S             Emit assembly (stop after compilation)
 *   -c             Compile only (don't link)
 *   -E             Preprocess only
 *   --emit-ir      Dump Weave IR text
 *   --target <t>   Target triple
 *   --cpp <cmd>    Use external preprocessor
 *   --help         Show help
 *   --version      Show version
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cephyr_driver.h"

static void print_help(const char *prog)
{
    printf("Cephyr C compiler v0.1.0 — CCWeave-based C17 compiler\n");
    printf("Usage: %s [options] <source.c>\n\n", prog);
    printf("Options:\n");
    printf("  -o <file>      Output file (default: stdout)\n");
    printf("  -O0, -O1, -O2  Optimization level (default: -O0)\n");
    printf("  -I <dir>       Add include path\n");
    printf("  -D <name>      Define preprocessor macro\n");
    printf("  -S             Emit assembly (stop after compilation)\n");
    printf("  -E             Preprocess only\n");
    printf("  --emit-ir      Dump Weave IR text\n");
    printf("  --target <t>   Target triple (default: x86_64-linux-gnu)\n");
    printf("  --cpp <cmd>    Use external preprocessor\n");
    printf("  --help         Show this help\n");
    printf("  --version      Show version\n");
}

static void print_version(void)
{
    printf("Cephyr v0.1.0 — CCWeave-based C17 compiler\n");
    printf("Copyright (c) 2026 CCWeave Project\n");
}

int main(int argc, char **argv)
{
    cephyr_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.opt_level = CEPHYR_O0;
    const char *source_path = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if (strcmp(argv[i], "-O0") == 0) {
            opts.opt_level = CEPHYR_O0;
        } else if (strcmp(argv[i], "-O1") == 0) {
            opts.opt_level = CEPHYR_O1;
        } else if (strcmp(argv[i], "-O2") == 0) {
            opts.opt_level = CEPHYR_O2;
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            /* Add include path — we'd need a dynamic array, simplified for v0.1 */
            opts.include_paths = realloc((void *)opts.include_paths,
                                         (size_t)(opts.include_path_count + 1) * sizeof(char *));
            ((const char **)opts.include_paths)[opts.include_path_count++] = argv[++i];
        } else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            opts.defines = realloc((void *)opts.defines,
                                   (size_t)(opts.define_count + 1) * sizeof(char *));
            ((const char **)opts.defines)[opts.define_count++] = argv[++i];
        } else if (strcmp(argv[i], "-S") == 0) {
            /* -S: emit assembly, stop after compilation */
            /* (already the default for v0.1) */
        } else if (strcmp(argv[i], "-E") == 0) {
            fprintf(stderr, "cephyr: -E (preprocess only) not yet implemented\n");
            return 1;
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            opts.emit_ir = true;
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            opts.target_triple = argv[++i];
        } else if (strcmp(argv[i], "--cpp") == 0 && i + 1 < argc) {
            opts.cpp_command = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "cephyr: unknown option '%s'\n", argv[i]);
            return 1;
        } else {
            source_path = argv[i];
        }
    }

    if (!source_path) {
        fprintf(stderr, "cephyr: no source file specified\n");
        fprintf(stderr, "Usage: %s [options] <source.c>\n", argv[0]);
        return 1;
    }

    opts.source_path = source_path;

    /* Compile */
    cephyr_result result = cephyr_compile(&opts);

    if (result != CEPHYR_SUCCESS) {
        fprintf(stderr, "cephyr: compilation failed: %s\n", cephyr_result_string(result));
        return 1;
    }

    return 0;
}
