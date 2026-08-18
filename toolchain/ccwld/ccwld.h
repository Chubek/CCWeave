#ifndef CCWLD_H
#define CCWLD_H
#include <stddef.h>
#include <stdint.h>
#include "ccwld-plugin.h"
#include "ccwld-lto.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ccwld_plan ccwld_plan;
typedef struct ccwld_expr ccwld_expr;
typedef struct ccwld_link ccwld_link;
typedef struct { int code; char message[512]; } ccwld_error;
typedef struct { const char *kind, *format, *entry, *soname, *osabi; } ccwld_output;
typedef struct {
    const char *kind;
    const char *format;
    const char *entry;
    const char *soname;
    const char *osabi;
    const char *const *search_paths;
    size_t search_path_count;
} ccwld_link_options;
ccwld_plan *ccwld_plan_new(const char *target);
void ccwld_plan_free(ccwld_plan *);
int ccwld_plan_output(ccwld_plan *, const ccwld_output *, ccwld_error *);
int ccwld_plan_input(ccwld_plan *, const char *, int as_needed, int startup, ccwld_error *);
int ccwld_plan_search_path(ccwld_plan *, const char *, ccwld_error *);
int ccwld_plan_memory(ccwld_plan *, const char *, const char *, uint64_t, uint64_t, ccwld_error *);
int ccwld_plan_section(ccwld_plan *, const char *, const char *, uint64_t, const char *, const char *, ccwld_error *);
int ccwld_plan_symbol(ccwld_plan *, const char *, ccwld_expr *, int provide, int hidden, ccwld_error *);
int ccwld_plan_hook(ccwld_plan *, ccwld_phase, int (*)(ccwld_phase, ccwld_link *, void *), void *, ccwld_error *);
ccwld_expr *ccwld_expr_int(uint64_t);
ccwld_expr *ccwld_expr_symbol(const char *);
ccwld_expr *ccwld_expr_dot(void);
ccwld_expr *ccwld_expr_binary(char, ccwld_expr *, ccwld_expr *);
ccwld_expr *ccwld_expr_unary(char, ccwld_expr *);
ccwld_expr *ccwld_expr_align(ccwld_expr *, uint64_t);
void ccwld_expr_free(ccwld_expr *);
int ccwld_expr_eval(const ccwld_expr *, const ccwld_plan *, uint64_t dot, uint64_t *out, ccwld_error *);
int ccwld_plan_seal(ccwld_plan *, ccwld_error *);
int ccwld_plan_serialize(const ccwld_plan *, char **out, size_t *len, ccwld_error *);
int ccwld_plan_hash(const ccwld_plan *, char out[65]);
int ccwld_link_run(ccwld_plan *, const char *output, ccwld_error *);
/* Link an ordered list of object/archive paths without invoking a subprocess. */
int ccwld_link_files(const char *target, const char *output,
                     const char *const *inputs, size_t input_count,
                     const ccwld_link_options *options, ccwld_error *);
void ccwld_free(void *);
int ccwld_run_lua(const char *, const char *, ccwld_plan **, ccwld_error *);
int ccwld_run_script(const char *, const char *, const char *, ccwld_error *);
#ifdef __cplusplus
}
#endif
#endif
