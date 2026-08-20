/* §2: The link-plan IR — sealed, serializable, canonical.
 *
 * Both frontends (ld-script via mpc, Lua via lccwld) lower to this
 * single immutable IR. Once sealed, the plan is read-only; post-seal
 * mutation only through phase hooks within their mutability scope.
 *
 * The struct types are defined in expr/ccwld_expr.h to avoid circular
 * inclusion issues. This header declares the plan builder and lifecycle
 * functions. */
#ifndef CCWLD_PLAN_H
#define CCWLD_PLAN_H

#include "../expr/ccwld_expr.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* --- lifecycle --- */
  ccwld_plan *ccwld_plan_new (const char *target);
  void ccwld_plan_free (ccwld_plan *p);

  /* --- plan builders (only valid before seal) --- */
  int ccwld_plan_output (ccwld_plan *p, const ccwld_output *o, ccwld_error *e);
  int ccwld_plan_input (ccwld_plan *p, const char *path, int as_needed,
                        int startup, ccwld_error *e);
  int ccwld_plan_group (ccwld_plan *p, const char **paths, size_t n,
                        ccwld_error *e);
  int ccwld_plan_search_path (ccwld_plan *p, const char *path, ccwld_error *e);
  int ccwld_plan_memory (ccwld_plan *p, const char *name, const char *attrs,
                         uint64_t origin, uint64_t length, ccwld_error *e);
  int ccwld_plan_section (ccwld_plan *p, const char *name, const char *region,
                          uint64_t align, const char *selector,
                          const char *at_region, ccwld_error *e);
  int ccwld_plan_section_full (ccwld_plan *p, const char *name,
                               const char *region, uint64_t align,
                               const char *selector, const char *keep,
                               const char *at_region, ccwld_expr *fill,
                               ccwld_error *e);
  int ccwld_plan_symbol (ccwld_plan *p, const char *name,
                         ccwld_expr *expr, int provide, int hidden,
                         ccwld_error *e);
  int ccwld_plan_symbol_full (ccwld_plan *p, const char *name,
                              ccwld_expr *expr, int provide,
                              const char *visibility, const char *binding,
                              ccwld_error *e);
  int ccwld_plan_phdr (ccwld_plan *p, const char *name, const char *type,
                       uint32_t flags, uint64_t align, ccwld_error *e);
  int ccwld_plan_version (ccwld_plan *p, const char *symbol,
                          const char *version, int is_default, ccwld_error *e);
  int ccwld_plan_lto (ccwld_plan *p, const char *pipeline, unsigned jobs,
                      const char *cache_dir, ccwld_error *e);
  int ccwld_plan_plugin (ccwld_plan *p, const char *path, const char *options,
                         ccwld_error *e);
  int ccwld_plan_hook (ccwld_plan *p, ccwld_phase phase, ccwld_hook_fn fn,
                       void *user, ccwld_error *e);

  /* --- seal / serialize / hash --- */
  int ccwld_plan_seal (ccwld_plan *p, ccwld_error *e);
  int ccwld_plan_serialize (const ccwld_plan *p, char **out, size_t *len,
                            ccwld_error *e);
  int ccwld_plan_hash (const ccwld_plan *p, char out[65]);

  /* --- frontend entry points --- */
  int ccwld_run_lua (const char *script, const char *target, ccwld_plan **out,
                     ccwld_error *e);
  int ccwld_run_ldscript (const char *script, const char *target,
                          ccwld_plan **out, ccwld_error *e);
  int ccwld_run_script (const char *script, const char *target,
                        const char *output_path, ccwld_error *e);

  void ccwld_free (void *p);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_PLAN_H */
