/* Cephyr standard module bundle — §8.5.
 *
 * Provides GNU C-style __attribute__ handlers and basic LTO support.
 * All operations are implemented via Kernels; this module only
 * contributes Sched script fragments and attribute hook registrations. */

#include "cephyr_stdmodule.h"

#include <stdlib.h>
#include <string.h>

/* ---------- module structure ---------- */

struct cephyr_module {
    cephyr_module_info        info;
    cephyr_module_fragment   *fragments;
    int                       fragment_count;
    const char              **attr_names;
    int                       attr_count;
    cephyr_attr_handler_fn   *attr_handlers;
};

/* ---------- GNU attributes module ---------- */

/* Supported GNU C attributes in v0.1.
 * These are recognized but most are no-ops in the current phase;
 * they serve as documentation and forward-compatibility markers. */
static const char *gnu_attr_names[] = {
    "noreturn", "const", "pure", "malloc",
    "format", "nonnull", "warn_unused_result",
    "used", "unused", "packed", "aligned",
    "section", "weak", "visibility",
    NULL
};

static bool gnu_attr_handler(void *ast_node, const char *attr_name,
                             const char *const *attr_args)
{
    (void)ast_node;
    (void)attr_args;
    /* v0.1: all attributes are recognized but most are no-ops.
     * 'noreturn' and 'const' are the only ones with semantic effect
     * in the current phase (they affect call analysis). */
    if (strcmp(attr_name, "noreturn") == 0) return true;
    if (strcmp(attr_name, "const") == 0)    return true;
    if (strcmp(attr_name, "pure") == 0)     return true;
    if (strcmp(attr_name, "malloc") == 0)   return true;
    return true; /* recognized, no error */
}

static cephyr_module *gnu_attributes_init(void)
{
    cephyr_module *mod = calloc(1, sizeof(cephyr_module));
    mod->info.name = "gnu-attributes";
    mod->info.version = "0.1.0";
    mod->info.description = "GNU C __attribute__ handlers";
    mod->info.author = "Cephyr";

    mod->attr_count = 14; /* count of gnu_attr_names */
    mod->attr_names = gnu_attr_names;
    mod->attr_handlers = calloc(14, sizeof(cephyr_attr_handler_fn));
    for (int i = 0; i < 14; i++)
        mod->attr_handlers[i] = gnu_attr_handler;

    /* No Sched fragments for attributes */
    mod->fragment_count = 0;
    mod->fragments = NULL;

    return mod;
}

/* ---------- LTO module ---------- */

static const char lto_fragment_source[] =
    "-- LTO fragment: registers link-time optimization passes.\n"
    "-- In v0.1, this is a placeholder; LTO requires kernel support\n"
    "-- for cross-module IR merging, deferred to v0.2.\n"
    "local lto_prep = S:probe { capability = 'lto.prepare' }\n"
    "if lto_prep then\n"
    "  S:edge(lto_prep, 'pre-tilly')\n"
    "end\n";

static cephyr_module_fragment lto_fragments[] = {
    { lto_fragment_source, "LTO preparation pass" },
};

static cephyr_module *lto_init(void)
{
    cephyr_module *mod = calloc(1, sizeof(cephyr_module));
    mod->info.name = "lto";
    mod->info.version = "0.1.0";
    mod->info.description = "Link-time optimization support";
    mod->info.author = "Cephyr";

    mod->fragment_count = 1;
    mod->fragments = lto_fragments;

    mod->attr_count = 0;
    mod->attr_names = NULL;
    mod->attr_handlers = NULL;

    return mod;
}

/* ---------- module API ---------- */

const cephyr_module_info *cephyr_module_get_info(const cephyr_module *mod)
{
    return mod ? &mod->info : NULL;
}

int cephyr_module_fragment_count(const cephyr_module *mod)
{
    return mod ? mod->fragment_count : 0;
}

const cephyr_module_fragment *cephyr_module_fragment_ref(const cephyr_module *mod, int idx)
{
    if (!mod || idx < 0 || idx >= mod->fragment_count) return NULL;
    return &mod->fragments[idx];
}

int cephyr_module_attr_handler_count(const cephyr_module *mod)
{
    return mod ? mod->attr_count : 0;
}

const char *cephyr_module_attr_name(const cephyr_module *mod, int idx)
{
    if (!mod || idx < 0 || idx >= mod->attr_count) return NULL;
    return mod->attr_names[idx];
}

cephyr_attr_handler_fn cephyr_module_attr_handler(const cephyr_module *mod, int idx)
{
    if (!mod || idx < 0 || idx >= mod->attr_count) return NULL;
    return mod->attr_handlers ? mod->attr_handlers[idx] : NULL;
}

/* ---------- bundle ---------- */

static const cephyr_module *bundle_singleton = NULL;

const cephyr_module *cephyr_stdmodule_gnu_attributes(void)
{
    static cephyr_module *mod = NULL;
    if (!mod) mod = gnu_attributes_init();
    return mod;
}

const cephyr_module *cephyr_stdmodule_lto(void)
{
    static cephyr_module *mod = NULL;
    if (!mod) mod = lto_init();
    return mod;
}

const cephyr_module *cephyr_stdmodule_bundle(void)
{
    (void)bundle_singleton;
    /* Return a representative module from the bundle */
    return cephyr_stdmodule_gnu_attributes();
}
