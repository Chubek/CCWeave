/* Parthia's deterministic module boundary.
 *
 * This deliberately consumes only Swaff's language-neutral surface AST.
 * Tree-sitter types and grammar details do not cross into the elaborator.
 * The current core representation is a compact, typed-fact carrier: module
 * signatures are erased after their constraints are recorded, and functor
 * applications receive names derived from their lexical application path.
 */

#include "sml_parthia.h"
#include "kstring.h"
#include "sched.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

struct ccw_sml_parthia_program {
    char *surface_ast;
    char *core_ast;
    ccw_sml_parthia_report report;
};

typedef struct sml_ext_entry {
    char *name;
    ccw_sml_native_fn invoke;
    void *userdata;
    void *handle;
    struct sml_ext_entry *next;
} sml_ext_entry;
struct ccw_sml_parthia_runtime { sml_ext_entry *extensions; };

static char *dup_text(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length + 1u);
    return copy;
}

static void set_error(char **error_message, const char *message)
{
    if (error_message != NULL) *error_message = dup_text(message);
}

int ccw_sml_parthia_load_plan(const char *level,
                              const char *manifest_dir,
                              const char *sched_dir,
                              ccw_plan **out,
                              char **error_message)
{
    const char *chosen = level != NULL ? level : "O2";
    const char *root = sched_dir != NULL ? sched_dir :
                       "interpreters/sml-parthia/sched";
    char path[1024];
    ccw_sched_error error = {0};

    if (out != NULL) *out = NULL;
    if (error_message != NULL) *error_message = NULL;
    if (strcmp(chosen, "O0") != 0 && strcmp(chosen, "O1") != 0 &&
        strcmp(chosen, "O2") != 0) {
        set_error(error_message, "sml/parthia: scheduler level must be O0, O1, or O2");
        return 0;
    }
    if (out == NULL) {
        set_error(error_message, "sml/parthia: plan output is required");
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s.lua", root, chosen);
    if (!ccw_sched_run_script(path, manifest_dir ? manifest_dir : "manifests",
                              out, &error)) {
        set_error(error_message, error.message);
        return 0;
    }
    return 1;
}

static bool append(kstring_t *out, const char *text)
{
    return text != NULL && kputs(text, out) != EOF;
}

static int count_tag(const char *ast, const char *tag)
{
    int count = 0;
    size_t tag_length;
    const char *cursor;
    if (ast == NULL || tag == NULL) return 0;
    tag_length = strlen(tag);
    cursor = ast;
    while ((cursor = strstr(cursor, tag)) != NULL) {
        bool boundary_before = cursor == ast || cursor[-1] == '(' ||
                               cursor[-1] == ' ';
        bool boundary_after = cursor[tag_length] == '\0' ||
                              cursor[tag_length] == ')' ||
                              cursor[tag_length] == ' ';
        if (boundary_before && boundary_after) count++;
        cursor += tag_length;
    }
    return count;
}

static int compare_names(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

static char **collect_functor_paths(const char *surface, int *count)
{
    const char *cursor = surface;
    char **names = NULL;
    int used = 0;
    while (cursor != NULL && (cursor = strstr(cursor, "(fctapp ")) != NULL) {
        const char *start = cursor + strlen("(fctapp ");
        const char *end = start;
        char *name;
        while (*end != '\0' && *end != ' ' && *end != ')' && *end != '(')
            end++;
        if (end == start) {
            cursor = end;
            continue;
        }
        name = (char *)malloc((size_t)(end - start) + 1u);
        if (name == NULL) break;
        memcpy(name, start, (size_t)(end - start));
        name[end - start] = '\0';
        {
            char **grown = (char **)realloc(names,
                                             (size_t)(used + 1) *
                                                 sizeof(*names));
            if (grown == NULL) {
                free(name);
                break;
            }
            names = grown;
        }
        names[used++] = name;
        cursor = end;
    }
    qsort(names, (size_t)used, sizeof(*names), compare_names);
    *count = used;
    return names;
}

static bool emit_core_facts(const char *surface, const ccw_sml_parse_report *parse,
                            kstring_t *out, ccw_sml_parthia_report *report)
{
    char line[160];
    int path_count = 0;
    char **paths = collect_functor_paths(surface, &path_count);
#define CORE_FAIL() do { \
        for (int core_i = 0; core_i < path_count; core_i++) free(paths[core_i]); \
        free(paths); \
        return false; \
    } while (0)
    if (!append(out, "(core-ml (modules")) CORE_FAIL();
    if (parse->structure_count > 0) {
        snprintf(line, sizeof(line), " (structures %d)", parse->structure_count);
        if (!append(out, line)) CORE_FAIL();
    }
    if (parse->signature_count > 0) {
        snprintf(line, sizeof(line), " (signatures %d)", parse->signature_count);
        if (!append(out, line)) CORE_FAIL();
    }
    if (parse->sharing_count > 0) {
        snprintf(line, sizeof(line), " (sharing-constraints %d)",
                 parse->sharing_count);
        if (!append(out, line)) CORE_FAIL();
    }
    if (parse->wheretype_count > 0) {
        snprintf(line, sizeof(line), " (where-type-constraints %d)",
                 parse->wheretype_count);
        if (!append(out, line)) CORE_FAIL();
    }
    if (!append(out, ") (functors")) CORE_FAIL();
    for (int i = 0; i < path_count; i++) {
        /* The path is sorted before emission.  Duplicate applications receive
         * a stable suffix only to keep their serialized names distinct. */
        int duplicate = 0;
        for (int j = 0; j < i; j++)
            if (strcmp(paths[j], paths[i]) == 0) duplicate++;
        if (duplicate == 0)
            snprintf(line, sizeof(line), " (instance fct.%s)", paths[i]);
        else
            snprintf(line, sizeof(line), " (instance fct.%s.%d)",
                     paths[i], duplicate);
        if (!append(out, line)) CORE_FAIL();
    }
    if (!append(out, ") (typed-facts")) CORE_FAIL();
    if (count_tag(surface, "(infix") > 0 &&
        !append(out, " (fixity-resolved true)"))
        CORE_FAIL();
    if (!append(out, ")))")) CORE_FAIL();

    report->module_facts = parse->structure_count + parse->signature_count +
                           parse->sharing_count + parse->wheretype_count;
    report->functor_instances = path_count;
    report->value_facts = count_tag(surface, "(val");
    report->type_facts = count_tag(surface, "(type");
    for (int i = 0; i < path_count; i++) free(paths[i]);
    free(paths);
#undef CORE_FAIL
    return true;
}

ccw_sml_parthia_program *ccw_sml_parthia_compile(
    const char *source, size_t source_len,
    ccw_sml_parthia_report *report, char **error_message)
{
    ccw_sml_parthia_program *program;
    ccw_sml_parse_report parse;
    char *surface;
    char *parse_error = NULL;
    kstring_t core = { 0, 0, NULL };

    if (error_message != NULL) *error_message = NULL;
    if (report != NULL) memset(report, 0, sizeof(*report));
    surface = ccw_swaff_parse_sml(source, source_len, &parse, &parse_error);
    if (surface == NULL) {
        if (report != NULL) report->parse = parse;
        if (error_message != NULL) *error_message = parse_error;
        else free(parse_error);
        return NULL;
    }
    free(parse_error);

    program = (ccw_sml_parthia_program *)calloc(1, sizeof(*program));
    if (program == NULL) {
        free(surface);
        set_error(error_message, "sml/parthia: out of memory");
        return NULL;
    }
    program->surface_ast = surface;
    program->report.parse = parse;
    if (!emit_core_facts(surface, &parse, &core, &program->report)) {
        ccw_sml_parthia_program_destroy(program);
        free(core.s);
        set_error(error_message, "sml/parthia: out of memory");
        return NULL;
    }
    program->core_ast = ks_release(&core);
    if (program->core_ast == NULL) {
        ccw_sml_parthia_program_destroy(program);
        set_error(error_message, "sml/parthia: out of memory");
        return NULL;
    }
    if (report != NULL) *report = program->report;
    return program;
}

void ccw_sml_parthia_program_destroy(ccw_sml_parthia_program *program)
{
    if (program == NULL) return;
    free(program->surface_ast);
    free(program->core_ast);
    free(program);
}

const char *ccw_sml_parthia_surface_ast(
    const ccw_sml_parthia_program *program)
{
    return program == NULL ? NULL : program->surface_ast;
}

const char *ccw_sml_parthia_core_ast(
    const ccw_sml_parthia_program *program)
{
    return program == NULL ? NULL : program->core_ast;
}

int ccw_sml_parthia_structure_count(
    const ccw_sml_parthia_program *program)
{
    return program == NULL ? 0 : program->report.parse.structure_count;
}

int ccw_sml_parthia_signature_count(
    const ccw_sml_parthia_program *program)
{
    return program == NULL ? 0 : program->report.parse.signature_count;
}

int ccw_sml_parthia_functor_count(
    const ccw_sml_parthia_program *program)
{
    return program == NULL ? 0 : program->report.parse.functor_count;
}

int ccw_sml_parthia_sharing_count(
    const ccw_sml_parthia_program *program)
{
    return program == NULL ? 0 : program->report.parse.sharing_count;
}

int ccw_sml_parthia_wheretype_count(
    const ccw_sml_parthia_program *program)
{
    return program == NULL ? 0 : program->report.parse.wheretype_count;
}

ccw_sml_parthia_runtime *ccw_sml_parthia_runtime_new(void)
{
    return (ccw_sml_parthia_runtime *)calloc(1, sizeof(ccw_sml_parthia_runtime));
}
void ccw_sml_parthia_runtime_free(ccw_sml_parthia_runtime *runtime)
{
    sml_ext_entry *e, *n;
    if (!runtime) return;
    for (e = runtime->extensions; e; e = n) {
        n = e->next; free(e->name);
#if defined(__unix__) || defined(__APPLE__)
        if (e->handle) dlclose(e->handle);
#endif
        free(e);
    }
    free(runtime);
}
int ccw_sml_parthia_register_extension(ccw_sml_parthia_runtime *runtime,
                                       const ccw_sml_extension *extension)
{
    sml_ext_entry *entry;
    if (!runtime || !extension || !extension->name || !*extension->name ||
        !extension->invoke) return 0;
    entry = (sml_ext_entry *)calloc(1, sizeof(*entry));
    if (!entry) return 0;
    entry->name = dup_text(extension->name);
    if (!entry->name) { free(entry); return 0; }
    entry->invoke = extension->invoke; entry->userdata = extension->userdata;
    entry->next = runtime->extensions; runtime->extensions = entry;
    return 1;
}
int ccw_sml_parthia_call_native(ccw_sml_parthia_runtime *runtime,
                                const char *name, const ccw_sml_value *args,
                                size_t nargs, ccw_sml_value *results,
                                size_t nresults)
{
    sml_ext_entry *e;
    if (!runtime || !name || (nargs && !args) || (nresults && !results)) return 0;
    for (e = runtime->extensions; e; e = e->next)
        if (strcmp(e->name, name) == 0)
            return e->invoke(args, nargs, results, nresults, e->userdata) == 0;
    return 0;
}
int ccw_sml_parthia_load_extension(ccw_sml_parthia_runtime *runtime,
                                   const char *path)
{
#if defined(__unix__) || defined(__APPLE__)
    void *handle;
    const ccw_sml_extension *(*init_fn)(void);
    const ccw_sml_extension *extension;
    if (!runtime || !path) return 0;
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return 0;
    *(void **)(&init_fn) = dlsym(handle, "ccw_sml_parthia_extension_init");
    extension = init_fn ? init_fn() : NULL;
    if (!extension || !ccw_sml_parthia_register_extension(runtime, extension)) {
        dlclose(handle); return 0;
    }
    runtime->extensions->handle = handle;
    return 1;
#else
    (void)runtime; (void)path; return 0;
#endif
}

ccw_sml_ffi_library ccw_sml_parthia_ffi_open(const char *path)
{
#if defined(__unix__) || defined(__APPLE__)
    return dlopen(path ? path : NULL, RTLD_NOW | RTLD_LOCAL);
#else
    (void)path; return NULL;
#endif
}
void *ccw_sml_parthia_ffi_symbol(ccw_sml_ffi_library library, const char *name)
{
#if defined(__unix__) || defined(__APPLE__)
    return library && name ? dlsym(library, name) : NULL;
#else
    (void)library; (void)name; return NULL;
#endif
}
void ccw_sml_parthia_ffi_close(ccw_sml_ffi_library library)
{
#if defined(__unix__) || defined(__APPLE__)
    if (library) dlclose(library);
#else
    (void)library;
#endif
}
int ccw_sml_parthia_ffi_call_i64(void *symbol, const long long *args,
                                 size_t nargs, long long *result)
{
    if (!symbol || !result || nargs > 3 || (nargs && !args)) return 0;
    if (nargs == 0) *result = ((long long (*)(void))symbol)();
    else if (nargs == 1) *result = ((long long (*)(long long))symbol)(args[0]);
    else if (nargs == 2) *result = ((long long (*)(long long,long long))symbol)(args[0],args[1]);
    else *result = ((long long (*)(long long,long long,long long))symbol)(args[0],args[1],args[2]);
    return 1;
}
