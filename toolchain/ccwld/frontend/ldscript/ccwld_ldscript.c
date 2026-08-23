/* §2/D-0039: the ld-script frontend.
 *
 * third_party/mpc parses the script; the AST lowers to deferred
 * expression nodes and plan-IR builders — never eagerly evaluated
 * values — so the ld-script and lccwld surfaces serialize to the same
 * canonical plan (parity suite, §2.2).  Diagnostics carry ld-script
 * file:line and the INCLUDE stack (§9). */
#include "mpc.h"

#include "../../ccwld.h"
#include "../../plan/ccwld_plan.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * lowering context
 * ================================================================ */

#define CCWLD_LD_MAX_INCLUDE 32

typedef struct
{
  ccwld_plan *p;
  ccwld_error *e;
  const char *stack[CCWLD_LD_MAX_INCLUDE + 1]; /* include/-T stack */
  size_t nstack;
  int failed;
} lower_ctx;

static void
ctx_fail (lower_ctx *ctx, int code, const mpc_ast_t *at, const char *fmt, ...)
{
  if (ctx->failed)
    return;
  ctx->failed = 1;
  char where[256];
  const char *file = ctx->nstack ? ctx->stack[ctx->nstack - 1] : "<script>";
  int row = at ? (int)at->state.row + 1 : 0;
  if (row > 0)
    snprintf (where, sizeof (where), "%s:%d", file, row);
  else
    snprintf (where, sizeof (where), "%s", file);
  char msg[384];
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (msg, sizeof (msg), fmt, ap);
  va_end (ap);
  /* include stack from the second frame on (§9) */
  char incstack[192] = "";
  size_t nl = 0;
  for (size_t i = ctx->nstack > 1 ? 1 : 0; i + 1 < ctx->nstack && nl + 1 < sizeof (incstack); i++)
    nl += (size_t)snprintf (incstack + nl, sizeof (incstack) - nl, "%s%s",
                            nl ? "->" : "", ctx->stack[i]);
  if (incstack[0])
    ccwld_error_set (ctx->e, code, "%s (%s; included from %s)", msg, where,
                     incstack);
  else
    ccwld_error_set (ctx->e, code, "%s (%s)", msg, where);
}

/* ================================================================
 * comment stripping — ld block comments become spaces; newlines are
 * preserved so diagnostics keep their line numbers
 * ================================================================ */

static char *
strip_comments (const char *src)
{
  size_t n = strlen (src);
  char *out = malloc (n + 1);
  if (!out)
    return NULL;
  size_t i = 0, j = 0;
  int in_str = 0;
  while (i < n)
    {
      if (!in_str && i + 1 < n && src[i] == '/' && src[i + 1] == '*')
        {
          out[j++] = ' ';
          i += 2;
          while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/'))
            {
              if (src[i] == '\n')
                out[j++] = '\n';
              i++;
            }
          i = i + 2 <= n ? i + 2 : n;
          out[j++] = ' ';
          continue;
        }
      if (src[i] == '"')
        in_str = !in_str;
      out[j++] = src[i++];
    }
  out[j] = 0;
  return out;
}

/* ================================================================
 * AST helpers (tags accumulate as "outer|inner|leaf-kind")
 * ================================================================ */

static int
tag_is (const mpc_ast_t *a, const char *rule)
{
  if (!a || !a->tag)
    return 0;
  size_t n = strlen (rule);
  if (strncmp (a->tag, rule, n))
    return 0;
  return a->tag[n] == 0 || a->tag[n] == '|';
}

static int
is_leaf (const mpc_ast_t *a)
{
  return a->children_num == 0;
}

static const char *
leaf_text (const mpc_ast_t *a)
{
  if (!is_leaf (a) || !a->contents || !a->contents[0])
    return NULL;
  return a->contents;
}

static mpc_ast_t *
child_rule (const mpc_ast_t *a, const char *rule)
{
  for (int i = 0; i < a->children_num; i++)
    if (tag_is (a->children[i], rule))
      return a->children[i];
  return NULL;
}

static mpc_ast_t *
child_rule_from (const mpc_ast_t *a, const char *rule, int from)
{
  for (int i = from; i < a->children_num; i++)
    if (tag_is (a->children[i], rule))
      return a->children[i];
  return NULL;
}

static int
index_of_child (const mpc_ast_t *parent, const mpc_ast_t *child)
{
  for (int i = 0; i < parent->children_num; i++)
    if (parent->children[i] == child)
      return i;
  return -1;
}

/* contents of the leaf under a named token rule (e.g. name_tok) */
static const char *
token_text (const mpc_ast_t *a, const char *rule)
{
  mpc_ast_t *c = child_rule (a, rule);
  if (!c)
    return NULL;
  while (!is_leaf (c) && c->children_num == 1)
    c = c->children[0];
  return leaf_text (c);
}

/* first line number found in the subtree (leaves carry state) */
static int
line_of (const mpc_ast_t *a)
{
  if (is_leaf (a))
    return (int)a->state.row + 1;
  for (int i = 0; i < a->children_num; i++)
    {
      int l = line_of (a->children[i]);
      if (l > 1)
        return l;
    }
  return 1;
}

/* ================================================================
 * the grammar
 * ================================================================ */

static mpc_parser_t *P_ld, *P_item, *P_command, *P_assign_stmt, *P_aop,
    *P_provide_stmt, *P_provide_hidden_stmt, *P_entry_c, *P_output_c,
    *P_output_format_c, *P_output_arch_c, *P_search_c, *P_startup_c,
    *P_input_c, *P_group_c, *P_include_c, *P_memory_c, *P_mem_def, *P_attr,
    *P_o_kw, *P_l_kw, *P_phdrs_c, *P_phdr_def, *P_ptype, *P_pflags,
    *P_version_c, *P_ver_block, *P_ver_entry, *P_sections_c, *P_sec_item,
    *P_sec_def, *P_sectype, *P_at_c, *P_align_c, *P_subalign_c,
    *P_sec_content, *P_keep_c, *P_input_sec, *P_sec_tail, *P_expr, *P_cond_e,
    *P_lor, *P_land, *P_bor, *P_bxor, *P_band, *P_eq, *P_rel, *P_shift,
    *P_add, *P_mul, *P_una, *P_primary, *P_call, *P_arglist, *P_fname,
    *P_number, *P_sym, *P_name_tok, *P_fname_tok, *P_filepat, *P_secpat,
    *P_secname;

static int parsers_ready = 0;

#define G(s) s "\n"

