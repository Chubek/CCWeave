/* S7 reference executor: implements every function in glue/GlueSTD.h.
 *
 * The executor registers nothing itself; all IR accessors come from the
 * host at runtime via ccw_glue_register (§3.3). */

#include "GlueSTD.h"
#include "ccw_s7_prelude.h"
#include "s7.h"
#include "kstring.h"
#include "kvec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CCW_GLUE_TAG "ccw-glue"

typedef struct {
    char           *scheme_name;
    int             min_arity;
    int             max_arity;   /* -1 = variadic */
    ccw_accessor_fn fn;
    void           *host_ctx;
    ccw_executor   *ex;
} ccw_accessor;

typedef struct {
    char     *library_key;   /* "(ccweave kernel foo)" */
    char    **capabilities;
    int       capability_count;
    bool      live;
} ccw_kernel;

struct ccw_executor {
    s7_scheme    *sc;
    int           abi_version;
    char          name[32];

    ccw_accessor **accessors;   /* individually allocated: pointers are stable */
    int           accessor_count;
    int           accessor_cap;

    ccw_kernel   *kernels;
    int           kernel_count;
    int           kernel_cap;

    ccw_ir       *current_ir;   /* non-NULL only during kernel-apply */
    bool          kernel_loaded;
};

static bool reserve_items(void **items, int *capacity, int needed,
                          size_t item_size)
{
    if (needed <= *capacity) return true;
    int next = *capacity ? *capacity * 2 : 8;
    while (next < needed) next *= 2;
    kvec_t(unsigned char) bytes = {
        0, (size_t)*capacity * item_size, *items
    };
    if (kv_resize(unsigned char, bytes, (size_t)next * item_size) == NULL)
        return false;
    *items = bytes.a;
    *capacity = next;
    return true;
}

static char *ccw_dup(const char *s)
{
    if (s == NULL) return NULL;
    kstring_t copy = { 0, 0, NULL };
    if (kputs(s, &copy) == EOF) return NULL;
    return ks_release(&copy);
}

/* ---------- ccw_val <-> s7 conversion (the seven boundary types) ---------- */

static s7_pointer ccw_val_to_s7(s7_scheme *sc, ccw_val *v)
{
    s7_pointer out;
    switch (v->type) {
    case CCW_T_BOOL:   out = s7_make_boolean(sc, v->as.b); break;
    case CCW_T_INT:    out = s7_make_integer(sc, v->as.i); break;
    case CCW_T_FLOAT:  out = s7_make_real(sc, v->as.f); break;
    case CCW_T_STRING: out = s7_make_string(sc, v->as.s ? v->as.s : ""); break;
    case CCW_T_SYMBOL: out = s7_make_symbol(sc, v->as.s ? v->as.s : "|nil|"); break;
    /* Node ids are plain integers in Scheme: kernels only ever hold ints. */
    case CCW_T_NODE:   out = s7_make_integer(sc, (s7_int)v->as.node); break;
    case CCW_T_NIL:
    default:           out = s7_nil(sc); break;
    }
    ccw_val_clear(v);   /* executor took ownership of any string buffer */
    return out;
}

static bool s7_to_ccw_val(s7_scheme *sc, s7_pointer p, ccw_val *out)
{
    if (s7_is_null(sc, p)) { *out = ccw_nil(); return true; }
    if (s7_is_boolean(p))  { *out = ccw_bool(s7_boolean(sc, p)); return true; }
    if (s7_is_integer(p))  { *out = ccw_int((int64_t)s7_integer(p)); return true; }
    if (s7_is_real(p))     { *out = ccw_float(s7_real(p)); return true; }
    if (s7_is_string(p))   { *out = ccw_string(s7_string(p)); return true; }
    if (s7_is_symbol(p))   { *out = ccw_symbol(s7_symbol_name(p)); return true; }
    return false;
}

/* ---------- accessor trampoline ---------- */

