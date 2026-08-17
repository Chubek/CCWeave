/* Cephyr plugin interface — §1.5.
 *
 * A plugin module contributes extensions to Cephyr: new attributes,
 * semantic analyses, optimizations, or pipeline stages. All extensions
 * MUST be implemented through Kernels; plugins contribute Sched script
 * fragments, never mutate IR from host code. */

#ifndef CEPHYR_MODULE_H
#define CEPHYR_MODULE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- plugin descriptor ---------- */

typedef struct cephyr_module cephyr_module;

/* Plugin lifecycle: init() is called once at load time; fini() at
 * shutdown. Returns NULL on failure; the driver skips the plugin. */
typedef cephyr_module *(*cephyr_module_init_fn)(void);
typedef void           (*cephyr_module_fini_fn)(cephyr_module *mod);

/* Descriptive metadata. Static strings, do not free. */
typedef struct {
    const char *name;        /* unique identifier, e.g. "gnu-attributes" */
    const char *version;     /* semver string */
    const char *description; /* human-readable summary */
    const char *author;
} cephyr_module_info;

/* ---------- Sched script fragment ---------- */

/* Each plugin may contribute a Sched script fragment: a function that
 * takes the live scheduler handle S (as a Lua lightuserdata) and the
 * resolved barrier node ids (as a Lua table). The fragment MAY only
 * add nodes and edges; it MUST NOT reach into IR.
 *
 * The fragment is a Lua source string. The driver applies all enabled
 * plugin fragments in manifest-ordered sequence before S:seal(). */
typedef struct {
    const char *fragment;      /* Lua source text */
    const char *description;   /* what this fragment adds */
} cephyr_module_fragment;

/* ---------- attribute extension ---------- */

/* A plugin can register custom __attribute__ handlers. Each handler
 * receives the attribute name, its arguments (NULL-terminated), and
 * the AST node being decorated. */
typedef bool (*cephyr_attr_handler_fn)(void *ast_node,
                                       const char *attr_name,
                                       const char *const *attr_args);

/* ---------- module API ---------- */

/* Get module metadata. */
const cephyr_module_info *cephyr_module_get_info(const cephyr_module *mod);

/* Standard module bundle: GNU C attributes, basic LTO, etc. */
const cephyr_module *cephyr_stdmodule_bundle(void);

/* Sched script fragments. Returns count; pass NULL for fragments to
 * just get the count. */
int cephyr_module_fragment_count(const cephyr_module *mod);
const cephyr_module_fragment *cephyr_module_fragment_ref(const cephyr_module *mod, int idx);

/* Attribute handlers. Returns count; pass NULL for handlers to get count. */
int cephyr_module_attr_handler_count(const cephyr_module *mod);
const char *cephyr_module_attr_name(const cephyr_module *mod, int idx);
cephyr_attr_handler_fn cephyr_module_attr_handler(const cephyr_module *mod, int idx);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_MODULE_H */
