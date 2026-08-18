/* ccw_val boundary-value support library (GlueSTD.h "boundary values").
 * Strings are copied at the boundary; each side frees its own copies. */

#include "GlueSTD.h"
#include "kstring.h"

#include <stdlib.h>

static char *ccw_strdup_or_null(const char *s)
{
    if (s == NULL) return NULL;
    kstring_t copy = { 0, 0, NULL };
    if (kputs(s, &copy) == EOF) return NULL;
    return ks_release(&copy);
}

ccw_val ccw_nil(void)
{
    ccw_val v;
    v.type = CCW_T_NIL;
    v.as.i = 0;
    return v;
}

ccw_val ccw_bool(bool b)
{
    ccw_val v = ccw_nil();
    v.type = CCW_T_BOOL;
    v.as.b = b;
    return v;
}

ccw_val ccw_int(int64_t i)
{
    ccw_val v = ccw_nil();
    v.type = CCW_T_INT;
    v.as.i = i;
    return v;
}

ccw_val ccw_float(double f)
{
    ccw_val v = ccw_nil();
    v.type = CCW_T_FLOAT;
    v.as.f = f;
    return v;
}

/* An allocation failure degrades to nil rather than aborting: library
 * code must never exit() or abort(). */
ccw_val ccw_string(const char *s)
{
    ccw_val v = ccw_nil();
    char *copy = ccw_strdup_or_null(s);
    if (copy == NULL) return v;
    v.type = CCW_T_STRING;
    v.as.s = copy;
    return v;
}

ccw_val ccw_symbol(const char *s)
{
    ccw_val v = ccw_nil();
    char *copy = ccw_strdup_or_null(s);
    if (copy == NULL) return v;
    v.type = CCW_T_SYMBOL;
    v.as.s = copy;
    return v;
}

ccw_val ccw_node_val(ccw_node n)
{
    ccw_val v = ccw_nil();
    v.type = CCW_T_NODE;
    v.as.node = n;
    return v;
}

void ccw_val_clear(ccw_val *v)
{
    if (v == NULL) return;
    if ((v->type == CCW_T_STRING || v->type == CCW_T_SYMBOL) && v->as.s != NULL)
        free(v->as.s);
    v->type = CCW_T_NIL;
    v->as.i = 0;
}