static s7_pointer ccw_accessor_trampoline(s7_scheme *sc, s7_pointer args)
{
    ccw_accessor *acc = (ccw_accessor *)s7_c_pointer(s7_car(args));
    args = s7_cdr(args);

    int nargs = (int)s7_list_length(sc, args);
    if (nargs < acc->min_arity ||
        (acc->max_arity >= 0 && nargs > acc->max_arity)) {
        /* CCW_ERR_ARITY: raise in Scheme without calling fn. */
        return s7_error(sc, s7_make_symbol(sc, "wrong-number-of-args"),
                        s7_list(sc, 2,
                                s7_make_string(sc, "~A: wrong number of arguments"),
                                s7_make_string(sc, acc->scheme_name)));
    }

    /* Accessors are only callable during a kernel-apply. */
    if (acc->ex->current_ir == NULL)
        return s7_error(sc, s7_make_symbol(sc, CCW_GLUE_TAG),
                        s7_list(sc, 2,
                                s7_make_string(sc, "~A: glue accessors are only callable during kernel-apply"),
                                s7_make_string(sc, acc->scheme_name)));

    kvec_t(ccw_val) vals = { 0, 0, NULL };
    if (nargs > 0) {
        if (kv_resize(ccw_val, vals, (size_t)nargs) == NULL)
            return s7_error(sc, s7_make_symbol(sc, CCW_GLUE_TAG),
                            s7_list(sc, 1, s7_make_string(sc, "out of memory")));
    }
    int i = 0;
    for (s7_pointer p = args; !s7_is_null(sc, p); p = s7_cdr(p), i++) {
        if (!s7_to_ccw_val(sc, s7_car(p), &vals.a[i])) {
            for (int j = 0; j < i; j++) ccw_val_clear(&vals.a[j]);
            kv_destroy(vals);
            return s7_error(sc, s7_make_symbol(sc, CCW_GLUE_TAG),
                            s7_list(sc, 2,
                                    s7_make_string(sc, "~A: value of unsupported type crossed the glue boundary"),
                                    s7_make_string(sc, acc->scheme_name)));
        }
    }

    ccw_val result = ccw_nil();
    char *error_message = NULL;
    ccw_status st = acc->fn(acc->host_ctx, acc->ex->current_ir, vals.a, nargs,
                            &result, &error_message);
    for (int j = 0; j < nargs; j++) ccw_val_clear(&vals.a[j]);
    kv_destroy(vals);

    if (st != CCW_OK) {
        ccw_val_clear(&result);
        /* Accessor failures surface as Scheme conditions naming the accessor. */
        s7_pointer err = s7_error(sc, s7_make_symbol(sc, CCW_GLUE_TAG),
                                  s7_list(sc, 3,
                                          s7_make_string(sc, "~A: ~A"),
                                          s7_make_string(sc, acc->scheme_name),
                                          s7_make_string(sc, error_message ? error_message
                                                                           : "accessor failed")));
        free(error_message);
        return err;
    }
    free(error_message);
    return ccw_val_to_s7(sc, &result);
}

ccw_status ccw_glue_register(ccw_executor *ex, const char *scheme_name,
                             int min_arity, int max_arity,
                             ccw_accessor_fn fn, void *host_ctx)
{
    if (ex == NULL || scheme_name == NULL || fn == NULL) return CCW_ERR_TYPE;
    /* Re-registering MUST NOT happen after a kernel is loaded. */
    if (ex->kernel_loaded) return CCW_ERR_ACCESSOR;

    for (int i = 0; i < ex->accessor_count; i++) {
        ccw_accessor *existing = ex->accessors[i];
        if (strcmp(existing->scheme_name, scheme_name) == 0) {
            /* Re-registering a name replaces the previous binding. */
            existing->min_arity = min_arity;
            existing->max_arity = max_arity;
            existing->fn = fn;
            existing->host_ctx = host_ctx;
            return CCW_OK;
        }
    }

    if (!reserve_items((void **)&ex->accessors, &ex->accessor_cap,
                       ex->accessor_count + 1, sizeof(*ex->accessors)))
        return CCW_ERR_OOM;

    ccw_accessor *slot = (ccw_accessor *)calloc(1, sizeof(*slot));
    if (slot == NULL) return CCW_ERR_OOM;
    slot->scheme_name = ccw_dup(scheme_name);
    if (slot->scheme_name == NULL) { free(slot); return CCW_ERR_OOM; }
    slot->min_arity = min_arity;
    slot->max_arity = max_arity;
    slot->fn = fn;
    slot->host_ctx = host_ctx;
    slot->ex = ex;

    /* Bind: (define <name> (lambda args (apply ccw-glue-invoke <ptr> args)))
     * so every glue procedure is variadic in S7 and arity is enforced by
     * the trampoline, which reports CCW_ERR_ARITY as a Scheme error. */
    s7_pointer ptr = s7_make_c_pointer(ex->sc, slot);
    s7_pointer form =
        s7_list(ex->sc, 3,
                s7_make_symbol(ex->sc, "define"),
                s7_make_symbol(ex->sc, scheme_name),
                s7_list(ex->sc, 3,
                        s7_make_symbol(ex->sc, "lambda"),
                        s7_make_symbol(ex->sc, "args"),
                        s7_list(ex->sc, 4,
                                s7_make_symbol(ex->sc, "apply"),
                                s7_make_symbol(ex->sc, "ccw-glue-invoke"),
                                ptr,
                                s7_make_symbol(ex->sc, "args"))));
    s7_eval(ex->sc, form, s7_rootlet(ex->sc));

    ex->accessors[ex->accessor_count++] = slot;
    return CCW_OK;
}

