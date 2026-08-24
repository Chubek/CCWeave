#ifndef CCW_SEMA_H
#define CCW_SEMA_H

#include "ccw_ir.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  int rulesets_loaded;
  int rules_loaded;
} ccw_sema_report;

/*
 * Load and apply the language-selected semantic salvo to canonical Weave IR.
 * The salvo is deliberately selected by the host; rules themselves remain
 * unordered declarative facts in sema-salvo.
 */
ccw_status ccw_sema_analyze (ccw_ir *ir, const char *salvo_dir,
                             const char *const *rulesets, size_t ruleset_count,
                             ccw_sema_report *report, char **error_message);

#ifdef __cplusplus
}
#endif

#endif