static const char *GRAMMAR = G ("ld  : /^/ <item>* /$/ ;")
    G ("item : <command> | <assign_stmt> | <provide_stmt> | <provide_hidden_stmt> ;")
    G ("command : <sections_c> | <memory_c> | <phdrs_c> | <version_c> | <entry_c>")
    G ("        | <output_c> | <output_format_c> | <output_arch_c> | <search_c>")
    G ("        | <startup_c> | <input_c> | <group_c> | <include_c> ;")
    G ("assign_stmt : <name_tok> <aop> <expr> ';' ;")
    G ("aop : \"<<=\" | \">>=\" | \"+=\" | \"-=\" | \"*=\" | \"/=\" | \"&=\" | \"|=\" | \"=\" ;")
    G ("provide_stmt : \"PROVIDE\" '(' <name_tok> '=' <expr> ')' ';' ;")
    G ("provide_hidden_stmt : \"PROVIDE_HIDDEN\" '(' <name_tok> '=' <expr> ')' ';' ;")
    G ("entry_c : \"ENTRY\" '(' <name_tok> ')' ;")
    G ("output_c : \"OUTPUT\" '(' <fname_tok> ')' ;")
    G ("output_format_c : \"OUTPUT_FORMAT\" '(' <fname_tok> ')' ;")
    G ("output_arch_c : \"OUTPUT_ARCH\" '(' <fname_tok> ')' ;")
    G ("search_c : \"SEARCH_DIR\" '(' <fname_tok> ')' ;")
    G ("startup_c : \"STARTUP\" '(' <fname_tok> ')' ;")
    G ("input_c : \"INPUT\" '(' (<fname_tok> (',' <fname_tok>)*)? ')' ;")
    G ("group_c : \"GROUP\" '(' (<fname_tok> (',' <fname_tok>)*)? ')' ;")
    G ("include_c : \"INCLUDE\" <fname_tok> ;")
    G ("memory_c : \"MEMORY\" '{' <mem_def>* '}' ;")
    G ("mem_def : <name_tok> <attr>? ':' <o_kw> '=' <expr> ',' <l_kw> '=' <expr> ';' ;")
    G ("attr : '(' /[rwxil]+/ ')' ;")
    G ("o_kw : \"ORIGIN\" | \"org\" | \"o\" ;")
    G ("l_kw : \"LENGTH\" | \"len\" | \"l\" ;")
    G ("phdrs_c : \"PHDRS\" '{' <phdr_def>* '}' ;")
    G ("phdr_def : <name_tok> <ptype> <pflags>? ';' ;")
    G ("ptype : \"PT_LOAD\" | \"PT_DYNAMIC\" | \"PT_INTERP\" | \"PT_NOTE\" | \"PT_NULL\" | \"PT_PHDR\" | \"PT_GNU_STACK\" ;")
    G ("pflags : \"FLAGS\" '(' /[rwx]+/ ')' ;")
    G ("version_c : \"VERSION\" '{' <ver_block>+ '}' ;")
    G ("ver_block : <name_tok> '{' <ver_entry>* '}' ';' ;")
    G ("ver_entry : (\"global\" | \"local\") ':' | <name_tok> <name_tok>? ';' ;")
    G ("sections_c : \"SECTIONS\" '{' <sec_item>* '}' ;")
    G ("sec_item : <sec_def> | <assign_stmt> | <provide_stmt> | <provide_hidden_stmt> ;")
    G ("sec_def : <secname> <sectype>? <expr>? ':' <at_c>? <align_c>? <subalign_c>? '{' <sec_content>* '}' <sec_tail>* ';' ;")
    G ("sectype : '(' \"NOLOAD\" ')' ;")
    G ("at_c : \"AT\" '(' <expr> ')' ;")
    G ("align_c : \"ALIGN\" '(' <expr> ')' ;")
    G ("subalign_c : \"SUBALIGN\" '(' <expr> ')' ;")
    G ("sec_content : <keep_c> | <input_sec> | <assign_stmt> | <provide_stmt> | <provide_hidden_stmt> ;")
    G ("keep_c : \"KEEP\" '(' <input_sec> ')' ;")
    G ("input_sec : <filepat> '(' <secpat> (',' <secpat>)* ')' ;")
    G ("sec_tail : '>' <name_tok> | \"AT\" '>' <name_tok> | ':' <name_tok> | '=' <expr> | ',' ;")
    G ("expr : <cond_e> ;")
    G ("cond_e : <lor> ('?' <expr> ':' <expr>)? ;")
    G ("lor : <land> (\"||\" <land>)* ;")
    G ("land : <bor> (\"&&\" <bor>)* ;")
    G ("bor : <bxor> (\"|\" <bxor>)* ;")
    G ("bxor : <band> (\"^\" <band>)* ;")
    G ("band : <eq> (\"&\" <eq>)* ;")
    G ("eq : <rel> ((\"==\" | \"!=\") <rel>)* ;")
    G ("rel : <shift> ((\"<=\" | \">=\" | \"<\" | \">\") <shift>)* ;")
    G ("shift : <add> ((\"<<\" | \">>\") <add>)* ;")
    G ("add : <mul> ((\"+\" | \"-\") <mul>)* ;")
    G ("mul : <una> ((\"*\" | \"/\" | \"%\") <una>)* ;")
    G ("una : (\"-\" | \"!\" | \"~\") <una> | <primary> ;")
    G ("primary : <number> | <call> | <sym> | '(' <expr> ')' ;")
    G ("call : <fname> '(' <arglist>? ')' ;")
    G ("arglist : <expr> (',' <expr>)* ;")
    G ("fname : \"ALIGN\" | \"ABSOLUTE\" | \"MAX\" | \"MIN\" | \"DEFINED\" | \"SIZEOF\" | \"ADDR\" | \"LOADADDR\" | \"ORIGIN\" | \"LENGTH\" | \"SEGMENT_START\" | \"MAXPAGESIZE\" | \"COMMONPAGESIZE\" | \"SIZEOF_HEADERS\" ;")
    G ("number : /0[xX][0-9a-fA-F]+[kKmMgG]?|[0-9]+[kKmMgG]?/ ;")
    G ("sym : /[A-Za-z_.$][A-Za-z0-9_.$]*/ ;")
    G ("name_tok : /[A-Za-z_.$*?][A-Za-z0-9_.$*?-]*/ ;")
    G ("fname_tok : /[A-Za-z0-9_./+$@~-]+/ ;")
    G ("filepat : /[A-Za-z0-9_.$?*$+/-]+/ ;")
    G ("secpat : /[A-Za-z0-9_.$?*]+/ ;")
    G ("secname : /[A-Za-z_.$*?][A-Za-z0-9_.$*?]*/ ;");

#undef G

