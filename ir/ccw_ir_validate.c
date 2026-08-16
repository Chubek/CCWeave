/* §5.3: core validation plus profile validation. Tilly forbids
 * dynamic-dispatch constructs; On1x forbids AOT link/relocation
 * constructs. No other divergence is permitted. */

#include "ccw_ir_internal.h"
#include "tilly/ccw_tilly.h"
#include "on1x/ccw_on1x.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ccw_status fail(char **error_message, const char *fmt, ...)
{
    if (error_message != NULL) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        *error_message = ccw_ir_strdup(buf);
    }
    return CCW_ERR_TYPE;
}

ccw_status ccw_ir_validate(const ccw_ir *ir, char **error_message)
{
    if (error_message) *error_message = NULL;
    if (ir == NULL) return CCW_ERR_TYPE;

    for (int fi = 0; fi < ir->functions.count; fi++) {
        ccw_node fn = ir->functions.items[fi];
        ccw_ir_node *f = ccw_ir_node_get(ir, fn);
        if (f == NULL || f->name == NULL)
            return fail(error_message, "function %d has no name", fi);
        if (f->children.count == 0)
            return fail(error_message, "function @%s has no blocks", f->name);

        for (int bi = 0; bi < f->children.count; bi++) {
            ccw_ir_node *b = ccw_ir_node_get(ir, f->children.items[bi]);
            if (b == NULL || b->name == NULL)
                return fail(error_message, "block %d of @%s has no name", bi, f->name);

            for (int ii = 0; ii < b->children.count; ii++) {
                ccw_node ins = b->children.items[ii];
                ccw_ir_node *n = ccw_ir_node_get(ir, ins);
                if (n == NULL || n->opcode == NULL)
                    return fail(error_message, "instruction %d in ^%s is malformed",
                                ii, b->name);
                if (!n->attached || n->parent != b->id)
                    return fail(error_message, "instruction %s in ^%s is not attached",
                                n->opcode, b->name);

                const char *why = NULL;
                if (ir->profile == CCW_PROFILE_TILLY)
                    why = ccw_tilly_reject_reason(ir, ins);
                else
                    why = ccw_on1x_reject_reason(ir, ins);
                if (why != NULL)
                    return fail(error_message, "%s profile violation in @%s ^%s: %s",
                                ccw_profile_name(ir->profile), f->name, b->name, why);
            }
        }
    }
    return CCW_OK;
}
