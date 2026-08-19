/* §2.3: deferred expression AST — shared between both frontends.
 * Expression-valued fields hold these AST nodes, never pre-computed integers.
 * Evaluation happens during layout in plan order against a live location
 * counter. */
#ifndef CCWLD_EXPR_H
#define CCWLD_EXPR_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  struct ccwld_plan;

  /* Expression node kinds (§2.3). */
  typedef enum
  {
    CCWLD_EXPR_INT = 1,             /* integer literal               */
    CCWLD_EXPR_SYMBOL = 2,          /* symbol reference              */
    CCWLD_EXPR_DOT = 3,             /* current location counter      */
    CCWLD_EXPR_BINARY = 4,          /* arithmetic / bitwise node     */
    CCWLD_EXPR_UNARY = 5,           /* unary op (neg, not, abs, …)   */
    CCWLD_EXPR_ALIGN = 6,           /* align(expr, boundary)         */
    CCWLD_EXPR_MAX = 7,             /* max(a, b)                     */
    CCWLD_EXPR_MIN = 8,             /* min(a, b)                     */
    CCWLD_EXPR_COND = 9,            /* cond(test, then, else)        */
    CCWLD_EXPR_DEFINED = 10,        /* defined(sym) — 1 or 0         */
    CCWLD_EXPR_REGION_ORIGIN = 11,  /* origin of named region        */
    CCWLD_EXPR_REGION_LENGTH = 12,  /* length of named region        */
    CCWLD_EXPR_SIZEOF = 13,         /* sizeof(section-name)          */
    CCWLD_EXPR_ADDR = 14,           /* addr(section-name)           */
    CCWLD_EXPR_LOADADDR = 15,       /* loadaddr(section-name)        */
    CCWLD_EXPR_SIZEOF_HEADERS = 16, /* sizeof_headers                */
    CCWLD_EXPR_SEGMENT_START = 17,  /* start of named segment        */
  } ccwld_expr_kind;

  /* Binary / unary operator tags. */
  typedef enum
  {
    CCWLD_OP_ADD = '+',
    CCWLD_OP_SUB = '-',
    CCWLD_OP_MUL = '*',
    CCWLD_OP_DIV = '/',
    CCWLD_OP_MOD = '%',
    CCWLD_OP_AND = '&',
    CCWLD_OP_OR = '|',
    CCWLD_OP_XOR = '^',
    CCWLD_OP_SHL = '<',
    CCWLD_OP_SHR = '>',
    CCWLD_OP_NEG = '~',
    CCWLD_OP_NOT = '!',
    CCWLD_OP_ABS = 'A',
  } ccwld_op_tag;

  typedef struct ccwld_expr
  {
    ccwld_expr_kind kind;
    ccwld_op_tag op;      /* for BINARY / UNARY nodes          */
    uint64_t ival;        /* for INT literal                   */
    char *name;           /* for SYMBOL / REGION / SECTION refs */
    struct ccwld_expr *a; /* left child  (or operand for UNARY/ALIGN) */
    struct ccwld_expr *b; /* right child (or boundary for ALIGN)     */
    struct ccwld_expr *c; /* third child (for COND: test-a-then-b-else-c) */
    /* --- internal: cycle-detection mark --- */
    int visited;
  } ccwld_expr;

  /* Constructors. */
  ccwld_expr *ccwld_expr_int (uint64_t value);
  ccwld_expr *ccwld_expr_symbol (const char *name);
  ccwld_expr *ccwld_expr_dot (void);
  ccwld_expr *ccwld_expr_binary (ccwld_op_tag op, ccwld_expr *a,
                                 ccwld_expr *b);
  ccwld_expr *ccwld_expr_unary (ccwld_op_tag op, ccwld_expr *a);
  ccwld_expr *ccwld_expr_align (ccwld_expr *a, uint64_t boundary);
  ccwld_expr *ccwld_expr_max (ccwld_expr *a, ccwld_expr *b);
  ccwld_expr *ccwld_expr_min (ccwld_expr *a, ccwld_expr *b);
  ccwld_expr *ccwld_expr_cond (ccwld_expr *test, ccwld_expr *then_e,
                               ccwld_expr *else_e);
  ccwld_expr *ccwld_expr_defined (const char *symbol_name);
  ccwld_expr *ccwld_expr_region_origin (const char *region_name);
  ccwld_expr *ccwld_expr_region_length (const char *region_name);
  ccwld_expr *ccwld_expr_sizeof (const char *section_name);
  ccwld_expr *ccwld_expr_addr (const char *section_name);
  ccwld_expr *ccwld_expr_loadaddr (const char *section_name);
  ccwld_expr *ccwld_expr_sizeof_headers (void);
  ccwld_expr *ccwld_expr_segment_start (const char *segment_name);

  /* Deep copy. */
  ccwld_expr *ccwld_expr_clone (const ccwld_expr *e);

  /* Free an expression tree recursively. */
  void ccwld_expr_free (ccwld_expr *e);

  /* Evaluate expression against a plan+dot.  Returns 0 on failure and
   * fills *error_message if provided.  Cycle detection is built-in. */
  int ccwld_expr_eval (const ccwld_expr *e, const struct ccwld_plan *plan,
                       uint64_t dot, uint64_t *out, char **error_message);

  /* Serialize an expression to a canonical string (for plan serialization). */
  void ccwld_expr_to_string (const ccwld_expr *e, char **out, size_t *len,
                             size_t *cap);

  /* Compare two expression trees for structural equality. */
  int ccwld_expr_equal (const ccwld_expr *a, const ccwld_expr *b);

  /* Reset internal state (visited flags) on the whole tree — called
   * after each evaluation pass. */
  void ccwld_expr_reset_visited (ccwld_expr *e);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_EXPR_H */