static int
init_parsers (ccwld_error *e)
{
  if (parsers_ready)
    return 1;
  P_ld = mpc_new ("ld");
  P_item = mpc_new ("item");
  P_command = mpc_new ("command");
  P_assign_stmt = mpc_new ("assign_stmt");
  P_aop = mpc_new ("aop");
  P_provide_stmt = mpc_new ("provide_stmt");
  P_provide_hidden_stmt = mpc_new ("provide_hidden_stmt");
  P_entry_c = mpc_new ("entry_c");
  P_output_c = mpc_new ("output_c");
  P_output_format_c = mpc_new ("output_format_c");
  P_output_arch_c = mpc_new ("output_arch_c");
  P_search_c = mpc_new ("search_c");
  P_startup_c = mpc_new ("startup_c");
  P_input_c = mpc_new ("input_c");
  P_group_c = mpc_new ("group_c");
  P_include_c = mpc_new ("include_c");
  P_memory_c = mpc_new ("memory_c");
  P_mem_def = mpc_new ("mem_def");
  P_attr = mpc_new ("attr");
  P_o_kw = mpc_new ("o_kw");
  P_l_kw = mpc_new ("l_kw");
  P_phdrs_c = mpc_new ("phdrs_c");
  P_phdr_def = mpc_new ("phdr_def");
  P_ptype = mpc_new ("ptype");
  P_pflags = mpc_new ("pflags");
  P_version_c = mpc_new ("version_c");
  P_ver_block = mpc_new ("ver_block");
  P_ver_entry = mpc_new ("ver_entry");
  P_sections_c = mpc_new ("sections_c");
  P_sec_item = mpc_new ("sec_item");
  P_sec_def = mpc_new ("sec_def");
  P_sectype = mpc_new ("sectype");
  P_at_c = mpc_new ("at_c");
  P_align_c = mpc_new ("align_c");
  P_subalign_c = mpc_new ("subalign_c");
  P_sec_content = mpc_new ("sec_content");
  P_keep_c = mpc_new ("keep_c");
  P_input_sec = mpc_new ("input_sec");
  P_sec_tail = mpc_new ("sec_tail");
  P_expr = mpc_new ("expr");
  P_cond_e = mpc_new ("cond_e");
  P_lor = mpc_new ("lor");
  P_land = mpc_new ("land");
  P_bor = mpc_new ("bor");
  P_bxor = mpc_new ("bxor");
  P_band = mpc_new ("band");
  P_eq = mpc_new ("eq");
  P_rel = mpc_new ("rel");
  P_shift = mpc_new ("shift");
  P_add = mpc_new ("add");
  P_mul = mpc_new ("mul");
  P_una = mpc_new ("una");
  P_primary = mpc_new ("primary");
  P_call = mpc_new ("call");
  P_arglist = mpc_new ("arglist");
  P_fname = mpc_new ("fname");
  P_number = mpc_new ("number");
  P_sym = mpc_new ("sym");
  P_name_tok = mpc_new ("name_tok");
  P_fname_tok = mpc_new ("fname_tok");
  P_filepat = mpc_new ("filepat");
  P_secpat = mpc_new ("secpat");
  P_secname = mpc_new ("secname");

  mpc_err_t *err = mpca_lang (
      MPCA_LANG_DEFAULT, GRAMMAR, P_ld, P_item, P_command, P_assign_stmt,
      P_aop, P_provide_stmt, P_provide_hidden_stmt, P_entry_c, P_output_c,
      P_output_format_c, P_output_arch_c, P_search_c, P_startup_c, P_input_c,
      P_group_c, P_include_c, P_memory_c, P_mem_def, P_attr, P_o_kw, P_l_kw,
      P_phdrs_c, P_phdr_def, P_ptype, P_pflags, P_version_c, P_ver_block,
      P_ver_entry, P_sections_c, P_sec_item, P_sec_def, P_sectype, P_at_c,
      P_align_c, P_subalign_c, P_sec_content, P_keep_c, P_input_sec,
      P_sec_tail, P_expr, P_cond_e, P_lor, P_land, P_bor, P_bxor, P_band,
      P_eq, P_rel, P_shift, P_add, P_mul, P_una, P_primary, P_call,
      P_arglist, P_fname, P_number, P_sym, P_name_tok, P_fname_tok, P_filepat,
      P_secpat, P_secname, NULL);
  if (err)
    {
      char *s = mpc_err_string (err);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL,
                       "internal: ld-script grammar rejected by mpc: %s",
                       s ? s : "?");
      free (s);
      mpc_err_delete (err);
      return 0;
    }
  parsers_ready = 1;
  return 1;
}

/* ================================================================
 * number parsing (ld literal syntax: hex, decimal, octal, K/M/G)
 * ================================================================ */

static int
parse_num (const char *s, uint64_t *out)
{
  if (!s || !s[0])
    return 0;
  int base = 10;
  const char *q = s;
  if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
    {
      base = 16;
      q += 2;
    }
  else if (q[0] == '0' && q[1] >= '0' && q[1] <= '7')
    base = 8;
  char *end = NULL;
  uint64_t v = strtoull (q, &end, base);
  if (end == q)
    return 0;
  if (*end == 'k' || *end == 'K')
    {
      v *= 1024;
      end++;
    }
  else if (*end == 'm' || *end == 'M')
    {
      v *= 1024ull * 1024;
      end++;
    }
  else if (*end == 'g' || *end == 'G')
    {
      v *= 1024ull * 1024 * 1024;
      end++;
    }
  if (*end)
    return 0;
  *out = v;
  return 1;
}

/* ================================================================
 * deferred-expression construction from the parse tree (D-0039:
 * never evaluated here)
 * ================================================================ */

static ccwld_op_tag
bin_op (const char *t)
{
  static const struct
  {
    const char *s;
    ccwld_op_tag op;
  } tbl[] = {
    { "+", CCWLD_OP_ADD },       { "-", CCWLD_OP_SUB },
    { "*", CCWLD_OP_MUL },       { "/", CCWLD_OP_DIV },
    { "%", CCWLD_OP_MOD },       { "&", CCWLD_OP_AND },
    { "|", CCWLD_OP_OR },        { "^", CCWLD_OP_XOR },
    { "<<", CCWLD_OP_SHL },      { ">>", CCWLD_OP_SHR },
    { "==", CCWLD_OP_EQ },       { "!=", CCWLD_OP_NE },
    { "<", CCWLD_OP_LT },        { "<=", CCWLD_OP_LE },
    { ">", CCWLD_OP_GT },        { ">=", CCWLD_OP_GE },
    { "&&", CCWLD_OP_LAND },     { "||", CCWLD_OP_LOR },
  };
  for (size_t i = 0; i < sizeof (tbl) / sizeof (tbl[0]); i++)
    if (!strcmp (tbl[i].s, t))
      return tbl[i].op;
  return (ccwld_op_tag)0;
}

static ccwld_expr *
build_expr (mpc_ast_t *n, lower_ctx *ctx);

/* name operand of a name-taking call (SIZEOF/ADDR/…) */
static const char *
expr_name_arg (ccwld_expr *e)
{
  if (e && e->kind == CCWLD_EXPR_SYMBOL && e->name)
    return e->name;
  return NULL;
}

