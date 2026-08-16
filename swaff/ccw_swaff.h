/* Swaff — frontend orchestration (§6.2).
 *
 * A frontend is a vendored Tree-sitter grammar plus a lowering adapter
 * that walks the CST and emits Kliche stereotype calls. The adapter is
 * the ONLY component that sees Tree-sitter node types; this header is
 * deliberately free of Tree-sitter declarations so nothing above or
 * below Swaff depends on the parser framework. */

#ifndef CCW_SWAFF_H
#define CCW_SWAFF_H

#include "ccw_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How the adapter treats Tree-sitter ERROR/MISSING nodes. The contract
 * requires an explicit decision, not silent tolerance. */
typedef enum {
    CCW_SWAFF_REJECT_ON_ERROR = 0,  /* any ERROR/MISSING fails the lowering */
    CCW_SWAFF_RECOVER_ON_ERROR      /* skip the subtree, record a diagnostic */
} ccw_swaff_error_policy;

typedef struct {
    int  error_nodes;
    int  missing_nodes;
    int  recovered_subtrees;
    int  functions_lowered;
    char message[256];
} ccw_swaff_report;

typedef struct ccw_swaff_frontend ccw_swaff_frontend;

/* The C frontend: tree-sitter-c grammar + its lowering adapter. */
const ccw_swaff_frontend *ccw_swaff_frontend_c(void);
const char               *ccw_swaff_frontend_name(const ccw_swaff_frontend *fe);

/* Parses `source` and lowers it into a fresh module named `module_name`.
 * Returns NULL on failure (including rejected ERROR nodes). */
ccw_ir *ccw_swaff_lower(const ccw_swaff_frontend *fe,
                        const char *source, size_t source_len,
                        const char *module_name, ccw_profile profile,
                        ccw_swaff_error_policy policy,
                        ccw_swaff_report *report,
                        char **error_message);

/* True when this build has Tree-sitter frontends compiled in. */
bool ccw_swaff_available(void);

#ifdef __cplusplus
}
#endif
#endif /* CCW_SWAFF_H */