/* ---------- executor lifecycle ---------- */

ccw_executor *ccw_executor_create(void)
{
    /* ABI check at creation: a mismatch means this build is unusable. */
    if (CCW_GLUE_ABI_VERSION != 1) return NULL;

    ccw_executor *ex = (ccw_executor *)calloc(1, sizeof(*ex));
    if (ex == NULL) return NULL;
    ex->sc = s7_init();
    if (ex->sc == NULL) { free(ex); return NULL; }
    ex->abi_version = CCW_GLUE_ABI_VERSION;
    snprintf(ex->name, sizeof(ex->name), "s7 %s", S7_VERSION);

    s7_define_function(ex->sc, "ccw-glue-invoke", ccw_accessor_trampoline,
                       1, 0, true, "internal glue trampoline");
    s7_eval_c_string(ex->sc, CCW_S7_R7RS);
    s7_eval_c_string(ex->sc, CCW_S7_PRELUDE);
    s7_eval_c_string(ex->sc, CCW_S7_GUARDS);
    return ex;
}

void ccw_executor_destroy(ccw_executor *ex)
{
    if (ex == NULL) return;
    for (int i = 0; i < ex->accessor_count; i++) {
        free(ex->accessors[i]->scheme_name);
        free(ex->accessors[i]);
    }
    free(ex->accessors);
    for (int i = 0; i < ex->kernel_count; i++) {
        free(ex->kernels[i].library_key);
        for (int j = 0; j < ex->kernels[i].capability_count; j++)
            free(ex->kernels[i].capabilities[j]);
        free(ex->kernels[i].capabilities);
    }
    free(ex->kernels);
    free(ex);
}

int ccw_executor_abi_version(const ccw_executor *ex)
{
    return ex ? ex->abi_version : CCW_ERR_ABI;
}

const char *ccw_executor_name(const ccw_executor *ex)
{
    return ex ? ex->name : "";
}

/* ---------- kernel loading ---------- */

static ccw_kernel *kernel_get(ccw_executor *ex, int kernel_id)
{
    if (ex == NULL || kernel_id < 0 || kernel_id >= ex->kernel_count) return NULL;
    ccw_kernel *k = &ex->kernels[kernel_id];
    return k->live ? k : NULL;
}

/* Look up an exported procedure of a loaded kernel library. */
static s7_pointer kernel_export(ccw_executor *ex, ccw_kernel *k, const char *name)
{
    s7_pointer call = s7_list(ex->sc, 3,
                              s7_make_symbol(ex->sc, "ccw-library-ref"),
                              s7_list(ex->sc, 2,
                                      s7_make_symbol(ex->sc, "quote"),
                                      s7_make_symbol(ex->sc, k->library_key)),
                              s7_list(ex->sc, 2,
                                      s7_make_symbol(ex->sc, "quote"),
                                      s7_make_symbol(ex->sc, name)));
    return s7_eval(ex->sc, call, s7_rootlet(ex->sc));
}

static void set_error(char **error_message, const char *msg)
{
    if (error_message != NULL) *error_message = ccw_dup(msg);
}

/* Calls a guard helper and splits its (ok . payload) result. */
static bool guarded_result(s7_scheme *sc, s7_pointer pair, s7_pointer *payload)
{
    if (!s7_is_pair(pair)) { *payload = s7_nil(sc); return false; }
    *payload = s7_cdr(pair);
    return s7_boolean(sc, s7_car(pair));
}

static char *condition_text(s7_scheme *sc, s7_pointer p)
{
    if (s7_is_string(p)) return ccw_dup(s7_string(p));
    char *s = s7_object_to_c_string(sc, p);
    char *copy = ccw_dup(s ? s : "kernel raised an error");
    free(s);
    return copy;
}