static ccwld_expr *
build_call (mpc_ast_t *n, lower_ctx *ctx)
{
  const char *fn = token_text (n, "fname");
  /* collect argument subtrees */
  ccwld_expr *args[3];
  size_t nargs = 0;
  for (int i = 0; i < n->children_num && nargs < 3; i++)
    {
      mpc_ast_t *c = n->children[i];
      if (tag_is (c, "arglist"))
        {
          for (int k = 0; k < c->children_num && nargs < 3; k++)
            if (tag_is (c->children[k], "expr"))
              {
                ccwld_expr *a = build_expr (c->children[k], ctx);
                if (!a)
                  return NULL;
                args[nargs++] = a;
              }
        }
    }
  if (!fn)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "malformed expression call");
      return NULL;
    }
  if (!strcmp (fn, "ALIGN"))
    {
      if (nargs == 1)
        return ccwld_expr_align_to (ccwld_expr_dot (), args[0]);
      if (nargs == 2) /* ALIGN(align, exp): exp aligned to boundary align */
        {
          ccwld_expr *r = ccwld_expr_align_to (args[1], args[0]);
          return r;
        }
      ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "ALIGN takes one or two arguments");
      return NULL;
    }
  if (!strcmp (fn, "ABSOLUTE"))
    {
      if (nargs != 1)
        goto arity;
      return args[0];
    }
  if (!strcmp (fn, "MAX") || !strcmp (fn, "MIN"))
    {
      if (nargs != 2)
        goto arity;
      return !strcmp (fn, "MAX") ? ccwld_expr_max (args[0], args[1])
                                 : ccwld_expr_min (args[0], args[1]);
    }
  if (!strcmp (fn, "DEFINED"))
    {
      if (nargs != 1)
        goto arity;
      const char *nm = expr_name_arg (args[0]);
      if (!nm)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, n,
                    "DEFINED takes a symbol name");
          return NULL;
        }
      ccwld_expr_free (args[0]);
      return ccwld_expr_defined (nm);
    }
  if (!strcmp (fn, "SIZEOF") || !strcmp (fn, "ADDR")
      || !strcmp (fn, "LOADADDR"))
    {
      if (nargs != 1)
        goto arity;
      const char *nm = expr_name_arg (args[0]);
      if (!nm)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "%s takes a section name", fn);
          return NULL;
        }
      ccwld_expr *r = !strcmp (fn, "SIZEOF") ? ccwld_expr_sizeof (nm)
                    : !strcmp (fn, "ADDR")   ? ccwld_expr_addr (nm)
                                             : ccwld_expr_loadaddr (nm);
      ccwld_expr_free (args[0]);
      return r;
    }
  if (!strcmp (fn, "ORIGIN") || !strcmp (fn, "LENGTH"))
    {
      if (nargs != 1)
        goto arity;
      const char *nm = expr_name_arg (args[0]);
      if (!nm)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "%s takes a region name", fn);
          return NULL;
        }
      ccwld_expr *r = !strcmp (fn, "ORIGIN") ? ccwld_expr_region_origin (nm)
                                             : ccwld_expr_region_length (nm);
      ccwld_expr_free (args[0]);
      return r;
    }
  if (!strcmp (fn, "SEGMENT_START"))
    {
      if (nargs != 2)
        goto arity;
      const char *nm = expr_name_arg (args[0]);
      if (!nm)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, n,
                    "SEGMENT_START takes a segment name and a default");
          return NULL;
        }
      ccwld_expr *r = ccwld_expr_segment_start2 (nm, args[1]);
      ccwld_expr_free (args[0]);
      return r;
    }
  if (!strcmp (fn, "MAXPAGESIZE") || !strcmp (fn, "COMMONPAGESIZE"))
    return ccwld_expr_int (0x1000);
  if (!strcmp (fn, "SIZEOF_HEADERS"))
    return ccwld_expr_sizeof_headers ();
  ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "unknown expression function '%s'", fn);
  return NULL;
arity:
  ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "%s: wrong argument count", fn);
  return NULL;
}

static ccwld_expr *
build_expr (mpc_ast_t *n, lower_ctx *ctx)
{
  if (ctx->failed)
    return NULL;

  /* leaf: number / symbol / dot */
  const char *t = leaf_text (n);
  if (t)
    {
      if (!strcmp (t, "."))
        return ccwld_expr_dot ();
      uint64_t v;
      if (parse_num (t, &v))
        return ccwld_expr_int (v);
      return ccwld_expr_symbol (t);
    }

  /* chain (single-child precedence node) */
  if (n->children_num == 1)
    return build_expr (n->children[0], ctx);

  /* call: first child is a <fname> rule node */
  if (tag_is (n->children[0], "fname"))
    return build_call (n, ctx);

  /* first child is a direct token: unary / grouping */
  const char *first = leaf_text (n->children[0]);
  if (first)
    {
      if (n->children_num >= 2
          && (!strcmp (first, "-") || !strcmp (first, "!")
              || !strcmp (first, "~")))
        {
          ccwld_expr *a = build_expr (n->children[1], ctx);
          if (!a)
            return NULL;
          if (!strcmp (first, "!"))
            return ccwld_expr_unary (CCWLD_OP_NOT, a);
          if (!strcmp (first, "~")) /* bitwise complement = XOR all-ones */
            return ccwld_expr_binary (CCWLD_OP_XOR, a, ccwld_expr_int (~(uint64_t)0));
          return ccwld_expr_unary (CCWLD_OP_NEG, a);
        }
      if (!strcmp (first, "("))
        {
          /* grouping: the single non-token child */
          for (int i = 1; i < n->children_num; i++)
            if (!is_leaf (n->children[i]) || !leaf_text (n->children[i]))
              {
                if (tag_is (n->children[i], "expr"))
                  return build_expr (n->children[i], ctx);
              }
          ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "empty parentheses in expression");
          return NULL;
        }
      /* call */
      return build_call (n, ctx);
    }

  /* operand-first: binary fold, or ternary when a '?' token appears */
  mpc_ast_t *qmark = NULL;
  for (int i = 1; i < n->children_num; i++)
    {
      const char *tok = leaf_text (n->children[i]);
      if (tok && !strcmp (tok, "?"))
        {
          qmark = n;
          break;
        }
    }
  if (qmark)
    {
      /* [test, '?', then, ':', else] */
      ccwld_expr *test = NULL, *then_e = NULL, *else_e = NULL;
      int stage = 0;
      for (int i = 0; i < n->children_num; i++)
        {
          const char *tok = leaf_text (n->children[i]);
          if (tok && (!strcmp (tok, "?") || !strcmp (tok, ":")))
            {
              stage++;
              continue;
            }
          if (stage == 0 && !test)
            {
              test = build_expr (n->children[i], ctx);
              if (!test)
                return NULL;
            }
          else if (stage == 1 && !then_e)
            {
              then_e = build_expr (n->children[i], ctx);
              if (!then_e)
                return NULL;
            }
          else if (stage >= 2 && !else_e)
            {
              else_e = build_expr (n->children[i], ctx);
              if (!else_e)
                return NULL;
            }
        }
      if (!test || !then_e || !else_e)
        {
          ccwld_expr_free (test);
          ccwld_expr_free (then_e);
          ccwld_expr_free (else_e);
          ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "malformed ?: expression");
          return NULL;
        }
      return ccwld_expr_cond (test, then_e, else_e);
    }

  ccwld_expr *left = build_expr (n->children[0], ctx);
  if (!left)
    return NULL;
  for (int i = 1; i + 1 < n->children_num; i += 2)
    {
      const char *op = leaf_text (n->children[i]);
      if (!op || !bin_op (op))
        continue; /* structural token (shouldn't appear) */
      ccwld_expr *right = build_expr (n->children[i + 1], ctx);
      if (!right)
        {
          ccwld_expr_free (left);
          return NULL;
        }
      left = ccwld_expr_binary (bin_op (op), left, right);
    }
  return left;
}

