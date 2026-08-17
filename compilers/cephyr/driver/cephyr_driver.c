/* Cephyr driver — §8.
 *
 * CLI entry point, toolchain discovery, plugin loading, and Sched plan
 * orchestration. Ties together: preprocessor → Swaff C frontend →
 * sema → lowering → Sched plan execution. */

#include "cephyr_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cpp/cephyr_cpp.h"
#include "../sema/cephyr_ast.h"
#include "../sema/cephyr_sema.h"
#include "../lower/cephyr_lower.h"
#include "../include/cephyr-module.h"
#include "../stdmodule/cephyr_stdmodule.h"

#include "../../../swaff/ccw_swaff.h"
#include "../../../ir/ccw_ir.h"
#include "../../../sched/sched.h"

/* ---------- default options ---------- */

void cephyr_options_init(cephyr_options *opts, const char *source_path)
{
    memset(opts, 0, sizeof(*opts));
    opts->source_path = source_path;
    opts->opt_level = CEPHYR_O0;
    opts->target_triple = "x86_64-linux-gnu";
}

/* ---------- toolchain discovery ---------- */

const char *cephyr_discover_assembler(const char *target_triple)
{
    (void)target_triple;
    /* Try common assembler names */
    static const char *candidates[] = { "as", "x86_64-linux-gnu-as", "llvm-mc", NULL };
    for (int i = 0; candidates[i]; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "which %s 2>/dev/null", candidates[i]);
        FILE *fp = popen(buf, "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp)) {
                /* Strip newline */
                size_t len = strlen(buf);
                if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
                int status = pclose(fp);
                if (status == 0) {
                    return strdup(buf);
                }
            } else {
                pclose(fp);
            }
        }
    }
    return "as"; /* fallback */
}

const char *cephyr_discover_linker(const char *target_triple)
{
    (void)target_triple;
    static const char *candidates[] = { "ld", "x86_64-linux-gnu-ld", "lld", NULL };
    for (int i = 0; candidates[i]; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "which %s 2>/dev/null", candidates[i]);
        FILE *fp = popen(buf, "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp)) {
                size_t len = strlen(buf);
                if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
                int status = pclose(fp);
                if (status == 0) {
                    return strdup(buf);
                }
            } else {
                pclose(fp);
            }
        }
    }
    return "ld"; /* fallback */
}

/* ---------- error strings ---------- */

const char *cephyr_result_string(cephyr_result r)
{
    switch (r) {
    case CEPHYR_SUCCESS:            return "success";
    case CEPHYR_ERR_PREPROCESSOR:   return "preprocessor error";
    case CEPHYR_ERR_PARSE:          return "parse error";
    case CEPHYR_ERR_SEMA:           return "semantic error";
    case CEPHYR_ERR_LOWER:          return "lowering error";
    case CEPHYR_ERR_SCHED:          return "scheduler error";
    case CEPHYR_ERR_ASSEMBLE:       return "assembler error";
    case CEPHYR_ERR_LINK:           return "linker error";
    case CEPHYR_ERR_INTERNAL:       return "internal compiler error";
    default:                        return "unknown error";
    }
}

/* ---------- read source file ---------- */

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len < 0) { fclose(f); return NULL; }

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);

    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* ---------- Sched plan execution ---------- */

static cephyr_result run_sched_plan(ccw_ir *ir, const cephyr_options *opts)
{
    /* Determine the Sched script path */
    const char *script_name = NULL;
    switch (opts->opt_level) {
    case CEPHYR_O0: script_name = "O0.lua"; break;
    case CEPHYR_O1: script_name = "O1.lua"; break;
    case CEPHYR_O2: script_name = "O2.lua"; break;
    }

    /* Build the full path to the script */
    char script_path[1024];
    snprintf(script_path, sizeof(script_path),
             "%s/compilers/cephyr/sched/%s",
             /* We need to find the repo root. The driver is at
              * compilers/cephyr/driver/cephyr_driver.c, so repo root
              * is ../../.. from the binary's location. Use a simple
              * heuristic: look relative to the current working directory
              * and also try the binary's directory. */
             "..", script_name);

    /* Try to find the script */
    FILE *test = fopen(script_path, "r");
    if (!test) {
        /* Try from the repo root */
        snprintf(script_path, sizeof(script_path),
                 "compilers/cephyr/sched/%s", script_name);
        test = fopen(script_path, "r");
    }
    if (!test) {
        fprintf(stderr, "cephyr: error: cannot find Sched script '%s'\n", script_name);
        return CEPHYR_ERR_SCHED;
    }
    fclose(test);

    /* Load and run the Sched script */
    ccw_sched_error err;
    memset(&err, 0, sizeof(err));
    ccw_plan *plan = NULL;
    int rc = ccw_sched_run_script(script_path, "manifests", &plan, &err);
    if (rc != 0) {
        fprintf(stderr, "cephyr: sched error: %s\n", err.message);
        return CEPHYR_ERR_SCHED;
    }

    if (plan) {
        /* Write the plan for debugging */
        char plan_path[1024];
        snprintf(plan_path, sizeof(plan_path),
                 "compilers/cephyr/sched/plans/%s.plan", script_name);
        ccw_plan_write(plan, plan_path, &err);
        ccw_plan_free(plan);
    }

    /* Print the IR for now (in v0.1, the plan runs but doesn't fully
     * execute the pipeline — the Kernels are the executors) */
    if (opts->emit_ir) {
        char *ir_text = ccw_ir_print(ir);
        if (ir_text) {
            printf("%s\n", ir_text);
            free(ir_text);
        }
    }

    return CEPHYR_SUCCESS;
}

