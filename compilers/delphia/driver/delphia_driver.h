/* Delphia driver — Delphi/Object Pascal compiler façade. */
#ifndef DELPHIA_DRIVER_H
#define DELPHIA_DRIVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DELPHIA_O0 = 0,
    DELPHIA_O1 = 1,
    DELPHIA_O2 = 2
} delphia_opt_level;

typedef enum {
    DELPHIA_STOP_NONE = 0,
    DELPHIA_STOP_ASSEMBLY
} delphia_stop_stage;

typedef struct {
    const char *source_path;
    const char *output_path;
    const char *target_triple;
    delphia_opt_level opt_level;
    delphia_stop_stage stop_stage;
    bool emit_ir;
    bool keep_temp;
    bool mode_delphi;
    bool generics;
    bool class_helpers;
    bool anonymous_methods;
    bool opt_level_explicit;
    bool target_explicit;
} delphia_options;

typedef enum {
    DELPHIA_SUCCESS = 0,
    DELPHIA_ERR_INPUT = 1,
    DELPHIA_ERR_PARSE = 2,
    DELPHIA_ERR_SEMA = 3,
    DELPHIA_ERR_LOWER = 4,
    DELPHIA_ERR_CODEGEN = 5,
    DELPHIA_ERR_INTERNAL = 99
} delphia_result;

void delphia_options_init(delphia_options *options, const char *source_path);
delphia_result delphia_compile(const delphia_options *options);
const char *delphia_result_string(delphia_result result);
const char *delphia_target_arch(const char *target_triple);
void delphia_list_target_triples(void);

#ifdef __cplusplus
}
#endif
#endif