/* ================================================================
 * lowering
 * ================================================================ */

static int lower_items (mpc_ast_t *items, lower_ctx *ctx, int sec_idx);
static int lower_include (const char *path, lower_ctx *ctx, int sec_idx);

static char *
site_str (lower_ctx *ctx, const mpc_ast_t *at)
{
  char buf[256];
  const char *file = ctx->nstack ? ctx->stack[ctx->nstack - 1] : "<script>";
  snprintf (buf, sizeof (buf), "%s:%d", file, at ? line_of (at) : 0);
  return strdup (buf);
}

static void
set_output_str (char **field, const char *value)
{
  free (*field);
  *field = strdup (value);
}

/* an input-section description: file(pat,...) with optional KEEP */
static int
lower_input_sec (mpc_ast_t *n, lower_ctx *ctx, const char *out_sec, int keep)
{
  const char *file = token_text (n, "filepat");
  if (!file)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "input-section description is "
                                         "missing its file pattern");
      return 0;
    }
  const char *globs[64];
  size_t nglobs = 0;
  for (int i = 0; i < n->children_num && nglobs < 64; i++)
    {
      if (!tag_is (n->children[i], "secpat"))
        continue;
      mpc_ast_t *c = n->children[i];
      while (!is_leaf (c) && c->children_num == 1)
        c = c->children[0];
      const char *txt = leaf_text (c);
      if (txt)
        globs[nglobs++] = txt;
    }
  if (nglobs == 0)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, n,
                "input-section description '%s(...)' has no section patterns",
                file);
      return 0;
    }
  /* "*" file glob is stored canonically (plan serializer emits it) */
  return ccwld_plan_selector (ctx->p, out_sec, strcmp (file, "*") ? file : "*",
                              (char *const *)globs, nglobs, keep, ctx->e)
             ? 1
             : (ctx->failed = 1, 0);
}

static int
lower_assign (mpc_ast_t *n, lower_ctx *ctx, int sec_idx)
{
  const char *name = token_text (n, "name_tok");
  const char *aop = token_text (n, "aop");
  mpc_ast_t *ex = child_rule (n, "expr");
  if (!name || !aop || !ex)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "malformed assignment");
      return 0;
    }
  ccwld_expr *rhs = build_expr (ex, ctx);
  if (!rhs)
    return 0;

  /* compound ops desugar to `sym = sym <op> rhs` */
  if (strlen (aop) == 2 && aop[1] == '=')
    {
      ccwld_op_tag op = aop[0] == '<' ? CCWLD_OP_SHL
                        : aop[0] == '>' ? CCWLD_OP_SHR
                                        : bin_op ((char[]){ aop[0], 0 });
      if (!op)
        {
          ccwld_expr_free (rhs);
          ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "unsupported assignment "
                                              "operator '%s'",
                    aop);
          return 0;
        }
      rhs = ccwld_expr_binary (op, ccwld_expr_symbol (name), rhs);
    }

  char *site = site_str (ctx, n);
  int ok;
  if (!strcmp (name, "."))
    ok = ccwld_plan_dotstep (ctx->p, rhs, sec_idx, site, ctx->e);
  else
    ok = ccwld_plan_symbol_at (ctx->p, name, rhs, 0, 0, sec_idx, site,
                               ctx->e);
  free (site);
  if (!ok)
    ctx->failed = 1;
  return ok;
}

static int
lower_provide (mpc_ast_t *n, lower_ctx *ctx, int sec_idx, int hidden)
{
  const char *name = token_text (n, "name_tok");
  mpc_ast_t *ex = child_rule (n, "expr");
  if (!name || !ex)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "malformed PROVIDE");
      return 0;
    }
  ccwld_expr *rhs = build_expr (ex, ctx);
  if (!rhs)
    return 0;
  char *site = site_str (ctx, n);
  int ok = ccwld_plan_symbol_at (ctx->p, name, rhs, 1, hidden, sec_idx, site,
                                 ctx->e);
  free (site);
  if (!ok)
    ctx->failed = 1;
  return ok;
}

