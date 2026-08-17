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
#include "ketopt.h"
#include "kvec.h"

enum {
    CEPHYR_OPT_EMIT_IR = 256,
    CEPHYR_OPT_TARGET,
    CEPHYR_OPT_CPP,
    CEPHYR_OPT_HELP,
    CEPHYR_OPT_VERSION
};

static const ko_longopt_t cephyr_long_options[] = {
    { "emit-ir", ko_no_argument,       CEPHYR_OPT_EMIT_IR },
    { "target",  ko_required_argument, CEPHYR_OPT_TARGET },
    { "cpp",     ko_required_argument, CEPHYR_OPT_CPP },
    { "help",    ko_no_argument,       CEPHYR_OPT_HELP },
    { "version", ko_no_argument,       CEPHYR_OPT_VERSION },
    { NULL,      0,                    0 }
};

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
    kvec_t(const char *) include_paths;
    kvec_t(const char *) defines;
    kv_init(include_paths);
    kv_init(defines);

    const char *source_path = NULL;
    int exit_code = 1;
    ketopt_t parser = KETOPT_INIT;
    int opt;

    /* Klib's ketopt handles short/long options and keeps collection storage
     * in Klib vectors until compilation has consumed it. */
    while ((opt = ketopt(&parser, argc, argv, 1, "o:I:D:O:SEchV",
                         cephyr_long_options)) != -1) {
        switch (opt) {
        case 'o':
            opts.output_path = parser.arg;
            break;
        case 'I':
            kv_push(const char *, include_paths, parser.arg);
            break;
        case 'D':
            kv_push(const char *, defines, parser.arg);
            break;
        case 'O':
            if (!parser.arg || strcmp(parser.arg, "0") == 0)
                opts.opt_level = CEPHYR_O0;
            else if (strcmp(parser.arg, "1") == 0)
                opts.opt_level = CEPHYR_O1;
            else if (strcmp(parser.arg, "2") == 0)
                opts.opt_level = CEPHYR_O2;
            else {
                fprintf(stderr, "cephyr: invalid optimization level '-O%s'\n",
                        parser.arg ? parser.arg : "");
                goto cleanup;
            }
            break;
        case 'S':
        case 'c':
            /* -S/-c are accepted; v0.1 already stops before system linking. */
            break;
        case 'E':
            fprintf(stderr, "cephyr: -E (preprocess only) not yet implemented\n");
            goto cleanup;
        case 'h':
        case CEPHYR_OPT_HELP:
            print_help(argv[0]);
            exit_code = 0;
            goto cleanup;
        case 'V':
        case CEPHYR_OPT_VERSION:
            print_version();
            exit_code = 0;
            goto cleanup;
        case CEPHYR_OPT_EMIT_IR:
            opts.emit_ir = true;
            break;
        case CEPHYR_OPT_TARGET:
            opts.target_triple = parser.arg;
            break;
        case CEPHYR_OPT_CPP:
            opts.cpp_command = parser.arg;
            break;
        case ':':
            fprintf(stderr, "cephyr: option '-%c' requires an argument\n",
                    parser.opt);
            goto cleanup;
        case '?':
        default:
            fprintf(stderr, "cephyr: unknown option\n");
            goto cleanup;
        }
    }

    if (parser.ind < argc)
        source_path = argv[parser.ind++];
    if (parser.ind < argc) {
        fprintf(stderr, "cephyr: more than one source file specified\n");
        goto cleanup;
    }
    if (!source_path) {
        fprintf(stderr, "cephyr: no source file specified\n");
        fprintf(stderr, "Usage: %s [options] <source.c>\n", argv[0]);
        goto cleanup;
    }

    opts.source_path = source_path;
    opts.include_paths = (const char *const *)include_paths.a;
    opts.include_path_count = (int)kv_size(include_paths);
    opts.defines = (const char *const *)defines.a;
    opts.define_count = (int)kv_size(defines);

    /* Compile */
    cephyr_result result = cephyr_compile(&opts);

    if (result != CEPHYR_SUCCESS) {
        fprintf(stderr, "cephyr: compilation failed: %s\n", cephyr_result_string(result));
        goto cleanup;
    }

    exit_code = 0;

cleanup:
    kv_destroy(include_paths);
    kv_destroy(defines);
    return exit_code;
}