int ccw_kernel_load(ccw_executor *ex, const char *path, char **error_message)
{
    if (error_message) *error_message = NULL;
    if (ex == NULL || path == NULL) return CCW_ERR_LOAD;

    s7_pointer loader = s7_name_to_value(ex->sc, "ccw-load-guarded");
    s7_pointer res = s7_call(ex->sc, loader,
                             s7_list(ex->sc, 1, s7_make_string(ex->sc, path)));
    s7_pointer payload = s7_nil(ex->sc);
    if (!guarded_result(ex->sc, res, &payload)) {
        if (error_message) *error_message = condition_text(ex->sc, payload);
        return CCW_ERR_LOAD;
    }
    s7_pointer key = payload;
    if (!s7_is_symbol(key)) {
        set_error(error_message, "kernel file defined no library");
        return CCW_ERR_LOAD;
    }

    if (!reserve_items((void **)&ex->kernels, &ex->kernel_cap,
                       ex->kernel_count + 1, sizeof(*ex->kernels))) {
        set_error(error_message, "out of memory");
        return CCW_ERR_OOM;
    }
    int id = ex->kernel_count;
    ccw_kernel *k = &ex->kernels[id];
    memset(k, 0, sizeof(*k));
    k->library_key = ccw_dup(s7_symbol_name(key));
    k->live = true;

    /* §GlueSTD: verify the three required exports at load time. */
    static const char *const required[] = { "kernel-info", "kernel-capabilities",
                                            "kernel-apply" };
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        s7_pointer p = kernel_export(ex, k, required[i]);
        if (!s7_is_procedure(p)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "kernel does not export %s", required[i]);
            set_error(error_message, msg);
            free(k->library_key);
            k->live = false;
            k->library_key = NULL;
            return CCW_ERR_LOAD;
        }
    }

    ex->kernel_count++;
    ex->kernel_loaded = true;
    return id;
}

ccw_status ccw_kernel_unload(ccw_executor *ex, int kernel_id)
{
    ccw_kernel *k = kernel_get(ex, kernel_id);
    if (k == NULL) return CCW_ERR_LOAD;
    free(k->library_key);
    k->library_key = NULL;
    for (int i = 0; i < k->capability_count; i++) free(k->capabilities[i]);
    free(k->capabilities);
    k->capabilities = NULL;
    k->capability_count = 0;
    k->live = false;   /* ids are never reused */
    return CCW_OK;
}

/* ---------- kernel-info ---------- */

/* kernel-info returns an alist; pull one entry as a fresh C string. */
static char *info_field(ccw_executor *ex, s7_pointer alist, const char *key)
{
    for (s7_pointer p = alist; s7_is_pair(p); p = s7_cdr(p)) {
        s7_pointer entry = s7_car(p);
        if (!s7_is_pair(entry)) continue;
        s7_pointer k = s7_car(entry);
        if (!s7_is_symbol(k) || strcmp(s7_symbol_name(k), key) != 0) continue;
        s7_pointer v = s7_cdr(entry);
        if (s7_is_string(v)) return ccw_dup(s7_string(v));
        if (s7_is_symbol(v)) return ccw_dup(s7_symbol_name(v));
        return ccw_dup(s7_object_to_c_string(ex->sc, v));
    }
    return NULL;
}

ccw_status ccw_kernel_info(ccw_executor *ex, int kernel_id,
                           char **name, char **version, char **description)
{
    if (name) *name = NULL;
    if (version) *version = NULL;
    if (description) *description = NULL;
    ccw_kernel *k = kernel_get(ex, kernel_id);
    if (k == NULL) return CCW_ERR_LOAD;

    s7_pointer fn = kernel_export(ex, k, "kernel-info");
    if (!s7_is_procedure(fn)) return CCW_ERR_LOAD;
    s7_pointer guard = s7_name_to_value(ex->sc, "ccw-call-guarded");
    s7_pointer alist = s7_nil(ex->sc);
    if (!guarded_result(ex->sc,
                        s7_call(ex->sc, guard, s7_list(ex->sc, 1, fn)),
                        &alist))
        return CCW_ERR_KERNEL;
    if (!s7_is_pair(alist)) return CCW_ERR_KERNEL;

    if (name)        *name        = info_field(ex, alist, "name");
    if (version)     *version     = info_field(ex, alist, "version");
    if (description) *description = info_field(ex, alist, "description");
    return CCW_OK;
}

/* ---------- capability enumeration (single source of truth) ---------- */

/* Refreshes the cached list from the live kernel. Cached strings stay
 * valid until the next call on this executor, per GlueSTD.h. */
