/* Cephyr driver — §8.
 *
 * CLI entry point, toolchain discovery, plugin loading, and Sched plan
 * orchestration. Ties together: preprocessor → Swaff C frontend →
 * sema → lowering → Sched plan execution. */

#define _POSIX_C_SOURCE 200809L

#include "cephyr_driver.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../cpp/cephyr_cpp.h"
#include "../sema/cephyr_ast.h"
#include "../sema/cephyr_sema.h"
#include "../lower/cephyr_lower.h"
#include "../include/cephyr-module.h"
#include "../stdmodule/cephyr_stdmodule.h"
#include "../profile/cephyr_profile.h"

#include "../../../swaff/ccw_swaff.h"
#include "../../../ir/ccw_ir.h"
#include "../../../sched/sched.h"

static char *cephyr_driver_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1u;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

/* ---------- default options ---------- */

void cephyr_options_init(cephyr_options *opts, const char *source_path)
{
    memset(opts, 0, sizeof(*opts));
    opts->source_path = source_path;
    opts->opt_level = CEPHYR_O0;
    opts->target_triple = "x86_64-linux-gnu";
    opts->manifest_dir = "manifests";
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
                    return cephyr_driver_strdup(buf);
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
                    return cephyr_driver_strdup(buf);
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

/* ---------- profile and Sched helpers ---------- */

static int profile_scalar_safe(const char *value)
{
    if (value == NULL || *value == '\0') return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r')
            return 0;
    return 1;
}

static char *profile_path_dirname(const char *path)
{
    char *copy;
    char *slash;
    if (path == NULL) return cephyr_driver_strdup(".");
    copy = cephyr_driver_strdup(path);
    if (copy == NULL) return NULL;
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        strcpy(copy, ".");
    } else if (slash == copy) {
        copy[1] = '\0';
    } else {
        *slash = '\0';
    }
    return copy;
}

static char *profile_resolve_path(const char *profile_path, const char *value)
{
    char *dir;
    char *result;
    size_t n;
    if (value == NULL) return NULL;
    if (value[0] == '/') return cephyr_driver_strdup(value);
    dir = profile_path_dirname(profile_path);
    if (dir == NULL) return NULL;
    n = strlen(dir) + strlen(value) + 2u;
    result = (char *)malloc(n);
    if (result != NULL) snprintf(result, n, "%s/%s", dir, value);
    free(dir);
    return result;
}

static int profile_opt_level(const char *value, cephyr_opt_level *out)
{
    if (value == NULL || out == NULL) return 0;
    if (!strcmp(value, "O0") || !strcmp(value, "0")) *out = CEPHYR_O0;
    else if (!strcmp(value, "O1") || !strcmp(value, "1")) *out = CEPHYR_O1;
    else if (!strcmp(value, "O2") || !strcmp(value, "2")) *out = CEPHYR_O2;
    else return 0;
    return 1;
}

static int profile_has_explicit_plan(const cephyr_profile *profile)
{
    return profile != NULL &&
           (profile->kernel_count != 0 || profile->rewrite_count != 0);
}

static int merge_string_lists(const char *const *first, size_t first_count,
                              const char *const *second, size_t second_count,
                              const char ***out, int *out_count)
{
    size_t total = first_count + second_count;
    const char **merged;
    if (out == NULL || out_count == NULL ||
        total > (size_t)INT_MAX) return 0;
    *out = NULL;
    *out_count = 0;
    if (total == 0) return 1;
    merged = (const char **)malloc(total * sizeof(*merged));
    if (merged == NULL) return 0;
    for (size_t i = 0; i < first_count; ++i) merged[i] = first[i];
    for (size_t i = 0; i < second_count; ++i)
        merged[first_count + i] = second[i];
    *out = merged;
    *out_count = (int)total;
    return 1;
}

