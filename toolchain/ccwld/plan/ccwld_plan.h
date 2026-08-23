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
  /* Attach an ordered input-section selector to the most recently
   * declared output section named `secname` (§2.1 input selectors).
   * `globs` are copied; whitespace-separated shorthand in the legacy
   * string selectors of ccwld_plan_section[_full] maps to file_glob
   * "*" plus the split list. */
  int ccwld_plan_selector (ccwld_plan *p, const char *secname,
                           const char *file_glob, char *const *globs,
                           size_t nglobs, int keep, ccwld_error *e);
  /* Output-section attribute setters (pre-seal). */
  int ccwld_plan_section_set_vma (ccwld_plan *p, const char *secname,
                                  ccwld_expr *vma, ccwld_error *e);
  int ccwld_plan_section_set_at (ccwld_plan *p, const char *secname,
                                 ccwld_expr *at, ccwld_error *e);
  int ccwld_plan_section_set_subalign (ccwld_plan *p, const char *secname,
                                       uint64_t subalign, ccwld_error *e);
  int ccwld_plan_section_set_phdr (ccwld_plan *p, const char *secname,
                                   const char *phdr, ccwld_error *e);
  int ccwld_plan_section_set_load (ccwld_plan *p, const char *secname,
                                   int load, ccwld_error *e);
  int ccwld_plan_section_set_region (ccwld_plan *p, const char *secname,
                                     const char *region, ccwld_error *e);
  int ccwld_plan_section_set_at_region (ccwld_plan *p, const char *secname,
                                        const char *at_region, ccwld_error *e);
  int ccwld_plan_section_set_fill (ccwld_plan *p, const char *secname,
                                   ccwld_expr *fill, ccwld_error *e);
  int ccwld_plan_symbol (ccwld_plan *p, const char *name,
                         ccwld_expr *expr, int provide, int hidden,
                         ccwld_error *e);
  int ccwld_plan_symbol_full (ccwld_plan *p, const char *name,
                              ccwld_expr *expr, int provide,
                              const char *visibility, const char *binding,
                              ccwld_error *e);
  /* Symbol assignment in a section context (ld SECTIONS inline
   * assignments, lccwld out()-scoped provide/assign).  `site` is the
   * definition-site provenance recorded for deferred-failure
   * diagnostics; it is copied and never serialized. */
  int ccwld_plan_symbol_at (ccwld_plan *p, const char *name,
                            ccwld_expr *expr, int provide, int hidden,
                            int sec_idx, const char *site, ccwld_error *e);
  /* `. = expr` location-counter step in section context `sec_idx`. */
  int ccwld_plan_dotstep (ccwld_plan *p, ccwld_expr *expr, int sec_idx,
                          const char *site, ccwld_error *e);
  /* Visibility/binding override or alias (lccwld §4.7). */
  int ccwld_plan_attr (ccwld_plan *p, const char *name, const char *visibility,
                       const char *binding, const char *alias, ccwld_error *e);
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
  /* -D key=value / --defsym binding (lccwld §3).  defsym entries are
   * additionally materialized as defined absolute plan symbols. */
  int ccwld_plan_env (ccwld_plan *p, const char *key, const char *val,
                      int defsym, ccwld_error *e);
  const char *ccwld_plan_env_get (const ccwld_plan *p, const char *key);
  /* Deterministic gensym (counter regime shared conceptually with
   * ccwas.gensym; reset per link because it lives on the plan). */
  size_t ccwld_plan_gensym (ccwld_plan *p, const char *prefix, char *buf,
                            size_t buflen);
  /* Provenance tag for the producer note ("ldscript"|"lua"|"api"). */
  void ccwld_plan_set_frontend (ccwld_plan *p, const char *frontend);

  /* --- seal / serialize / hash --- */
  int ccwld_plan_seal (ccwld_plan *p, ccwld_error *e);
  int ccwld_plan_serialize (const ccwld_plan *p, char **out, size_t *len,
                            ccwld_error *e);
  int ccwld_plan_hash (const ccwld_plan *p, char out[65]);

  /* --- frontend entry points --- */
  int ccwld_run_lua (const char *file, const char *target,
                     const char *const *defines,
                     const char *const *defsymbols, int unsafe_lua,
                     const ccwld_driver_defs *extra, ccwld_plan **out,
                     ccwld_error *e);
  int ccwld_run_ldscript (const char *script, const char *script_path,
                          const char *target, const ccwld_driver_defs *extra,
                          ccwld_plan **out, ccwld_error *e);

  void ccwld_free (void *p);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_PLAN_H */