static int
lower_sec_def (mpc_ast_t *n, lower_ctx *ctx)
{
  const char *name = token_text (n, "secname");
  if (!name)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, n, "output section is missing a name");
      return 0;
    }
  if (!ccwld_plan_section (ctx->p, name, NULL, 1, NULL, NULL, ctx->e))
    {
      ctx->failed = 1;
      return 0;
    }
  int sec_idx = (int)ctx->p->nsecs - 1;

  /* (NOLOAD) */
  if (child_rule (n, "sectype")
      && !ccwld_plan_section_set_load (ctx->p, name, 0, ctx->e))
    {
      ctx->failed = 1;
      return 0;
    }

  /* address expression (before the ':') */
  mpc_ast_t *addr = child_rule (n, "expr");
  mpc_ast_t *at = child_rule (n, "at_c");
  mpc_ast_t *align = child_rule (n, "align_c");
  mpc_ast_t *sub = child_rule (n, "subalign_c");

  ccwld_expr *vma = NULL;
  if (addr)
    {
      vma = build_expr (addr, ctx);
      if (!vma)
        return 0;
    }
  if (align)
    {
      mpc_ast_t *aex = child_rule (align, "expr");
      ccwld_expr *a = aex ? build_expr (aex, ctx) : NULL;
      if (!a)
        return 0;
      /* after-colon ALIGN: the section start aligned to the boundary */
      ccwld_expr *base = vma ? vma : ccwld_expr_dot ();
      vma = ccwld_expr_align_to (base, a);
    }
  if (vma && !ccwld_plan_section_set_vma (ctx->p, name, vma, ctx->e))
    {
      ctx->failed = 1;
      return 0;
    }
  if (at)
    {
      mpc_ast_t *aex = child_rule (at, "expr");
      ccwld_expr *a = aex ? build_expr (aex, ctx) : NULL;
      if (!a)
        return 0;
      if (!ccwld_plan_section_set_at (ctx->p, name, a, ctx->e))
        {
          ctx->failed = 1;
          return 0;
        }
    }
  if (sub)
    {
      /* SUBALIGN is a plain number in the IR: constant expressions only */
      mpc_ast_t *sex = child_rule (sub, "expr");
      ccwld_expr *sx = sex ? build_expr (sex, ctx) : NULL;
      if (!sx)
        return 0;
      uint64_t v = 0;
      char *emsg = NULL;
      if (!ccwld_expr_eval (sx, ctx->p, 0, &v, &emsg))
        {
          ccwld_expr_free (sx);
          ctx_fail (ctx, CCWLD_EXIT_USAGE, sub,
                    "SUBALIGN needs a constant (here: %s)",
                    emsg ? emsg : "?");
          free (emsg);
          return 0;
        }
      ccwld_expr_free (sx);
      if (!ccwld_plan_section_set_subalign (ctx->p, name, v, ctx->e))
        {
          ctx->failed = 1;
          return 0;
        }
    }

  /* body: ordered selectors and statements */
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *c = n->children[i];
      if (tag_is (c, "keep_c"))
        {
          mpc_ast_t *inner = child_rule (c, "input_sec");
          if (!inner || !lower_input_sec (inner, ctx, name, 1))
            return ctx->failed ? 0 : (ctx_fail (ctx, CCWLD_EXIT_USAGE, c,
                                                "malformed KEEP"), 0);
        }
      else if (tag_is (c, "input_sec"))
        {
          if (!lower_input_sec (c, ctx, name, 0))
            return 0;
        }
      else if (tag_is (c, "assign_stmt"))
        {
          if (!lower_assign (c, ctx, sec_idx))
            return 0;
        }
      else if (tag_is (c, "provide_stmt"))
        {
          if (!lower_provide (c, ctx, sec_idx, 0))
            return 0;
        }
      else if (tag_is (c, "provide_hidden_stmt"))
        {
          if (!lower_provide (c, ctx, sec_idx, 1))
            return 0;
        }
    }

  /* trailers: >region | AT>region | :phdr | =fill */
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *c = n->children[i];
      if (!tag_is (c, "sec_tail"))
        continue;
      int has_at = 0, has_gt = 0, has_colon = 0;
      for (int q = 0; q < c->children_num; q++)
        {
          const char *tok = leaf_text (c->children[q]);
          if (tok && !strcmp (tok, "AT"))
            has_at = 1;
          else if (tok && !strcmp (tok, ">"))
            has_gt = 1;
          else if (tok && !strcmp (tok, ":"))
            has_colon = 1;
        }
      const char *nm = token_text (c, "name_tok");
      if (nm)
        {
          int ok = (has_at && has_gt)
                       ? ccwld_plan_section_set_at_region (ctx->p, name, nm,
                                                           ctx->e)
                   : has_colon
                       ? ccwld_plan_section_set_phdr (ctx->p, name, nm, ctx->e)
                   : has_gt
                       ? ccwld_plan_section_set_region (ctx->p, name, nm,
                                                        ctx->e)
                       : 0;
          if (!ok)
            {
              ctx->failed = 1;
              return 0;
            }
        }
      else
        {
          /* '= fillexp' (or a bare ',' separator: ignored) */
          mpc_ast_t *fex = child_rule (c, "expr");
          if (fex)
            {
              ccwld_expr *f = build_expr (fex, ctx);
              if (!f)
                return 0;
              if (!ccwld_plan_section_set_fill (ctx->p, name, f, ctx->e))
                {
                  ctx->failed = 1;
                  return 0;
                }
            }
        }
    }
  return 1;
}

static int
lower_sections (mpc_ast_t *n, lower_ctx *ctx)
{
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *c = n->children[i];
      if (tag_is (c, "sec_def"))
        {
          if (!lower_sec_def (c, ctx))
            return 0;
        }
      else if (tag_is (c, "assign_stmt"))
        {
          if (!lower_assign (c, ctx, -1))
            return 0;
        }
      else if (tag_is (c, "provide_stmt"))
        {
          if (!lower_provide (c, ctx, -1, 0))
            return 0;
        }
      else if (tag_is (c, "provide_hidden_stmt"))
        {
          if (!lower_provide (c, ctx, -1, 1))
            return 0;
        }
    }
  return 1;
}

static int
lower_memory (mpc_ast_t *n, lower_ctx *ctx)
{
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *d = n->children[i];
      if (!tag_is (d, "mem_def"))
        continue;
      const char *name = token_text (d, "name_tok");
      if (!name)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, d, "memory region is missing a name");
          return 0;
        }
      const char *attrs = NULL;
      mpc_ast_t *a = child_rule (d, "attr");
      if (a)
        {
          for (int k = 0; k < a->children_num; k++)
            {
              const char *txt = leaf_text (a->children[k]);
              if (txt && strchr ("rwxil", txt[0]) && !strchr ("()", txt[0]))
                attrs = txt;
            }
        }
      mpc_ast_t *e1 = child_rule (d, "expr");
      int i1 = e1 ? index_of_child (d, e1) : -1;
      mpc_ast_t *e2 = i1 >= 0 ? child_rule_from (d, "expr", i1 + 1) : NULL;
      if (!e1 || !e2)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, d,
                    "memory region '%s' needs ORIGIN and LENGTH", name);
          return 0;
        }
      ccwld_expr *oe = build_expr (e1, ctx);
      if (!oe)
        return 0;
      ccwld_expr *le = build_expr (e2, ctx);
      if (!le)
        return 0;
      /* ORIGIN/LENGTH are stored as plain numbers: constants only */
      uint64_t o = 0, l = 0;
      char *emsg = NULL;
      if (!ccwld_expr_eval (oe, ctx->p, 0, &o, &emsg))
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, d,
                    "ORIGIN of region '%s' must be constant (here: %s)", name,
                    emsg ? emsg : "?");
          free (emsg);
          return 0;
        }
      free (emsg);
      emsg = NULL;
      if (!ccwld_expr_eval (le, ctx->p, 0, &l, &emsg))
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, d,
                    "LENGTH of region '%s' must be constant (here: %s)", name,
                    emsg ? emsg : "?");
          free (emsg);
          return 0;
        }
      free (emsg);
      if (!ccwld_plan_memory (ctx->p, name, attrs, o, l, ctx->e))
        {
          ctx->failed = 1;
          return 0;
        }
    }
  return 1;
}

static uint32_t
pf_flags (const char *letters)
{
  uint32_t f = 0;
  for (; letters && *letters; letters++)
    {
      if (*letters == 'r')
        f |= 4;
      else if (*letters == 'w')
        f |= 2;
      else if (*letters == 'x')
        f |= 1;
    }
  return f;
}