static int refresh_capabilities(ccw_executor *ex, ccw_kernel *k)
{
    for (int i = 0; i < k->capability_count; i++) free(k->capabilities[i]);
    free(k->capabilities);
    k->capabilities = NULL;
    k->capability_count = 0;

    s7_pointer fn = kernel_export(ex, k, "kernel-capabilities");
    if (!s7_is_procedure(fn)) return CCW_ERR_LOAD;
    s7_pointer guard = s7_name_to_value(ex->sc, "ccw-call-guarded");
    s7_pointer caps = s7_nil(ex->sc);
    if (!guarded_result(ex->sc,
                        s7_call(ex->sc, guard, s7_list(ex->sc, 1, fn)),
                        &caps))
        return CCW_ERR_KERNEL;

    int n = 0;
    for (s7_pointer p = caps; s7_is_pair(p); p = s7_cdr(p)) n++;
    if (n == 0) return 0;

    int cap = 0;
    if (!reserve_items((void **)&k->capabilities, &cap, n, sizeof(*k->capabilities)))
        return CCW_ERR_OOM;
    memset(k->capabilities, 0, (size_t)cap * sizeof(*k->capabilities));
    int i = 0;
    for (s7_pointer p = caps; s7_is_pair(p); p = s7_cdr(p)) {
        s7_pointer c = s7_car(p);
        const char *text = s7_is_symbol(c) ? s7_symbol_name(c)
                         : s7_is_string(c) ? s7_string(c) : NULL;
        if (text == NULL) continue;
        k->capabilities[i++] = ccw_dup(text);
    }
    k->capability_count = i;
    return i;
}

int ccw_kernel_capability_count(ccw_executor *ex, int kernel_id)
{
    ccw_kernel *k = kernel_get(ex, kernel_id);
    if (k == NULL) return CCW_ERR_LOAD;
    return refresh_capabilities(ex, k);
}

const char *ccw_kernel_capability(ccw_executor *ex, int kernel_id, int idx)
{
    ccw_kernel *k = kernel_get(ex, kernel_id);
    if (k == NULL) return NULL;
    if (k->capabilities == NULL && refresh_capabilities(ex, k) < 0) return NULL;
    if (idx < 0 || idx >= k->capability_count) return NULL;
    return k->capabilities[idx];
}

/* ---------- invocation ---------- */

/* options: "key=value" strings -> alist of (symbol . string). */
static s7_pointer options_alist(s7_scheme *sc, const char *const *options)
{
    s7_pointer list = s7_nil(sc);
    if (options == NULL) return list;
    int n = 0;
    while (options[n] != NULL) n++;
    for (int i = n - 1; i >= 0; i--) {
        const char *eq = strchr(options[i], '=');
        s7_pointer key, value;
        if (eq == NULL) {
            key = s7_make_symbol(sc, options[i]);
            value = s7_make_string(sc, "");
        } else {
            char *kbuf = (char *)malloc((size_t)(eq - options[i]) + 1u);
            if (kbuf == NULL) continue;
            memcpy(kbuf, options[i], (size_t)(eq - options[i]));
            kbuf[eq - options[i]] = '\0';
            key = s7_make_symbol(sc, kbuf);
            free(kbuf);
            value = s7_make_string(sc, eq + 1);
        }
        list = s7_cons(sc, s7_cons(sc, key, value), list);
    }
    return list;
}

ccw_status ccw_kernel_apply(ccw_executor *ex, int kernel_id,
                            const char *capability, ccw_ir *ir,
                            const char *const *options,
                            char **error_message)
{
    if (error_message) *error_message = NULL;
    ccw_kernel *k = kernel_get(ex, kernel_id);
    if (k == NULL || capability == NULL) return CCW_ERR_LOAD;

    /* Capability is verified against the live list before dispatching. */
    int n = refresh_capabilities(ex, k);
    bool found = false;
    for (int i = 0; i < n; i++)
        if (strcmp(k->capabilities[i], capability) == 0) { found = true; break; }
    if (!found) {
        set_error(error_message, "kernel does not provide the requested capability");
        return CCW_ERR_NO_CAPABILITY;
    }

    s7_pointer fn = kernel_export(ex, k, "kernel-apply");
    if (!s7_is_procedure(fn)) return CCW_ERR_LOAD;

    /* The IR handle is opaque to Scheme: kernels pass it back unchanged. */
    s7_pointer args = s7_list(ex->sc, 3,
                              s7_make_symbol(ex->sc, capability),
                              s7_make_c_pointer(ex->sc, ir),
                              options_alist(ex->sc, options));

    ccw_ir *saved = ex->current_ir;
    ex->current_ir = ir;

    s7_pointer guard = s7_name_to_value(ex->sc, "ccw-call-guarded");
    s7_pointer result = s7_call(ex->sc, guard, s7_cons(ex->sc, fn, args));
    ex->current_ir = saved;

    s7_pointer payload = s7_nil(ex->sc);
    if (!guarded_result(ex->sc, result, &payload)) {
        if (error_message) *error_message = condition_text(ex->sc, payload);
        return CCW_ERR_KERNEL;
    }
    return CCW_OK;
}