/* ---------- main compilation pipeline ---------- */

cephyr_result cephyr_compile(const cephyr_options *opts)
{
    cephyr_result result = CEPHYR_SUCCESS;
    char *source_text = NULL;
    size_t source_len = 0;
    char *preprocessed = NULL;
    char *error_msg = NULL;
    cephyr_cpp_result cpp_res;
    memset(&cpp_res, 0, sizeof(cpp_res));

    /* Step 1: Read source file */
    source_text = read_file(opts->source_path, &source_len);
    if (!source_text) {
        fprintf(stderr, "cephyr: error: cannot read source file '%s'\n", opts->source_path);
        return CEPHYR_ERR_INTERNAL;
    }

    /* Step 2: Preprocess */
    if (opts->cpp_command) {
        /* Use external preprocessor */
        preprocessed = cephyr_cpp_external(opts->source_path, opts->cpp_command, &error_msg);
        if (!preprocessed) {
            fprintf(stderr, "cephyr: preprocessor error: %s\n",
                    error_msg ? error_msg : "unknown error");
            free(error_msg);
            free(source_text);
            return CEPHYR_ERR_PREPROCESSOR;
        }
        cpp_res.text = preprocessed;
        cpp_res.text_len = strlen(preprocessed);
    } else {
        /* Use ucpp */
        cpp_res = cephyr_cpp_preprocess(source_text, source_len, opts->source_path,
                                        opts->include_paths, opts->include_path_count);
        if (cpp_res.error_message) {
            fprintf(stderr, "cephyr: preprocessor error: %s\n", cpp_res.error_message);
            cephyr_cpp_result_free(&cpp_res);
            free(source_text);
            return CEPHYR_ERR_PREPROCESSOR;
        }
    }

    /* Step 3: Parse with Swaff C frontend */
    if (!ccw_swaff_available()) {
        fprintf(stderr, "cephyr: error: Swaff C frontend not available\n");
        cephyr_cpp_result_free(&cpp_res);
        free(source_text);
        return CEPHYR_ERR_PARSE;
    }

    const ccw_swaff_frontend *fe = ccw_swaff_frontend_c();
    ccw_swaff_report report;
    memset(&report, 0, sizeof(report));

    ccw_ir *ir = ccw_swaff_lower(fe, cpp_res.text, cpp_res.text_len,
                                 opts->source_path, CCW_PROFILE_TILLY,
                                 CCW_SWAFF_RECOVER_ON_ERROR, &report, &error_msg);

    if (!ir) {
        fprintf(stderr, "cephyr: parse error: %s\n",
                error_msg ? error_msg : "unknown error");
        fprintf(stderr, "  Swaff report: %d errors, %d missing, %d recovered, %d unsupported\n",
                report.error_nodes, report.missing_nodes,
                report.recovered_subtrees, report.unsupported_nodes);
        free(error_msg);
        cephyr_cpp_result_free(&cpp_res);
        free(source_text);
        return CEPHYR_ERR_PARSE;
    }

    /* Step 4: Semantic analysis
     * In v0.1, the Swaff C adapter already produces Weave IR directly.
     * The sema layer operates on a typed AST; for v0.1 we validate
     * the IR and report diagnostics. The full typed-AST pipeline is
     * staged for v0.2, when the Swaff→IR lowering is completed. */
    char *validate_err = NULL;
    ccw_status validate_rc = ccw_ir_validate(ir, &validate_err);
    if (validate_rc != CCW_OK) {
        fprintf(stderr, "cephyr: IR validation error: %s\n",
                validate_err ? validate_err : "unknown");
        free(validate_err);
        ccw_ir_module_destroy(ir);
        cephyr_cpp_result_free(&cpp_res);
        free(source_text);
        return CEPHYR_ERR_SEMA;
    }

    /* Step 5: Run the Sched plan */
    result = run_sched_plan(ir, opts);

    /* Step 6: Output
     * In v0.1, we emit the IR as text. Assembly emission is deferred
     * to v0.2 when the codegen kernels are fully implemented. */
    if (result == CEPHYR_SUCCESS) {
        if (opts->output_path) {
            FILE *out = fopen(opts->output_path, "w");
            if (out) {
                char *ir_text = ccw_ir_print(ir);
                if (ir_text) {
                    fprintf(out, "%s\n", ir_text);
                    free(ir_text);
                }
                fclose(out);
            } else {
                fprintf(stderr, "cephyr: error: cannot write to '%s'\n", opts->output_path);
                result = CEPHYR_ERR_INTERNAL;
            }
        }
    }

    /* Cleanup */
    ccw_ir_module_destroy(ir);
    cephyr_cpp_result_free(&cpp_res);
    free(source_text);

    return result;
}