static int
lower_phdrs (mpc_ast_t *n, lower_ctx *ctx)
{
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *d = n->children[i];
      if (!tag_is (d, "phdr_def"))
        continue;
      const char *name = token_text (d, "name_tok");
      const char *pt = token_text (d, "ptype");
      if (!name || !pt)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, d, "malformed PHDRS entry");
          return 0;
        }
      const char *type = !strcmp (pt, "PT_LOAD")      ? "LOAD"
                         : !strcmp (pt, "PT_DYNAMIC") ? "DYNAMIC"
                         : !strcmp (pt, "PT_INTERP")  ? "INTERP"
                         : !strcmp (pt, "PT_NOTE")    ? "NOTE"
                         : !strcmp (pt, "PT_NULL")    ? "NULL"
                         : !strcmp (pt, "PT_PHDR")    ? "PHDR"
                                                      : "GNU_STACK";
      uint32_t flags = 0;
      mpc_ast_t *fl = child_rule (d, "pflags");
      if (fl)
        for (int k = 0; k < fl->children_num; k++)
          {
            const char *txt = leaf_text (fl->children[k]);
            if (txt && strchr ("rwx", txt[0]))
              flags = pf_flags (txt);
          }
      if (!ccwld_plan_phdr (ctx->p, name, type, flags, 0x1000, ctx->e))
        {
          ctx->failed = 1;
          return 0;
        }
    }
  return 1;
}

static int
lower_version (mpc_ast_t *n, lower_ctx *ctx)
{
  int first_block = 1;
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *b = n->children[i];
      if (!tag_is (b, "ver_block"))
        continue;
      const char *ver = token_text (b, "name_tok");
      if (!ver)
        {
          ctx_fail (ctx, CCWLD_EXIT_USAGE, b, "malformed VERSION block");
          return 0;
        }
      int scope_global = 1;
      for (int k = 0; k < b->children_num; k++)
        {
          mpc_ast_t *e = b->children[k];
          if (!tag_is (e, "ver_entry"))
            continue;
          int is_scope = 0;
          for (int q = 0; q < e->children_num; q++)
            {
              const char *tok = leaf_text (e->children[q]);
              if (tok && (!strcmp (tok, "global") || !strcmp (tok, "local")))
                is_scope = 1;
            }
          if (is_scope)
            {
              const char *tok = leaf_text (e->children[0]);
              scope_global = tok && !strcmp (tok, "global");
              continue;
            }
          if (!scope_global)
            continue; /* local: *; entries recorded as the catch-all below */
          const char *sym = token_text (e, "name_tok");
          if (!sym)
            continue;
          if (!ccwld_plan_version (ctx->p, sym, ver, first_block, ctx->e))
            {
              ctx->failed = 1;
              return 0;
            }
        }
      first_block = 0;
    }
  return 1;
}

static int
lower_item (mpc_ast_t *n, lower_ctx *ctx)
{
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *c = n->children[i];
      if (tag_is (c, "entry_c"))
        set_output_str (&ctx->p->output.entry,
                        token_text (c, "name_tok") ? token_text (c, "name_tok")
                                                   : "");
      else if (tag_is (c, "output_c"))
        set_output_str (&ctx->p->options.out_name,
                        token_text (c, "fname_tok") ? token_text (c, "fname_tok")
                                                    : "");
      else if (tag_is (c, "output_format_c"))
        {
          const char *f = token_text (c, "fname_tok");
          if (f)
            {
              if (strncmp (f, "elf", 3))
                {
                  ctx_fail (ctx, CCWLD_EXIT_USAGE, c,
                            "unsupported output format '%s' (elf only)", f);
                  return 0;
                }
              set_output_str (&ctx->p->output.format, "elf");
            }
        }
      else if (tag_is (c, "output_arch_c"))
        {
          const char *a = token_text (c, "fname_tok");
          if (a)
            set_output_str (&ctx->p->target, a);
        }
      else if (tag_is (c, "search_c"))
        {
          const char *d = token_text (c, "fname_tok");
          if (!d || !ccwld_plan_search_path (ctx->p, d, ctx->e))
            {
              ctx->failed = 1;
              return 0;
            }
        }
      else if (tag_is (c, "startup_c"))
        {
          const char *f = token_text (c, "fname_tok");
          if (!f || !ccwld_plan_input (ctx->p, f, 0, 1, ctx->e))
            {
              ctx->failed = 1;
              return 0;
            }
        }
      else if (tag_is (c, "input_c"))
        {
          for (int k = 0; k < c->children_num; k++)
            {
              mpc_ast_t *f = c->children[k];
              if (!tag_is (f, "fname_tok"))
                continue;
              mpc_ast_t *q = f;
              while (!is_leaf (q) && q->children_num == 1)
                q = q->children[0];
              const char *name = leaf_text (q);
              if (!name || !ccwld_plan_input (ctx->p, name, 0, 0, ctx->e))
                {
                  ctx->failed = 1;
                  return 0;
                }
            }
        }
      else if (tag_is (c, "group_c"))
        {
          const char *paths[128];
          size_t npaths = 0;
          for (int k = 0; k < c->children_num && npaths < 128; k++)
            {
              mpc_ast_t *f = c->children[k];
              if (!tag_is (f, "fname_tok"))
                continue;
              mpc_ast_t *q = f;
              while (!is_leaf (q) && q->children_num == 1)
                q = q->children[0];
              paths[npaths++] = leaf_text (q);
            }
          if (npaths
              && !ccwld_plan_group (ctx->p, (const char **)paths, npaths, ctx->e))
            {
              ctx->failed = 1;
              return 0;
            }
        }
      else if (tag_is (c, "include_c"))
        {
          const char *f = token_text (c, "fname_tok");
          if (!f)
            {
              ctx_fail (ctx, CCWLD_EXIT_USAGE, c, "INCLUDE needs a file");
              return 0;
            }
          if (!lower_include (f, ctx, -1))
            return 0;
        }
      else if (tag_is (c, "memory_c"))
        {
          if (!lower_memory (c, ctx))
            return 0;
        }
      else if (tag_is (c, "sections_c"))
        {
          if (!lower_sections (c, ctx))
            return 0;
        }
      else if (tag_is (c, "phdrs_c"))
        {
          if (!lower_phdrs (c, ctx))
            return 0;
        }
      else if (tag_is (c, "version_c"))
        {
          if (!lower_version (c, ctx))
            return 0;
        }
      else if (tag_is (c, "assign_stmt"))
        {
          if (!lower_assign (c, ctx, -1))
            return 0;
        }
      else if (tag_is (c, "provide_stmt"))
        {
          if (!lower_provide (c, ctx, -1, 0))
            return 0;
        }
      else if (tag_is (c, "provide_hidden_stmt"))
        {
          if (!lower_provide (c, ctx, -1, 1))
            return 0;
        }
    }
  return 1;
}