static int write_explicit_sched_script(const cephyr_profile *profile,
                                       char **path_out,
                                       char *error_message,
                                       size_t error_capacity)
{
    char template_path[] = "/tmp/cephyr-profile-XXXXXX";
    int fd;
    FILE *file;
    unsigned node_number = 0;
    unsigned previous = 0;

    if (profile == NULL || path_out == NULL) return 0;
    *path_out = NULL;
    fd = mkstemp(template_path);
    if (fd < 0) {
        snprintf(error_message, error_capacity,
                 "cannot create temporary Sched script: %s", strerror(errno));
        return 0;
    }
    file = fdopen(fd, "w");
    if (file == NULL) {
        close(fd);
        unlink(template_path);
        snprintf(error_message, error_capacity,
                 "cannot open temporary Sched script: %s", strerror(errno));
        return 0;
    }
    fputs("local S = sched.new \"Cephyr-profile\"\n", file);

    for (size_t i = 0; i < profile->kernel_count; ++i) {
        const cephyr_profile_kernel *kernel = &profile->kernels[i];
        char variable[32];
        if ((kernel->name == NULL) == (kernel->capability == NULL) ||
            !profile_scalar_safe(kernel->name ? kernel->name : kernel->capability) ||
            (kernel->prefer != NULL && !profile_scalar_safe(kernel->prefer))) {
            snprintf(error_message, error_capacity,
                     "each profile kernel needs exactly one safe name or capability");
            fclose(file);
            unlink(template_path);
            return 0;
        }
        snprintf(variable, sizeof(variable), "n%u", node_number++);
        if (kernel->name != NULL)
            fprintf(file, "local %s = S:require { kernel = \"%s\" }\n",
                    variable, kernel->name);
        else if (kernel->prefer != NULL)
            fprintf(file, "local %s = S:require { capability = \"%s\", prefer = \"%s\" }\n",
                    variable, kernel->capability, kernel->prefer);
        else
            fprintf(file, "local %s = S:require { capability = \"%s\" }\n",
                    variable, kernel->capability);
        if (previous != 0)
            fprintf(file, "S:edge(n%u, %s)\n", previous - 1u, variable);
        previous = node_number;
    }

    for (size_t i = 0; i < profile->rewrite_count; ++i) {
        char variable[32];
        const char *pattern = profile->rewrites[i];
        if (!profile_scalar_safe(pattern)) {
            snprintf(error_message, error_capacity,
                     "profile rewrite patterns must be scalar strings");
            fclose(file);
            unlink(template_path);
            return 0;
        }
        snprintf(variable, sizeof(variable), "n%u", node_number++);
        fprintf(file, "local %s = S:rewrite \"%s\"\n", variable, pattern);
        if (previous != 0)
            fprintf(file, "S:edge(n%u, %s)\n", previous - 1u, variable);
        previous = node_number;
    }
    if (node_number == 0) {
        snprintf(error_message, error_capacity,
                 "explicit profile must mention a kernel or rewrite");
        fclose(file);
        unlink(template_path);
        return 0;
    }
    fputs("return S:seal()\n", file);
    if (fclose(file) != 0) {
        snprintf(error_message, error_capacity,
                 "cannot finish temporary Sched script");
        unlink(template_path);
        return 0;
    }
    *path_out = cephyr_driver_strdup(template_path);
    if (*path_out == NULL) {
        unlink(template_path);
        snprintf(error_message, error_capacity, "out of memory creating Sched script");
        return 0;
    }
    return 1;
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

static cephyr_result write_stage_text(const char *path, const char *text)
{
    FILE *out;
    if (path == NULL || strcmp(path, "-") == 0)
        return fputs(text ? text : "", stdout) < 0 ?
               CEPHYR_ERR_INTERNAL : CEPHYR_SUCCESS;
    out = fopen(path, "w");
    if (out == NULL) {
        fprintf(stderr, "cephyr: error: cannot write to '%s'\n", path);
        return CEPHYR_ERR_INTERNAL;
    }
    if (fputs(text ? text : "", out) < 0 || fclose(out) != 0) {
        fprintf(stderr, "cephyr: error: cannot write to '%s'\n", path);
        return CEPHYR_ERR_INTERNAL;
    }
    return CEPHYR_SUCCESS;
}

/* ---------- Sched plan execution ---------- */

static cephyr_result run_sched_plan(ccw_ir *ir, const cephyr_options *opts)
{
    /* Determine the Sched script path */
    const char *script_name = NULL;
    const char *manifest_dir = opts->manifest_dir ? opts->manifest_dir : "manifests";
    switch (opts->opt_level) {
    case CEPHYR_O0: script_name = "O0.lua"; break;
    case CEPHYR_O1: script_name = "O1.lua"; break;
    case CEPHYR_O2: script_name = "O2.lua"; break;
    }

    /* Build the full path to the script */
    char script_path[1024];
    if (opts->sched_script != NULL) {
        snprintf(script_path, sizeof(script_path), "%s", opts->sched_script);
    } else {
        snprintf(script_path, sizeof(script_path),
                 "compilers/cephyr/sched/%s", script_name);
    }

    /* Try to find the script */
    FILE *test = fopen(script_path, "r");
    if (!test) {
        if (opts->sched_script == NULL) {
            snprintf(script_path, sizeof(script_path),
                     "../compilers/cephyr/sched/%s", script_name);
            test = fopen(script_path, "r");
        }
    }
    if (!test) {
        fprintf(stderr, "cephyr: error: cannot find Sched script '%s'\n",
                opts->sched_script ? opts->sched_script : script_name);
        return CEPHYR_ERR_SCHED;
    }
    fclose(test);

    /* Load and run the Sched script */
    ccw_sched_error err;
    memset(&err, 0, sizeof(err));
    ccw_plan *plan = NULL;
    int rc = ccw_sched_run_script(script_path, manifest_dir, &plan, &err);
    if (rc == 0) {
        fprintf(stderr, "cephyr: sched error: %s\n", err.message);
        return CEPHYR_ERR_SCHED;
    }

    if (plan) {
        ccw_oeuph_budget budget = ccw_oeuph_default_budget();
        if (!ccw_plan_apply_rewrites(plan, ir, manifest_dir, budget,
                                     CCW_COST_PERFORMANCE, NULL, 0, NULL,
                                     &err)) {
            fprintf(stderr, "cephyr: rewrite error: %s\n", err.message);
            ccw_plan_free(plan);
            return CEPHYR_ERR_SCHED;
        }
        /* Write the plan for debugging */
        const char *plan_name = opts->sched_script ? "profile" : script_name;
        char plan_path[1024];
        snprintf(plan_path, sizeof(plan_path),
                 "compilers/cephyr/sched/plans/%s.plan", plan_name);
        ccw_plan_write(plan, plan_path, &err);
        ccw_plan_free(plan);
    }

    /* Kernel nodes remain host/executor responsibilities; rewrite nodes are
     * applied above through Oeuph. */
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

static cephyr_result cephyr_compile_inner(const cephyr_options *opts)
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
        preprocessed = cephyr_cpp_external_with_options(
            opts->source_path, opts->cpp_command,
            opts->preprocessor_options, opts->preprocessor_option_count,
            opts->preprocessor_args, opts->preprocessor_arg_count,
            &error_msg);
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
        cpp_res = cephyr_cpp_preprocess_with_options(
            source_text, source_len, opts->source_path,
            opts->include_paths, opts->include_path_count,
            opts->defines, opts->define_count,
            opts->preprocessor_options, opts->preprocessor_option_count,
            opts->preprocessor_args, opts->preprocessor_arg_count);
        if (cpp_res.error_message) {
            fprintf(stderr, "cephyr: preprocessor error: %s\n", cpp_res.error_message);
            cephyr_cpp_result_free(&cpp_res);
            free(source_text);
            return CEPHYR_ERR_PREPROCESSOR;
        }
    }

    if (opts->stop_stage == CEPHYR_STOP_PREPROCESS) {
        result = write_stage_text(opts->output_path, cpp_res.text);
        cephyr_cpp_result_free(&cpp_res);
        free(source_text);
        return result;
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
    if (result == CEPHYR_SUCCESS &&
        (opts->output_path != NULL ||
         opts->stop_stage == CEPHYR_STOP_ASSEMBLER_SCRIPT)) {
        char *ir_text = ccw_ir_print(ir);
        if (ir_text == NULL) {
            result = CEPHYR_ERR_INTERNAL;
        } else {
            size_t n = strlen(ir_text);
            char *with_newline = (char *)malloc(n + 2u);
            if (with_newline == NULL) {
                result = CEPHYR_ERR_INTERNAL;
            } else {
                memcpy(with_newline, ir_text, n);
                with_newline[n] = '\n';
                with_newline[n + 1u] = '\0';
                result = write_stage_text(opts->output_path, with_newline);
                free(with_newline);
            }
            free(ir_text);
        }
    }

    /* Cleanup */
    ccw_ir_module_destroy(ir);
    cephyr_cpp_result_free(&cpp_res);
    free(source_text);

    return result;
}

cephyr_result cephyr_compile(const cephyr_options *opts)
{
    cephyr_profile profile;
    cephyr_options effective;
    char profile_error_message[512] = {0};
    char *profile_path = NULL;
    char *resolved_manifest = NULL;
    char *resolved_sched = NULL;
    char *generated_sched = NULL;
    const char **include_paths = NULL;
    const char **defines = NULL;
    const char **preprocessor_options = NULL;
    const char **preprocessor_args = NULL;
    const char **assembler_options = NULL;
    const char **assembler_args = NULL;
    const char **linker_options = NULL;
    const char **linker_args = NULL;
    const char **library_paths = NULL;
    const char **libraries = NULL;
    size_t include_count = 0;
    size_t define_count = 0;
    int preprocessor_option_count = 0;
    int preprocessor_arg_count = 0;
    int assembler_option_count = 0;
    int assembler_arg_count = 0;
    int linker_option_count = 0;
    int linker_arg_count = 0;
    int library_path_count = 0;
    int library_count = 0;
    cephyr_result result;

    if (opts == NULL || opts->source_path == NULL) {
        fprintf(stderr, "cephyr: invalid compiler options\n");
        return CEPHYR_ERR_INTERNAL;
    }
    memset(&profile, 0, sizeof(profile));
    if (opts->profile_path != NULL) {
        profile_path = cephyr_driver_strdup(opts->profile_path);
        if (profile_path == NULL ||
            !cephyr_profile_load(profile_path, &profile,
                                 profile_error_message,
                                 sizeof(profile_error_message))) {
            fprintf(stderr, "cephyr: profile error: %s\n",
                    profile_error_message[0] ? profile_error_message :
                    "cannot load profile");
            free(profile_path);
            return CEPHYR_ERR_INTERNAL;
        }
    } else {
        profile_path = cephyr_profile_discover(
            ".", profile_error_message, sizeof(profile_error_message));
        if (profile_path != NULL) {
            if (!cephyr_profile_load(profile_path, &profile,
                                     profile_error_message,
                                     sizeof(profile_error_message))) {
                fprintf(stderr, "cephyr: profile error: %s\n",
                        profile_error_message);
                free(profile_path);
                return CEPHYR_ERR_INTERNAL;
            }
        } else {
            cephyr_profile_init(&profile);
        }
    }

    if (profile.sched_script != NULL && profile_has_explicit_plan(&profile)) {
        fprintf(stderr,
                "cephyr: profile must choose sched_script or explicit kernels/rewrites\n");
        cephyr_profile_destroy(&profile);
        free(profile_path);
        return CEPHYR_ERR_SCHED;
    }

    effective = *opts;
    if (!opts->opt_level_explicit) {
        if (!profile_opt_level(profile.opt_level, &effective.opt_level)) {
            fprintf(stderr, "cephyr: profile has invalid opt_level '%s'\n",
                    profile.opt_level ? profile.opt_level : "");
            cephyr_profile_destroy(&profile);
            free(profile_path);
            return CEPHYR_ERR_INTERNAL;
        }
    }
    if (!opts->target_explicit && profile.target_triple != NULL)
        effective.target_triple = profile.target_triple;
    if (!opts->cpp_explicit) {
        if (profile.preprocessor != NULL &&
            strcmp(profile.preprocessor, "ucpp") != 0 &&
            profile.preprocessor[0] != '\0')
            effective.cpp_command = profile.preprocessor;
        else
            effective.cpp_command = NULL;
    }
    if (opts->assembler == NULL && profile.assembler != NULL)
        effective.assembler = profile.assembler;
    if (opts->linker == NULL && profile.linker != NULL)
        effective.linker = profile.linker;
    if (!opts->pic_explicit) effective.pic = profile.pic;
    if (!opts->pie_explicit) effective.pie = profile.pie;
    if (!opts->shared_explicit) effective.shared = profile.shared;
    if (!opts->manifest_explicit && profile.manifest_dir != NULL) {
        resolved_manifest = profile_resolve_path(profile_path,
                                                 profile.manifest_dir);
        if (resolved_manifest == NULL) {
            fprintf(stderr, "cephyr: out of memory resolving manifest_dir\n");
            cephyr_profile_destroy(&profile);
            free(profile_path);
            return CEPHYR_ERR_INTERNAL;
        }
        effective.manifest_dir = resolved_manifest;
    }

    include_count = profile.include_path_count +
                    (size_t)(opts->include_path_count > 0 ?
                             opts->include_path_count : 0);
    define_count = profile.define_count +
                   (size_t)(opts->define_count > 0 ? opts->define_count : 0);
    if (include_count != 0) {
        include_paths = (const char **)malloc(include_count *
                                               sizeof(*include_paths));
        if (include_paths == NULL) goto oom;
        for (size_t i = 0; i < profile.include_path_count; ++i)
            include_paths[i] = profile.include_paths[i];
        for (size_t i = 0; i < (size_t)opts->include_path_count; ++i)
            include_paths[profile.include_path_count + i] =
                opts->include_paths[i];
        effective.include_paths = include_paths;
        effective.include_path_count = (int)include_count;
    }
    if (define_count != 0) {
        defines = (const char **)malloc(define_count * sizeof(*defines));
        if (defines == NULL) goto oom;
        for (size_t i = 0; i < profile.define_count; ++i)
            defines[i] = profile.defines[i];
        for (size_t i = 0; i < (size_t)opts->define_count; ++i)
            defines[profile.define_count + i] = opts->defines[i];
        effective.defines = defines;
        effective.define_count = (int)define_count;
    }
    if (!merge_string_lists((const char *const *)profile.preprocessor_options,
                            profile.preprocessor_option_count,
                            opts->preprocessor_options,
                            (size_t)(opts->preprocessor_option_count > 0 ?
                                     opts->preprocessor_option_count : 0),
                            &preprocessor_options,
                            &preprocessor_option_count) ||
        !merge_string_lists((const char *const *)profile.preprocessor_args,
                            profile.preprocessor_arg_count,
                            opts->preprocessor_args,
                            (size_t)(opts->preprocessor_arg_count > 0 ?
                                     opts->preprocessor_arg_count : 0),
                            &preprocessor_args, &preprocessor_arg_count) ||
        !merge_string_lists((const char *const *)profile.assembler_options,
                            profile.assembler_option_count,
                            opts->assembler_options,
                            (size_t)(opts->assembler_option_count > 0 ?
                                     opts->assembler_option_count : 0),
                            &assembler_options, &assembler_option_count) ||
        !merge_string_lists((const char *const *)profile.assembler_args,
                            profile.assembler_arg_count,
                            opts->assembler_args,
                            (size_t)(opts->assembler_arg_count > 0 ?
                                     opts->assembler_arg_count : 0),
                            &assembler_args, &assembler_arg_count) ||
        !merge_string_lists((const char *const *)profile.linker_options,
                            profile.linker_option_count,
                            opts->linker_options,
                            (size_t)(opts->linker_option_count > 0 ?
                                     opts->linker_option_count : 0),
                            &linker_options, &linker_option_count) ||
        !merge_string_lists((const char *const *)profile.linker_args,
                            profile.linker_arg_count,
                            opts->linker_args,
                            (size_t)(opts->linker_arg_count > 0 ?
                                     opts->linker_arg_count : 0),
                            &linker_args, &linker_arg_count) ||
        !merge_string_lists((const char *const *)profile.library_paths,
                            profile.library_path_count,
                            opts->library_paths,
                            (size_t)(opts->library_path_count > 0 ?
                                     opts->library_path_count : 0),
                            &library_paths, &library_path_count) ||
        !merge_string_lists((const char *const *)profile.libraries,
                            profile.library_count,
                            opts->libraries,
                            (size_t)(opts->library_count > 0 ?
                                     opts->library_count : 0),
                            &libraries, &library_count)) {
        goto oom;
    }
    effective.preprocessor_options = preprocessor_options;
    effective.preprocessor_option_count = preprocessor_option_count;
    effective.preprocessor_args = preprocessor_args;
    effective.preprocessor_arg_count = preprocessor_arg_count;
    effective.assembler_options = assembler_options;
    effective.assembler_option_count = assembler_option_count;
    effective.assembler_args = assembler_args;
    effective.assembler_arg_count = assembler_arg_count;
    effective.linker_options = linker_options;
    effective.linker_option_count = linker_option_count;
    effective.linker_args = linker_args;
    effective.linker_arg_count = linker_arg_count;
    effective.library_paths = library_paths;
    effective.library_path_count = library_path_count;
    effective.libraries = libraries;
    effective.library_count = library_count;

    if (opts->sched_script != NULL) {
        effective.sched_script = opts->sched_script;
    } else if (profile.sched_script != NULL) {
        resolved_sched = profile_resolve_path(profile_path,
                                              profile.sched_script);
        if (resolved_sched == NULL) goto oom;
        effective.sched_script = resolved_sched;
    } else if (profile_has_explicit_plan(&profile)) {
        if (!write_explicit_sched_script(&profile, &generated_sched,
                                         profile_error_message,
                                         sizeof(profile_error_message))) {
            fprintf(stderr, "cephyr: profile error: %s\n",
                    profile_error_message);
            result = CEPHYR_ERR_SCHED;
            goto cleanup;
        }
        effective.sched_script = generated_sched;
    }

    result = cephyr_compile_inner(&effective);
    goto cleanup;
oom:
    fprintf(stderr, "cephyr: out of memory preparing profile\n");
    result = CEPHYR_ERR_INTERNAL;
cleanup:
    if (generated_sched != NULL) unlink(generated_sched);
    free(generated_sched);
    free(resolved_sched);
    free(resolved_manifest);
    free(include_paths);
    free(defines);
    free(preprocessor_options);
    free(preprocessor_args);
    free(assembler_options);
    free(assembler_args);
    free(linker_options);
    free(linker_args);
    free(library_paths);
    free(libraries);
    cephyr_profile_destroy(&profile);
    free(profile_path);
    return result;
}
