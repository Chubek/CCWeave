#include "sml_parthia.h"
#include "repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_stream(FILE *stream, size_t *length)
{
    size_t used = 0;
    size_t capacity = 4096;
    char *buffer = (char *)malloc(capacity + 1u);
    if (buffer == NULL) return NULL;
    for (;;) {
        size_t got = fread(buffer + used, 1, capacity - used, stream);
        used += got;
        if (got == 0) {
            if (ferror(stream)) {
                free(buffer);
                return NULL;
            }
            break;
        }
        if (used == capacity) {
            char *grown;
            capacity *= 2u;
            grown = (char *)realloc(buffer, capacity + 1u);
            if (grown == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = grown;
        }
    }
    buffer[used] = '\0';
    *length = used;
    return buffer;
}

int main(int argc, char **argv)
{
    FILE *input = stdin;
    char *source;
    size_t length;
    char *error = NULL;
    ccw_sml_parthia_report report;
    ccw_sml_parthia_program *program;

    if (argc > 2) {
        fprintf(stderr, "usage: sml-parthia [file.sml]\n");
        return 2;
    }
    if (argc == 1) return sml_parthia_repl();

    if (argc == 2) {
        input = fopen(argv[1], "rb");
        if (input == NULL) {
            fprintf(stderr, "sml-parthia: cannot open %s\n", argv[1]);
            return 1;
        }
    }
    source = read_stream(input, &length);
    if (argc == 2) fclose(input);
    if (source == NULL) {
        fprintf(stderr, "sml-parthia: cannot read input\n");
        return 1;
    }
    program = ccw_sml_parthia_compile(source, length, &report, &error);
    free(source);
    if (program == NULL) {
        fprintf(stderr, "sml-parthia: %s\n", error ? error : "compile failed");
        free(error);
        return 1;
    }
    fputs(ccw_sml_parthia_core_ast(program), stdout);
    fputc('\n', stdout);
    ccw_sml_parthia_program_destroy(program);
    free(error);
    return 0;
}