static int
lower_items (mpc_ast_t *n, lower_ctx *ctx, int sec_idx)
{
  (void)sec_idx;
  for (int i = 0; i < n->children_num; i++)
    {
      mpc_ast_t *c = n->children[i];
      if (!c || !c->tag)
        continue;
      if (is_leaf (c))
        continue; /* anchor / structural tokens */
      if (!tag_is (c, "item"))
        continue;
      if (!lower_item (c, ctx))
        return 0;
    }
  return 1;
}

/* ================================================================
 * INCLUDE (§9: depth 32, cycle detection)
 * ================================================================ */

static char *
read_file (const char *path, size_t *len_out)
{
  FILE *f = fopen (path, "rb");
  if (!f)
    return NULL;
  fseek (f, 0, SEEK_END);
  long n = ftell (f);
  fseek (f, 0, SEEK_SET);
  if (n < 0)
    {
      fclose (f);
      return NULL;
    }
  char *buf = malloc ((size_t)n + 1);
  if (!buf)
    {
      fclose (f);
      return NULL;
    }
  if (fread (buf, 1, (size_t)n, f) != (size_t)n)
    {
      free (buf);
      fclose (f);
      return NULL;
    }
  fclose (f);
  buf[n] = 0;
  if (len_out)
    *len_out = (size_t)n;
  return buf;
}

static char *
join_dir (const char *file, const char *leaf)
{
  const char *slash = strrchr (file, '/');
  if (!slash)
    return strdup (leaf);
  size_t dirlen = (size_t)(slash - file) + 1;
  char *out = malloc (dirlen + strlen (leaf) + 1);
  if (!out)
    return NULL;
  memcpy (out, file, dirlen);
  strcpy (out + dirlen, leaf);
  return out;
}

static int
lower_include (const char *leaf, lower_ctx *ctx, int sec_idx)
{
  if (ctx->nstack >= CCWLD_LD_MAX_INCLUDE)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, NULL,
                "INCLUDE nesting exceeds depth %d",
                CCWLD_LD_MAX_INCLUDE);
      return 0;
    }
  /* resolve: absolute / including-file dir / search paths */
  char *cand = NULL;
  if (strchr (leaf, '/'))
    cand = strdup (leaf);
  else
    {
      const char *base = ctx->nstack ? ctx->stack[ctx->nstack - 1] : NULL;
      cand = base ? join_dir (base, leaf) : strdup (leaf);
    }
  char *data = read_file (cand, NULL);
  if (!data)
    {
      for (size_t i = 0; i < ctx->p->npaths && !data; i++)
        {
          char *p2 = malloc (strlen (ctx->p->paths[i]) + strlen (leaf) + 2);
          if (!p2)
            continue;
          sprintf (p2, "%s/%s", ctx->p->paths[i], leaf);
          data = read_file (p2, NULL);
          if (data)
            {
              free (cand);
              cand = p2;
            }
          else
            free (p2);
        }
    }
  if (!data)
    {
      ctx_fail (ctx, CCWLD_EXIT_USAGE, NULL,
                "cannot open included file '%s'", leaf);
      free (cand);
      return 0;
    }
  /* cycle detection */
  for (size_t i = 0; i < ctx->nstack; i++)
    if (!strcmp (ctx->stack[i], cand))
      {
        ctx_fail (ctx, CCWLD_EXIT_USAGE, NULL,
                  "INCLUDE cycle: '%s' is already being processed", cand);
        free (cand);
        free (data);
        return 0;
      }
  ctx->stack[ctx->nstack++] = cand;

  char *clean = strip_comments (data);
  free (data);
  if (!clean)
    {
      ctx_fail (ctx, CCWLD_EXIT_INTERNAL, NULL, "out of memory");
      return 0;
    }
  mpc_result_t r;
  if (!mpc_parse (cand, clean, P_ld, &r))
    {
      char *s = mpc_err_string (r.error);
      ccwld_error_set (ctx->e, CCWLD_EXIT_USAGE, "%s (included from %s)", s,
                       ctx->nstack > 1 ? ctx->stack[ctx->nstack - 2]
                                       : "<script>");
      free (s);
      mpc_err_delete (r.error);
      free (clean);
      ctx->failed = 1;
      return 0;
    }
  int ok = lower_items (r.output, ctx, sec_idx);
  mpc_ast_delete (r.output);
  free (clean);
  free (ctx->stack[--ctx->nstack]);
  return ok;
}

/* ================================================================
 * entry point
 * ================================================================ */

int
ccwld_run_ldscript (const char *script, const char *script_path,
                    const char *target, const ccwld_driver_defs *extra,
                    ccwld_plan **out, ccwld_error *e)
{
  if (!out)
    {
      ccwld_error_set (e, CCWLD_EXIT_USAGE, "invalid ld-script request");
      return 0;
    }
  *out = NULL;
  if (!script)
    {
      ccwld_error_set (e, CCWLD_EXIT_USAGE, "empty ld-script");
      return 0;
    }
  if (!init_parsers (e))
    return 0;

  ccwld_plan *p = ccwld_plan_new (target ? target : "unknown");
  if (!p)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  ccwld_plan_set_frontend (p, "ldscript");

  ccwld_output def;
  memset (&def, 0, sizeof (def));
  def.kind = strdup ("exe");
  def.format = strdup ("elf");
  if (!ccwld_plan_output (p, &def, e))
    {
      free (def.kind);
      free (def.format);
      ccwld_plan_free (p);
      return 0;
    }
  free (def.kind);
  free (def.format);

  /* driver-level declarations land before the script body (§2.1) */
  if (!ccwld_apply_driver_defs (p, extra, e))
    {
      ccwld_plan_free (p);
      return 0;
    }

  char *clean = strip_comments (script);
  if (!clean)
    {
      ccwld_plan_free (p);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }

  lower_ctx ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.p = p;
  ctx.e = e;
  ctx.stack[ctx.nstack++] = strdup (script_path ? script_path : "<script>");

  mpc_result_t r;
  const char *name = script_path ? script_path : "<script>";
  if (!mpc_parse (name, clean, P_ld, &r))
    {
      char *s = mpc_err_string (r.error);
      ccwld_error_set (e, CCWLD_EXIT_USAGE, "%s: %s", name,
                       s ? s : "parse error");
      free (s);
      mpc_err_delete (r.error);
      goto fail;
    }
  {
    int ok = lower_items (r.output, &ctx, -1);
    mpc_ast_delete (r.output);
    if (!ok)
      goto fail;
  }
  free (ctx.stack[0]);
  free (clean);
  /* GNU-style: the command-line -e wins over the script's ENTRY */
  if (extra && extra->entry
      && !ccwld_driver_entry_override (p, extra->entry, e))
    {
      ccwld_plan_free (p);
      return 0;
    }
  if (!ccwld_plan_seal (p, e))
    {
      ccwld_plan_free (p);
      return 0;
    }
  *out = p;
  return 1;

fail:
  free (ctx.stack[0]);
  free (clean);
  ccwld_plan_free (p);
  return 0;
}
