/* Parthia core compiler: surface S-expression -> core AST.
 *
 * This is the single compile stage shared by both execution modes: AoT
 * (ccw_sml_parthia_run) compiles the whole program; the JIT phrase engine
 * (ccw_sml_parthia_eval) compiles one topdec group at a time and caches the
 * result.  The parser has already resolved infix fixity, so this stage only
 * desugars: fixity-resolved (infix op l r) nodes become curried
 * applications, fun declarations become nested fn/case matchers, type
 * annotations are erased, and module language forms become structure/
 * functor operations.  Output is a pure function of the surface text
 * (D-0052); generated names come from a per-runtime counter. */

#include "parthia_rt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  prt *rt;
  int failed;
  char err[256];
} pcx;

static void
cx_fail (pcx *cx, const char *fmt, ...)
{
  va_list ap;
  if (cx->failed)
    return;
  cx->failed = 1;
  va_start (ap, fmt);
  vsnprintf (cx->err, sizeof (cx->err), fmt, ap);
  va_end (ap);
}

/* ---------- psx helpers ---------- */

static int
sx_is (const psx *n, const char *tag)
{
  return n != NULL && n->is_list && n->count >= 1 && !n->items[0]->is_list
         && strcmp (n->items[0]->atom, tag) == 0;
}

static const char *
sx_tag (const psx *n)
{
  if (n == NULL || !n->is_list || n->count == 0 || n->items[0]->is_list)
    return NULL;
  return n->items[0]->atom;
}

static const char *
sx_atom (const psx *n)
{
  return n != NULL && !n->is_list ? n->atom : NULL;
}

static int
sx_all_atoms (const psx *n, size_t from)
{
  size_t i;
  for (i = from; i < n->count; i++)
    if (n->items[i]->is_list)
      return 0;
  return 1;
}

static int
tag_has_suffix (const char *tag, const char *suffix)
{
  size_t tl = strlen (tag), sl = strlen (suffix);
  return tl >= sl && strcmp (tag + tl - sl, suffix) == 0;
}

static int
is_ty_tag (const char *tag)
{
  static const char *const names[]
      = { "tyvar", "tycon", "longtycon",      "lab",     "tyvarseq",
          "tyseq", "tyrow", "ellipsis_tyrow", "typbind", "withtype" };
  size_t i;
  if (tag_has_suffix (tag, "_ty"))
    return 1;
  for (i = 0; i < sizeof (names) / sizeof (names[0]); i++)
    if (strcmp (tag, names[i]) == 0)
      return 1;
  return 0;
}

static int
is_dec_tag (const char *tag)
{
  static const char *const names[]
      = { "val_dec",      "fun_dec",     "type_dec",      "datatype_dec",
          "datarepl_dec", "abstype_dec", "exception_dec", "local_dec",
          "open_dec",     "infix",       "infixr",        "nonfix",
          "do_dec" };
  size_t i;
  for (i = 0; i < sizeof (names) / sizeof (names[0]); i++)
    if (strcmp (tag, names[i]) == 0)
      return 1;
  return 0;
}

static int
is_strexp_tag (const char *tag)
{
  return strcmp (tag, "struct") == 0 || strcmp (tag, "strid") == 0
         || strcmp (tag, "constrain") == 0 || strcmp (tag, "fctapp") == 0
         || strcmp (tag, "let-struct") == 0;
}

/* Splits a longvid/longtycon atom "A.B.c" into arena segments. */
static char **
split_path (pcx *cx, const char *text, size_t *out_len)
{
  size_t n = 1, i;
  const char *p;
  char **path;
  for (p = text; *p; p++)
    if (*p == '.')
      n++;
  path = (char **)pa_alloc (cx->rt, n * sizeof (*path));
  if (path == NULL)
    {
      cx_fail (cx, "out of memory");
      return NULL;
    }
  p = text;
  for (i = 0; i < n; i++)
    {
      const char *dot = strchr (p, '.');
      size_t len = dot != NULL ? (size_t)(dot - p) : strlen (p);
      path[i] = pa_strndup (cx->rt, p, len);
      if (path[i] == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      p += len + (dot != NULL ? 1u : 0u);
    }
  *out_len = n;
  return path;
}

static char *
cx_gensym (pcx *cx)
{
  char buf[32];
  snprintf (buf, sizeof (buf), "g$%u", cx->rt->gensym++);
  return pa_strdup (cx->rt, buf);
}

/* ---------- special constants ---------- */

static int
hex_value (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* Unescapes the body of an SML string/character constant (quotes already
 * *not* included in [text, text+len)).  Returns arena bytes; *out_len set. */
static char *
unescape (pcx *cx, const char *text, size_t len, size_t *out_len)
{
  char *out = (char *)pa_alloc (cx->rt, len + 1u);
  size_t i = 0, o = 0;
  if (out == NULL)
    {
      cx_fail (cx, "out of memory");
      return NULL;
    }
  while (i < len)
    {
      char c = text[i];
      if (c != '\\')
        {
          out[o++] = c;
          i++;
          continue;
        }
      i++;
      if (i >= len)
        break;
      c = text[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
          /* formatting gap: \ ws* \ */
          while (i < len && text[i] != '\\')
            i++;
          i++; /* closing backslash */
          continue;
        }
      if (c >= '0' && c <= '9')
        {
          int v = 0, k;
          for (k = 0; k < 3 && i < len && text[i] >= '0' && text[i] <= '9';
               k++, i++)
            v = v * 10 + (text[i] - '0');
          out[o++] = (char)(v & 0xff);
          continue;
        }
      if (c == 'u' && i + 4 < len)
        {
          int v = 0, k, h;
          for (k = 1; k <= 4; k++)
            {
              h = hex_value (text[i + k]);
              if (h < 0)
                break;
              v = v * 16 + h;
            }
          if (k > 4)
            {
              out[o++] = (char)(v & 0xff);
              i += 5;
              continue;
            }
        }
      if (c == '^' && i + 1 < len)
        {
          out[o++] = (char)(text[i + 1] & 0x1f);
          i += 2;
          continue;
        }
      switch (c)
        {
        case 'a':
          out[o++] = '\a';
          break;
        case 'b':
          out[o++] = '\b';
          break;
        case 't':
          out[o++] = '\t';
          break;
        case 'n':
          out[o++] = '\n';
          break;
        case 'v':
          out[o++] = '\v';
          break;
        case 'f':
          out[o++] = '\f';
          break;
        case 'r':
          out[o++] = '\r';
          break;
        case '"':
          out[o++] = '"';
          break;
        case '\\':
          out[o++] = '\\';
          break;
        default:
          out[o++] = c;
          break;
        }
      i++;
    }
  out[o] = '\0';
  *out_len = o;
  return out;
}

static pv *
parse_scon (pcx *cx, const char *t)
{
  size_t len = strlen (t);
  pv *v;
  if (len == 0)
    {
      cx_fail (cx, "empty special constant");
      return NULL;
    }
  if (t[0] == '"')
    {
      size_t out_len = 0;
      char *body;
      if (len < 2 || t[len - 1] != '"')
        {
          cx_fail (cx, "unterminated string constant %s", t);
          return NULL;
        }
      body = unescape (cx, t + 1, len - 2, &out_len);
      if (body == NULL)
        return NULL;
      v = pa_new (cx->rt, PV_STRING);
      if (v != NULL)
        {
          v->s.data = body;
          v->s.len = out_len;
        }
      return v;
    }
  if (t[0] == '#' && len >= 3 && t[1] == '"')
    {
      size_t out_len = 0;
      char *body = unescape (cx, t + 2, len - 3, &out_len);
      if (body == NULL)
        return NULL;
      if (out_len == 0)
        {
          cx_fail (cx, "empty character constant");
          return NULL;
        }
      v = pa_new (cx->rt, PV_CHAR);
      if (v != NULL)
        v->ch = (unsigned char)body[0];
      return v;
    }
  if (len >= 2 && t[0] == '0' && t[1] == 'w')
    {
      /* word constant: 0wN, 0wxH, 0wbB */
      char buf[128];
      size_t o = 0, i;
      int base = 10;
      const char *digits = t + 2;
      unsigned long long value;
      if (digits[0] == 'x')
        {
          base = 16;
          digits++;
        }
      else if (digits[0] == 'b')
        {
          base = 2;
          digits++;
        }
      for (i = 0; digits[i] && o + 1 < sizeof (buf); i++)
        if (digits[i] != '_')
          buf[o++] = digits[i];
      buf[o] = '\0';
      value = strtoull (buf, NULL, base);
      v = pa_new (cx->rt, PV_WORD);
      if (v != NULL)
        v->w = value;
      return v;
    }
  {
    int is_real = 0;
    int is_hex = 0;
    size_t i;
    char buf[128];
    size_t o = 0;
    for (i = 0; t[i]; i++)
      {
        if (t[i] == 'x' || t[i] == 'X')
          is_hex = 1;
        if (!is_hex && (t[i] == '.' || t[i] == 'e' || t[i] == 'E'))
          is_real = 1;
      }
    for (i = 0; t[i] && o + 1 < sizeof (buf); i++)
      {
        char c = t[i];
        if (c == '_')
          continue;
        if (c == '~')
          c = '-';
        buf[o++] = c;
      }
    buf[o] = '\0';
    if (is_real)
      {
        v = pa_new (cx->rt, PV_REAL);
        if (v != NULL)
          v->r = strtod (buf, NULL);
        return v;
      }
    v = pa_new (cx->rt, PV_INT);
    if (v != NULL)
      {
        if (is_hex)
          {
            int neg = 0;
            const char *digits = buf;
            long long value;
            if (digits[0] == '-')
              {
                neg = 1;
                digits++;
              }
            value = (long long)strtoull (digits, NULL, 16);
            v->i = neg ? -value : value;
          }
        else
          v->i = strtoll (buf, NULL, 10);
      }
    return v;
  }
}

/* ---------- forward declarations ---------- */

static pa_exp *cx_exp (pcx *cx, psx *n);
static pa_pat *cx_pat (pcx *cx, psx *n);
static pa_dec *cx_dec (pcx *cx, psx *n);
static pa_strdec *cx_strdec (pcx *cx, psx *n);
static pa_strexp *cx_strexp (pcx *cx, psx *n);

static pa_exp *
mk_exp (pcx *cx, int kind)
{
  pa_exp *e = (pa_exp *)pa_alloc (cx->rt, sizeof (*e));
  if (e == NULL)
    cx_fail (cx, "out of memory");
  else
    memset (e, 0, sizeof (*e));
  if (e != NULL)
    e->kind = kind;
  return e;
}

static pa_pat *
mk_pat (pcx *cx, int kind)
{
  pa_pat *p = (pa_pat *)pa_alloc (cx->rt, sizeof (*p));
  if (p == NULL)
    cx_fail (cx, "out of memory");
  else
    memset (p, 0, sizeof (*p));
  if (p != NULL)
    p->kind = kind;
  return p;
}

static pa_dec *
mk_dec (pcx *cx, int kind)
{
  pa_dec *d = (pa_dec *)pa_alloc (cx->rt, sizeof (*d));
  if (d == NULL)
    cx_fail (cx, "out of memory");
  else
    memset (d, 0, sizeof (*d));
  if (d != NULL)
    d->kind = kind;
  return d;
}

/* Compiles children [from, to) of `parent` into an arena array using the
 * given element compiler. */
#define CX_ARRAY(cx, parent, from, to, out, elem_type, compile_one)           \
  do                                                                          \
    {                                                                         \
      size_t cx_i, cx_n = (size_t)((to) - (from));                            \
      elem_type **cx_arr = (elem_type **)pa_alloc (                           \
          (cx)->rt, (cx_n ? cx_n : 1u) * sizeof (*cx_arr));                   \
      if (cx_arr == NULL)                                                     \
        {                                                                     \
          cx_fail ((cx), "out of memory");                                    \
          return NULL;                                                        \
        }                                                                     \
      for (cx_i = 0; cx_i < cx_n; cx_i++)                                     \
        {                                                                     \
          cx_arr[cx_i] = compile_one ((cx), (parent)->items[(from) + cx_i]);  \
          if ((cx)->failed)                                                   \
            return NULL;                                                      \
        }                                                                     \
      (out) = cx_arr;                                                         \
    }                                                                         \
  while (0)

static pa_exp *
cx_vid_exp (pcx *cx, const char *name)
{
  pa_exp *e = mk_exp (cx, PE_VID);
  if (e == NULL)
    return NULL;
  e->path = split_path (cx, name, &e->path_len);
  if (cx->failed)
    return NULL;
  return e;
}

/* ---------- rules (matches) ---------- */

static int
cx_rules (pcx *cx, psx *parent, size_t from, pa_rule **out, size_t *out_n)
{
  size_t n = parent->count - from, i;
  pa_rule *rules
      = (pa_rule *)pa_alloc (cx->rt, (n ? n : 1u) * sizeof (*rules));
  if (rules == NULL)
    {
      cx_fail (cx, "out of memory");
      return 0;
    }
  for (i = 0; i < n; i++)
    {
      psx *m = parent->items[from + i];
      if (!sx_is (m, "mrule") || m->count < 3)
        {
          cx_fail (cx, "malformed match rule");
          return 0;
        }
      rules[i].pat = cx_pat (cx, m->items[1]);
      rules[i].body = cx_exp (cx, m->items[m->count - 1]);
      if (cx->failed)
        return 0;
    }
  *out = rules;
  *out_n = n;
  return 1;
}

/* ---------- expressions ---------- */

static pa_exp *
cx_exp (pcx *cx, psx *n)
{
  const char *tag = sx_tag (n);
  pa_exp *e;
  if (cx->failed)
    return NULL;
  if (tag == NULL)
    {
      cx_fail (cx, "malformed expression node");
      return NULL;
    }
  if (strcmp (tag, "scon") == 0)
    {
      if (n->count < 2)
        {
          cx_fail (cx, "malformed constant");
          return NULL;
        }
      e = mk_exp (cx, PE_LIT);
      if (e == NULL)
        return NULL;
      e->lit = parse_scon (cx, n->items[1]->atom);
      return cx->failed ? NULL : e;
    }
  if (strcmp (tag, "vid") == 0)
    {
      if (n->count < 2)
        {
          cx_fail (cx, "malformed value identifier");
          return NULL;
        }
      return cx_vid_exp (cx, n->items[1]->atom);
    }
  if (strcmp (tag, "record_exp") == 0)
    {
      size_t i, nr = n->count - 1;
      e = mk_exp (cx, PE_RECORD);
      if (e == NULL)
        return NULL;
      e->labs = (char **)pa_alloc (cx->rt, (nr ? nr : 1u) * sizeof (char *));
      e->items
          = (pa_exp **)pa_alloc (cx->rt, (nr ? nr : 1u) * sizeof (pa_exp *));
      if (e->labs == NULL || e->items == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      e->count = nr;
      for (i = 0; i < nr; i++)
        {
          psx *row = n->items[1 + i];
          if (sx_is (row, "exprow") && row->count >= 3)
            {
              e->labs[i] = pa_strdup (cx->rt, row->items[1]->atom);
              e->items[i] = cx_exp (cx, row->items[2]);
            }
          else if (sx_is (row, "labvar_exprow") && row->count >= 2)
            {
              e->labs[i] = pa_strdup (cx->rt, row->items[1]->atom);
              e->items[i] = cx_vid_exp (cx, row->items[1]->atom);
            }
          else
            {
              cx_fail (cx, "unsupported record row (ellipsis?)");
              return NULL;
            }
          if (cx->failed)
            return NULL;
        }
      return e;
    }
  if (strcmp (tag, "recordsel_exp") == 0)
    {
      if (n->count < 2)
        {
          cx_fail (cx, "malformed record selector");
          return NULL;
        }
      e = mk_exp (cx, PE_SEL);
      if (e == NULL)
        return NULL;
      e->sel = pa_strdup (cx->rt, n->items[1]->atom);
      return e;
    }
  if (strcmp (tag, "unit_exp") == 0)
    return mk_exp (cx, PE_UNIT);
  if (strcmp (tag, "tuple_exp") == 0 || strcmp (tag, "vec_exp") == 0)
    {
      e = mk_exp (cx, PE_TUPLE);
      if (e == NULL)
        return NULL;
      CX_ARRAY (cx, n, 1, n->count, e->items, pa_exp, cx_exp);
      e->count = n->count - 1;
      return e;
    }
  if (strcmp (tag, "list_exp") == 0)
    {
      size_t i;
      for (i = 1; i < n->count; i++)
        if (sx_is (n->items[i], "ellipsis_listexp"))
          {
            cx_fail (cx, "unsupported list ellipsis");
            return NULL;
          }
      e = mk_exp (cx, PE_LIST);
      if (e == NULL)
        return NULL;
      CX_ARRAY (cx, n, 1, n->count, e->items, pa_exp, cx_exp);
      e->count = n->count - 1;
      return e;
    }
  if (strcmp (tag, "sequence_exp") == 0)
    {
      e = mk_exp (cx, PE_SEQ);
      if (e == NULL)
        return NULL;
      CX_ARRAY (cx, n, 1, n->count, e->items, pa_exp, cx_exp);
      e->count = n->count - 1;
      return e;
    }
  if (strcmp (tag, "let_exp") == 0)
    {
      psx *local_part = n->count > 1 ? n->items[1] : NULL;
      psx *in_part = n->count > 2 ? n->items[2] : NULL;
      pa_exp *body;
      e = mk_exp (cx, PE_LET);
      if (e == NULL)
        return NULL;
      if (sx_is (local_part, "local") && sx_is (in_part, "in"))
        {
          CX_ARRAY (cx, local_part, 1, local_part->count, e->decs, pa_dec,
                    cx_dec);
          e->ndecs = local_part->count - 1;
          body = mk_exp (cx, PE_SEQ);
          if (body == NULL)
            return NULL;
          CX_ARRAY (cx, in_part, 1, in_part->count, body->items, pa_exp,
                    cx_exp);
          body->count = in_part->count - 1;
        }
      else
        {
          size_t i;
          e->ndecs = n->count > 2 ? n->count - 2 : 0;
          e->decs = (pa_dec **)pa_alloc (cx->rt, (e->ndecs ? e->ndecs : 1u)
                                                     * sizeof (*e->decs));
          if (e->decs == NULL)
            {
              cx_fail (cx, "out of memory");
              return NULL;
            }
          for (i = 0; i < e->ndecs; i++)
            e->decs[i] = cx_dec (cx, n->items[1 + i]);
          body = cx_exp (cx, n->items[n->count - 1]);
        }
      e->a = body;
      return e;
    }
  if (strcmp (tag, "paren_exp") == 0)
    return n->count > 1 ? cx_exp (cx, n->items[1]) : mk_exp (cx, PE_UNIT);
  if (strcmp (tag, "app") == 0)
    {
      e = mk_exp (cx, PE_APP);
      if (e == NULL)
        return NULL;
      CX_ARRAY (cx, n, 1, n->count, e->items, pa_exp, cx_exp);
      e->count = n->count - 1;
      return e;
    }
  if (strcmp (tag, "infix") == 0)
    {
      /* Fixity-resolved binary application: curried (op l) r. */
      if (n->count != 4)
        {
          cx_fail (cx, "malformed infix application");
          return NULL;
        }
      e = mk_exp (cx, PE_APP);
      if (e == NULL)
        return NULL;
      e->items = (pa_exp **)pa_alloc (cx->rt, 3u * sizeof (*e->items));
      if (e->items == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      e->items[0] = cx_vid_exp (cx, n->items[1]->atom);
      e->items[1] = cx_exp (cx, n->items[2]);
      e->items[2] = cx_exp (cx, n->items[3]);
      e->count = 3;
      return cx->failed ? NULL : e;
    }
  if (strcmp (tag, "typed_exp") == 0)
    return cx_exp (cx,
                   n->items[1]); /* types are erased (signature-erased core) */
  if (strcmp (tag, "conj_exp") == 0 || strcmp (tag, "disj_exp") == 0)
    {
      if (n->count < 3)
        {
          cx_fail (cx, "malformed boolean connective");
          return NULL;
        }
      e = mk_exp (cx, strcmp (tag, "conj_exp") == 0 ? PE_CONJ : PE_DISJ);
      if (e == NULL)
        return NULL;
      e->a = cx_exp (cx, n->items[1]);
      e->b = cx_exp (cx, n->items[2]);
      return cx->failed ? NULL : e;
    }
  if (strcmp (tag, "handle_exp") == 0)
    {
      e = mk_exp (cx, PE_HANDLE);
      if (e == NULL)
        return NULL;
      e->a = cx_exp (cx, n->items[1]);
      if (cx->failed)
        return NULL;
      if (!cx_rules (cx, n, 2, &e->rules, &e->nrules))
        return NULL;
      return e;
    }
  if (strcmp (tag, "raise_exp") == 0)
    {
      e = mk_exp (cx, PE_RAISE);
      if (e == NULL)
        return NULL;
      e->a = cx_exp (cx, n->items[1]);
      return cx->failed ? NULL : e;
    }
  if (strcmp (tag, "cond_exp") == 0)
    {
      if (n->count < 3)
        {
          cx_fail (cx, "malformed conditional");
          return NULL;
        }
      e = mk_exp (cx, PE_IF);
      if (e == NULL)
        return NULL;
      e->a = cx_exp (cx, n->items[1]);
      e->b = cx_exp (cx, n->items[2]);
      e->c = n->count > 3 ? cx_exp (cx, n->items[3]) : NULL;
      return cx->failed ? NULL : e;
    }
  if (strcmp (tag, "iter_exp") == 0)
    {
      e = mk_exp (cx, PE_WHILE);
      if (e == NULL)
        return NULL;
      e->a = cx_exp (cx, n->items[1]);
      e->b = cx_exp (cx, n->items[2]);
      return cx->failed ? NULL : e;
    }
  if (strcmp (tag, "case_exp") == 0)
    {
      e = mk_exp (cx, PE_CASE);
      if (e == NULL)
        return NULL;
      e->a = cx_exp (cx, n->items[1]);
      if (cx->failed)
        return NULL;
      if (!cx_rules (cx, n, 2, &e->rules, &e->nrules))
        return NULL;
      return e;
    }
  if (strcmp (tag, "fn_exp") == 0)
    {
      e = mk_exp (cx, PE_FN);
      if (e == NULL)
        return NULL;
      if (!cx_rules (cx, n, 1, &e->rules, &e->nrules))
        return NULL;
      return e;
    }
  cx_fail (cx, "unsupported expression form (%s)", tag);
  return NULL;
}

/* ---------- patterns ---------- */

static pa_pat *
cx_pat (pcx *cx, psx *n)
{
  const char *tag;
  pa_pat *p;
  if (cx->failed)
    return NULL;
  if (!n->is_list)
    {
      if (strcmp (n->atom, "_") == 0)
        return mk_pat (cx, PP_WILD);
      /* A bare atom in pattern position is a value identifier. */
      p = mk_pat (cx, PP_VID);
      if (p == NULL)
        return NULL;
      p->path = split_path (cx, n->atom, &p->path_len);
      return cx->failed ? NULL : p;
    }
  tag = sx_tag (n);
  if (tag == NULL)
    {
      cx_fail (cx, "malformed pattern node");
      return NULL;
    }
  if (strcmp (tag, "scon") == 0)
    {
      p = mk_pat (cx, PP_LIT);
      if (p == NULL)
        return NULL;
      p->lit = parse_scon (cx, n->items[1]->atom);
      return cx->failed ? NULL : p;
    }
  if (strcmp (tag, "vid") == 0 || strcmp (tag, "vid_pat") == 0)
    {
      p = mk_pat (cx, PP_VID);
      if (p == NULL)
        return NULL;
      p->path = split_path (cx, n->items[1]->atom, &p->path_len);
      return cx->failed ? NULL : p;
    }
  if (strcmp (tag, "record_pat") == 0)
    {
      size_t i, nr = n->count - 1;
      p = mk_pat (cx, PP_RECORD);
      if (p == NULL)
        return NULL;
      p->labs = (char **)pa_alloc (cx->rt, (nr ? nr : 1u) * sizeof (char *));
      p->items
          = (pa_pat **)pa_alloc (cx->rt, (nr ? nr : 1u) * sizeof (pa_pat *));
      if (p->labs == NULL || p->items == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      p->count = nr;
      for (i = 0; i < nr; i++)
        {
          psx *row = n->items[1 + i];
          if (sx_is (row, "patrow") && row->count >= 3)
            {
              p->labs[i] = pa_strdup (cx->rt, row->items[1]->atom);
              p->items[i] = cx_pat (cx, row->items[2]);
            }
          else if (sx_is (row, "labvar_patrow") && row->count >= 2)
            {
              size_t k;
              p->labs[i] = pa_strdup (cx->rt, row->items[1]->atom);
              p->items[i] = NULL;
              for (k = 2; k < row->count; k++)
                {
                  const char *sub = sx_tag (row->items[k]);
                  if (sub != NULL && !is_ty_tag (sub))
                    p->items[i] = cx_pat (cx, row->items[k]); /* `as` pat */
                }
              if (p->items[i] == NULL)
                {
                  p->items[i] = mk_pat (cx, PP_VID);
                  if (p->items[i] != NULL)
                    p->items[i]->path = split_path (cx, row->items[1]->atom,
                                                    &p->items[i]->path_len);
                }
            }
          else if (sx_is (row, "ellipsis_patrow"))
            {
              p->ellipsis = 1;
              p->labs[i] = NULL;
              p->items[i] = NULL;
            }
          else
            {
              cx_fail (cx, "malformed record pattern row");
              return NULL;
            }
          if (cx->failed)
            return NULL;
        }
      return p;
    }
  if (strcmp (tag, "wildcard_pat") == 0 || strcmp (tag, "_") == 0)
    return mk_pat (cx, PP_WILD);
  if (strcmp (tag, "unit_pat") == 0)
    return mk_pat (cx, PP_UNIT);
  if (strcmp (tag, "tuple_pat") == 0 || strcmp (tag, "vec_pat") == 0)
    {
      p = mk_pat (cx, PP_TUPLE);
      if (p == NULL)
        return NULL;
      CX_ARRAY (cx, n, 1, n->count, p->items, pa_pat, cx_pat);
      p->count = n->count - 1;
      return p;
    }
  if (strcmp (tag, "list_pat") == 0)
    {
      size_t i;
      for (i = 1; i < n->count; i++)
        if (sx_is (n->items[i], "ellipsis_listpat"))
          {
            cx_fail (cx, "unsupported list pattern ellipsis");
            return NULL;
          }
      p = mk_pat (cx, PP_LIST);
      if (p == NULL)
        return NULL;
      CX_ARRAY (cx, n, 1, n->count, p->items, pa_pat, cx_pat);
      p->count = n->count - 1;
      return p;
    }
  if (strcmp (tag, "paren_pat") == 0)
    return cx_pat (cx, n->items[1]);
  if (strcmp (tag, "app") == 0)
    {
      if (n->count == 4 && sx_is (n->items[2], "vid_pat"))
        {
          pa_pat *tuple = mk_pat (cx, PP_TUPLE);
          if (tuple == NULL)
            return NULL;
          p = mk_pat (cx, PP_CTOR);
          if (p == NULL)
            return NULL;
          p->path = split_path (cx, n->items[2]->items[1]->atom, &p->path_len);
          tuple->items
              = (pa_pat **)pa_alloc (cx->rt, 2u * sizeof (*tuple->items));
          if (tuple->items == NULL)
            {
              cx_fail (cx, "out of memory");
              return NULL;
            }
          tuple->count = 2;
          tuple->items[0] = cx_pat (cx, n->items[1]);
          tuple->items[1] = cx_pat (cx, n->items[3]);
          p->arg = tuple;
          return cx->failed ? NULL : p;
        }
      if (n->count != 3
          || (!sx_is (n->items[1], "vid") && !sx_is (n->items[1], "vid_pat")))
        {
          cx_fail (cx, "malformed constructor pattern");
          return NULL;
        }
      p = mk_pat (cx, PP_CTOR);
      if (p == NULL)
        return NULL;
      p->path = split_path (cx, n->items[1]->items[1]->atom, &p->path_len);
      p->arg = cx_pat (cx, n->items[2]);
      return cx->failed ? NULL : p;
    }
  if (strcmp (tag, "infix") == 0)
    {
      pa_pat *tuple;
      if (n->count != 4)
        {
          cx_fail (cx, "malformed infix pattern");
          return NULL;
        }
      p = mk_pat (cx, PP_CTOR);
      if (p == NULL)
        return NULL;
      p->path = split_path (cx, n->items[1]->atom, &p->path_len);
      tuple = mk_pat (cx, PP_TUPLE);
      if (tuple == NULL)
        return NULL;
      tuple->items = (pa_pat **)pa_alloc (cx->rt, 2u * sizeof (*tuple->items));
      if (tuple->items == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      tuple->items[0] = cx_pat (cx, n->items[2]);
      tuple->items[1] = cx_pat (cx, n->items[3]);
      tuple->count = 2;
      p->arg = tuple;
      return cx->failed ? NULL : p;
    }
  if (strcmp (tag, "typed_pat") == 0)
    return cx_pat (cx, n->items[1]);
  if (strcmp (tag, "as_pat") == 0 || strcmp (tag, "conj_pat") == 0)
    {
      const char *name = NULL;
      psx *sub = NULL;
      size_t k;
      if (sx_atom (n->items[1]) != NULL)
        name = n->items[1]->atom; /* as_pat: vid is a raw atom */
      else if (sx_is (n->items[1], "vid"))
        name = n->items[1]->items[1]->atom; /* conj_pat: (vid x) */
      for (k = 2; k < n->count; k++)
        {
          const char *sub_tag = sx_tag (n->items[k]);
          if (sub_tag != NULL && !is_ty_tag (sub_tag))
            sub = n->items[k];
        }
      if (name == NULL || sub == NULL)
        {
          cx_fail (cx, "malformed as-pattern");
          return NULL;
        }
      p = mk_pat (cx, PP_AS);
      if (p == NULL)
        return NULL;
      p->aname = pa_strdup (cx->rt, name);
      p->asub = cx_pat (cx, sub);
      return cx->failed ? NULL : p;
    }
  if (strcmp (tag, "disj_pat") == 0)
    {
      if (n->count < 3)
        {
          cx_fail (cx, "malformed or-pattern");
          return NULL;
        }
      p = mk_pat (cx, PP_OR);
      if (p == NULL)
        return NULL;
      p->l = cx_pat (cx, n->items[1]);
      p->r = cx_pat (cx, n->items[2]);
      return cx->failed ? NULL : p;
    }
  cx_fail (cx, "unsupported pattern form (%s)", tag);
  return NULL;
}

/* ---------- declarations ---------- */

/* Parses one fmrule into name/argpats/body.  Plain form has the name atom
 * first; the parenthesized and bare infix forms put argl before it. */
static int
cx_fmrule (pcx *cx, psx *m, const char **name, pa_pat ***args, size_t *nargs,
           pa_exp **body)
{
  size_t i;
  size_t cap = 0, used = 0;
  pa_pat **pats = NULL;
  const char *fname = NULL;
  if (!sx_is (m, "fmrule") || m->count < 3)
    {
      cx_fail (cx, "malformed fun clause");
      return 0;
    }
  *body = cx_exp (cx, m->items[m->count - 1]);
  if (cx->failed)
    return 0;
  for (i = 1; i + 1 < m->count; i++)
    {
      psx *child = m->items[i];
      const char *t;
      if (!child->is_list)
        {
          if (fname == NULL)
            {
              fname = child->atom;
              continue;
            }
        }
      t = sx_tag (child);
      if (t != NULL && strcmp (t, "null") == 0 && fname == NULL)
        {
          fname = "null";
          continue;
        }
      if (t != NULL && is_ty_tag (t))
        continue;
      if (used == cap)
        {
          size_t grown = cap == 0 ? 4u : cap * 2u;
          pa_pat **np = (pa_pat **)pa_alloc (cx->rt, grown * sizeof (*np));
          if (np == NULL)
            {
              cx_fail (cx, "out of memory");
              return 0;
            }
          if (pats != NULL)
            memcpy (np, pats, used * sizeof (*np));
          pats = np;
          cap = grown;
        }
      pats[used] = cx_pat (cx, child);
      if (cx->failed)
        return 0;
      used++;
    }
  if (fname == NULL || used == 0)
    {
      cx_fail (cx, "fun clause without name or arguments");
      return 0;
    }
  /* Infix declaration form `fun (a ++ b) ...`: the name atom follows the
   * first pattern.  Fold argl/name/argr into one tuple argument. */
  if (m->items[1]->is_list)
    {
      pa_pat *tuple = mk_pat (cx, PP_TUPLE);
      if (tuple == NULL || used < 2)
        {
          cx_fail (cx, "malformed infix fun clause");
          return 0;
        }
      tuple->items = (pa_pat **)pa_alloc (cx->rt, 2u * sizeof (*tuple->items));
      if (tuple->items == NULL)
        {
          cx_fail (cx, "out of memory");
          return 0;
        }
      tuple->items[0] = pats[0];
      tuple->items[1] = pats[1];
      tuple->count = 2;
      pats[0] = tuple;
      memmove (pats + 1, pats + 2, (used - 2) * sizeof (*pats));
      used--;
    }
  *name = fname;
  *args = pats;
  *nargs = used;
  return 1;
}

/* Builds fun f a1 .. an = e  ==>  f = fn g0 => .. => case (g0,..) of ... */
static pa_exp *
cx_fun_match (pcx *cx, psx *fvalbind)
{
  size_t i, j, nrules = 0, nargs = 0;
  size_t count = fvalbind->count;
  pa_rule *rules;
  const char *name = NULL;
  (void)name;
  rules = (pa_rule *)pa_alloc (cx->rt, (count ? count : 1u) * sizeof (*rules));
  if (rules == NULL)
    {
      cx_fail (cx, "out of memory");
      return NULL;
    }
  for (i = 1; i < count; i++)
    {
      psx *m = fvalbind->items[i];
      const char *rule_name;
      pa_pat **args;
      size_t rule_nargs;
      pa_exp *body;
      if (!sx_is (m, "fmrule"))
        continue; /* tyvarseq etc. */
      if (!cx_fmrule (cx, m, &rule_name, &args, &rule_nargs, &body))
        return NULL;
      if (nrules == 0)
        nargs = rule_nargs;
      else if (rule_nargs != nargs)
        {
          cx_fail (cx, "fun clauses disagree on argument count");
          return NULL;
        }
      if (nargs == 1)
        {
          rules[nrules].pat = args[0];
          rules[nrules].body = body;
        }
      else
        {
          pa_pat *tuple = mk_pat (cx, PP_TUPLE);
          if (tuple == NULL)
            return NULL;
          tuple->items = args;
          tuple->count = rule_nargs;
          rules[nrules].pat = tuple;
          rules[nrules].body = body;
        }
      nrules++;
    }
  if (nrules == 0)
    {
      cx_fail (cx, "fun binding without clauses");
      return NULL;
    }
  if (nargs == 1)
    {
      pa_exp *fn = mk_exp (cx, PE_FN);
      if (fn == NULL)
        return NULL;
      fn->rules = rules;
      fn->nrules = nrules;
      return fn;
    }
  /* Curried: fn g0 => fn g1 => .. => case (g0, g1, ..) of (pats) => e .. */
  {
    pa_exp *case_exp = mk_exp (cx, PE_CASE);
    pa_exp *sel = mk_exp (cx, PE_TUPLE);
    pa_exp *result;
    char **gnames = (char **)pa_alloc (cx->rt, nargs * sizeof (*gnames));
    if (case_exp == NULL || sel == NULL || gnames == NULL)
      return NULL;
    for (j = 0; j < nargs; j++)
      {
        gnames[j] = cx_gensym (cx);
        if (gnames[j] == NULL)
          {
            cx_fail (cx, "out of memory");
            return NULL;
          }
      }
    sel->items = (pa_exp **)pa_alloc (cx->rt, nargs * sizeof (*sel->items));
    if (sel->items == NULL)
      {
        cx_fail (cx, "out of memory");
        return NULL;
      }
    sel->count = nargs;
    for (j = 0; j < nargs; j++)
      {
        sel->items[j] = cx_vid_exp (cx, gnames[j]);
        if (cx->failed)
          return NULL;
      }
    case_exp->a = sel;
    case_exp->rules = rules;
    case_exp->nrules = nrules;
    result = case_exp;
    for (j = nargs; j-- > 0;)
      {
        pa_exp *fn = mk_exp (cx, PE_FN);
        pa_pat *vp = mk_pat (cx, PP_VID);
        if (fn == NULL || vp == NULL)
          return NULL;
        fn->rules = (pa_rule *)pa_alloc (cx->rt, sizeof (*fn->rules));
        if (fn->rules == NULL)
          {
            cx_fail (cx, "out of memory");
            return NULL;
          }
        vp->path = split_path (cx, gnames[j], &vp->path_len);
        if (cx->failed)
          return NULL;
        fn->rules[0].pat = vp;
        fn->rules[0].body = result;
        fn->nrules = 1;
        result = fn;
      }
    return result;
  }
}

static pa_dec *
cx_dec (pcx *cx, psx *n)
{
  const char *tag = sx_tag (n);
  if (cx->failed)
    return NULL;
  if (tag == NULL)
    {
      cx_fail (cx, "malformed declaration");
      return NULL;
    }
  if (strcmp (tag, "val_dec") == 0)
    {
      size_t i, nbinds = 0;
      pa_dec **binds = (pa_dec **)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                        * sizeof (*binds));
      int all_fn = 1;
      if (binds == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      for (i = 1; i < n->count; i++)
        {
          psx *vb = n->items[i];
          pa_dec *d;
          if (!sx_is (vb, "valbind") || vb->count < 3)
            continue; /* tyvarseq */
          d = mk_dec (cx, PD_VAL);
          if (d == NULL)
            return NULL;
          d->pat = cx_pat (cx, vb->items[1]);
          d->rhs = cx_exp (cx, vb->items[vb->count - 1]);
          if (cx->failed)
            return NULL;
          if (!(d->pat->kind == PP_VID && d->pat->path_len == 1
                && d->rhs->kind == PE_FN))
            all_fn = 0;
          binds[nbinds++] = d;
        }
      if (nbinds == 0)
        {
          cx_fail (cx, "val declaration without bindings");
          return NULL;
        }
      if (all_fn)
        {
          /* vid = fn .. bindings are recursion-capable (this also covers
           * `val rec`, whose keyword the surface AST drops). */
          pa_dec *d = mk_dec (cx, PD_VALREC);
          if (d == NULL)
            return NULL;
          d->recnames = (char **)pa_alloc (cx->rt, nbinds * sizeof (char *));
          d->recfns = (pa_exp **)pa_alloc (cx->rt, nbinds * sizeof (pa_exp *));
          if (d->recnames == NULL || d->recfns == NULL)
            {
              cx_fail (cx, "out of memory");
              return NULL;
            }
          d->nrec = nbinds;
          for (i = 0; i < nbinds; i++)
            {
              d->recnames[i] = pa_strdup (cx->rt, binds[i]->pat->path[0]);
              d->recfns[i] = binds[i]->rhs;
            }
          return d;
        }
      if (nbinds == 1)
        return binds[0];
      {
        pa_dec *d = mk_dec (cx, PD_SEQ);
        if (d == NULL)
          return NULL;
        d->seq = binds;
        d->nseq = nbinds;
        return d;
      }
    }
  if (strcmp (tag, "fun_dec") == 0)
    {
      size_t i, nfuns = 0;
      pa_dec *d = mk_dec (cx, PD_VALREC);
      if (d == NULL)
        return NULL;
      d->recnames = (char **)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                   * sizeof (char *));
      d->recfns = (pa_exp **)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                   * sizeof (pa_exp *));
      if (d->recnames == NULL || d->recfns == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      for (i = 1; i < n->count; i++)
        {
          psx *fvb = n->items[i];
          size_t k;
          const char *fname = NULL;
          if (!sx_is (fvb, "fvalbind"))
            continue; /* tyvarseq */
          for (k = 1; k < fvb->count; k++)
            if (sx_is (fvb->items[k], "fmrule"))
              {
                psx *m = fvb->items[k];
                size_t q;
                for (q = 1; q + 1 < m->count; q++)
                  if (!m->items[q]->is_list)
                    {
                      fname = m->items[q]->atom;
                      break;
                    }
                break;
              }
          if (fname == NULL)
            {
              cx_fail (cx, "fun binding without clauses");
              return NULL;
            }
          d->recnames[nfuns] = pa_strdup (cx->rt, fname);
          d->recfns[nfuns] = cx_fun_match (cx, fvb);
          if (cx->failed)
            return NULL;
          nfuns++;
        }
      if (nfuns == 0)
        {
          cx_fail (cx, "fun declaration without bindings");
          return NULL;
        }
      d->nrec = nfuns;
      return d;
    }
  if (strcmp (tag, "type_dec") == 0)
    return mk_dec (cx, PD_TYPE); /* type aliases are erased */
  if (strcmp (tag, "datatype_dec") == 0 || strcmp (tag, "abstype_dec") == 0)
    {
      int is_abs = strcmp (tag, "abstype_dec") == 0;
      size_t i, ndts = 0, nwith = 0;
      pa_dec *d = mk_dec (cx, is_abs ? PD_ABSTYPE : PD_DATATYPE);
      if (d == NULL)
        return NULL;
      d->dts = (pa_datdef *)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                  * sizeof (*d->dts));
      d->b_decs = (pa_dec **)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                   * sizeof (pa_dec *));
      if (d->dts == NULL || d->b_decs == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      for (i = 1; i < n->count; i++)
        {
          psx *db = n->items[i];
          if (sx_is (db, "datbind"))
            {
              pa_datdef *def = &d->dts[ndts];
              size_t k, ncons = 0;
              memset (def, 0, sizeof (*def));
              def->cons = (pa_condef *)pa_alloc (
                  cx->rt, (db->count ? db->count : 1u) * sizeof (*def->cons));
              if (def->cons == NULL)
                {
                  cx_fail (cx, "out of memory");
                  return NULL;
                }
              for (k = 1; k < db->count; k++)
                {
                  psx *child = db->items[k];
                  const char *ct = sx_tag (child);
                  if (!child->is_list)
                    {
                      if (def->tycon == NULL)
                        def->tycon = pa_strdup (cx->rt, child->atom);
                      continue;
                    }
                  if (ct != NULL && is_ty_tag (ct))
                    continue;
                  if (sx_is (child, "conbind") && child->count >= 2)
                    {
                      pa_condef *con = &def->cons[ncons++];
                      memset (con, 0, sizeof (*con));
                      con->name = pa_strdup (cx->rt, child->items[1]->atom);
                      con->has_arg = child->count > 2;
                    }
                }
              if (def->tycon == NULL)
                {
                  cx_fail (cx, "datatype binding without a name");
                  return NULL;
                }
              def->ncons = ncons;
              ndts++;
            }
          else if (is_abs && sx_tag (db) != NULL && !is_ty_tag (sx_tag (db)))
            {
              d->b_decs[nwith] = cx_dec (cx, db);
              if (cx->failed)
                return NULL;
              nwith++;
            }
        }
      d->ndts = ndts;
      d->nb = nwith;
      return d;
    }
  if (strcmp (tag, "datarepl_dec") == 0)
    {
      pa_dec *d;
      if (n->count < 3)
        {
          cx_fail (cx, "malformed datatype replication");
          return NULL;
        }
      d = mk_dec (cx, PD_DATAREPL);
      if (d == NULL)
        return NULL;
      d->repl_name = pa_strdup (cx->rt, n->items[1]->atom);
      d->repl_path = split_path (cx, n->items[2]->atom, &d->repl_path_len);
      return cx->failed ? NULL : d;
    }
  if (strcmp (tag, "exception_dec") == 0)
    {
      size_t i, nex = 0;
      pa_dec *d = mk_dec (cx, PD_EXN);
      if (d == NULL)
        return NULL;
      d->exns = (pa_condef *)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                   * sizeof (*d->exns));
      if (d->exns == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      for (i = 1; i < n->count; i++)
        {
          psx *eb = n->items[i];
          pa_condef *con;
          if (!sx_is (eb, "exbind") || eb->count < 2)
            continue;
          con = &d->exns[nex++];
          memset (con, 0, sizeof (*con));
          con->name = pa_strdup (cx->rt, eb->items[1]->atom);
          if (eb->count > 2)
            {
              if (eb->items[2]->is_list)
                con->has_arg = 1; /* exception E of t */
              else
                con->alias_path
                    = split_path (cx, eb->items[2]->atom, &con->alias_len);
            }
          if (cx->failed)
            return NULL;
        }
      d->nexns = nex;
      return d;
    }
  if (strcmp (tag, "local_dec") == 0)
    {
      psx *local_part = n->count > 1 ? n->items[1] : NULL;
      psx *in_part = n->count > 2 ? n->items[2] : NULL;
      pa_dec *d;
      if (!sx_is (local_part, "local") || !sx_is (in_part, "in"))
        {
          cx_fail (cx, "malformed local declaration");
          return NULL;
        }
      d = mk_dec (cx, PD_LOCAL);
      if (d == NULL)
        return NULL;
      CX_ARRAY (cx, local_part, 1, local_part->count, d->a_decs, pa_dec,
                cx_dec);
      d->na = local_part->count - 1;
      CX_ARRAY (cx, in_part, 1, in_part->count, d->b_decs, pa_dec, cx_dec);
      d->nb = in_part->count - 1;
      return d;
    }
  if (strcmp (tag, "open_dec") == 0)
    {
      size_t i;
      pa_dec *d = mk_dec (cx, PD_OPEN);
      if (d == NULL)
        return NULL;
      d->nopen = n->count > 1 ? n->count - 1 : 0;
      d->open_paths = (char ***)pa_alloc (cx->rt, (d->nopen ? d->nopen : 1u)
                                                      * sizeof (char **));
      d->open_lens = (size_t *)pa_alloc (cx->rt, (d->nopen ? d->nopen : 1u)
                                                     * sizeof (size_t));
      if (d->open_paths == NULL || d->open_lens == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      for (i = 0; i < d->nopen; i++)
        d->open_paths[i]
            = split_path (cx, n->items[1 + i]->atom, &d->open_lens[i]);
      return cx->failed ? NULL : d;
    }
  if (strcmp (tag, "infix") == 0 || strcmp (tag, "infixr") == 0
      || strcmp (tag, "nonfix") == 0)
    return mk_dec (cx, PD_FIXITY); /* fixity is resolved by the parser */
  if (strcmp (tag, "do_dec") == 0)
    {
      pa_dec *d = mk_dec (cx, PD_DO);
      if (d == NULL)
        return NULL;
      d->rhs = cx_exp (cx, n->items[1]);
      return cx->failed ? NULL : d;
    }
  cx_fail (cx, "unsupported declaration form (%s)", tag);
  return NULL;
}

/* ---------- modules ---------- */

static pa_strexp *
cx_strexp (pcx *cx, psx *n)
{
  const char *tag = sx_tag (n);
  pa_strexp *s;
  if (cx->failed)
    return NULL;
  if (tag == NULL)
    {
      cx_fail (cx, "malformed structure expression");
      return NULL;
    }
  s = (pa_strexp *)pa_alloc (cx->rt, sizeof (*s));
  if (s == NULL)
    {
      cx_fail (cx, "out of memory");
      return NULL;
    }
  memset (s, 0, sizeof (*s));
  if (strcmp (tag, "struct") == 0)
    {
      size_t i;
      s->kind = PSE_STRUCT;
      s->decs = (pa_strdec **)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                    * sizeof (pa_strdec *));
      if (s->decs == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      s->ndecs = n->count > 1 ? n->count - 1 : 0;
      for (i = 0; i < s->ndecs; i++)
        {
          s->decs[i] = cx_strdec (cx, n->items[1 + i]);
          if (cx->failed)
            return NULL;
        }
      return s;
    }
  if (strcmp (tag, "strid") == 0)
    {
      s->kind = PSE_STRID;
      s->path = split_path (cx, n->items[1]->atom, &s->path_len);
      return cx->failed ? NULL : s;
    }
  if (strcmp (tag, "constrain") == 0)
    {
      s->kind = PSE_CONSTRAIN;
      s->sub = cx_strexp (cx, n->items[1]); /* transparent at runtime */
      return cx->failed ? NULL : s;
    }
  if (strcmp (tag, "fctapp") == 0)
    {
      size_t i;
      s->kind = PSE_FCTAPP;
      s->fct = pa_strdup (cx->rt, n->items[1]->atom);
      s->argdecs = (pa_strdec **)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                       * sizeof (pa_strdec *));
      if (s->argdecs == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      for (i = 2; i < n->count; i++)
        {
          const char *at = sx_tag (n->items[i]);
          if (at != NULL && is_strexp_tag (at))
            {
              if (s->arg != NULL || s->nargdecs != 0)
                {
                  cx_fail (cx, "malformed functor application");
                  return NULL;
                }
              s->arg = cx_strexp (cx, n->items[i]);
            }
          else
            {
              s->argdecs[s->nargdecs] = cx_strdec (cx, n->items[i]);
              s->nargdecs++;
            }
          if (cx->failed)
            return NULL;
        }
      return s;
    }
  if (strcmp (tag, "let-struct") == 0)
    {
      psx *local_part = n->count > 1 ? n->items[1] : NULL;
      psx *in_part = n->count > 2 ? n->items[2] : NULL;
      size_t i;
      if (!sx_is (local_part, "local") || !sx_is (in_part, "in")
          || in_part->count < 2)
        {
          cx_fail (cx, "malformed let structure expression");
          return NULL;
        }
      s->kind = PSE_LET;
      s->decs = (pa_strdec **)pa_alloc (
          cx->rt,
          (local_part->count ? local_part->count : 1u) * sizeof (pa_strdec *));
      if (s->decs == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      s->ndecs = local_part->count - 1;
      for (i = 0; i < s->ndecs; i++)
        {
          s->decs[i] = cx_strdec (cx, local_part->items[1 + i]);
          if (cx->failed)
            return NULL;
        }
      s->sub = cx_strexp (cx, in_part->items[1]);
      return cx->failed ? NULL : s;
    }
  cx_fail (cx, "unsupported structure expression (%s)", tag);
  return NULL;
}

static pa_strdec *
cx_strdec (pcx *cx, psx *n)
{
  const char *tag = sx_tag (n);
  pa_strdec *d;
  if (cx->failed)
    return NULL;
  if (tag == NULL)
    {
      cx_fail (cx, "malformed top-level declaration");
      return NULL;
    }
  d = (pa_strdec *)pa_alloc (cx->rt, sizeof (*d));
  if (d == NULL)
    {
      cx_fail (cx, "out of memory");
      return NULL;
    }
  memset (d, 0, sizeof (*d));
  if (strcmp (tag, "structure") == 0)
    {
      size_t i;
      d->kind = PSD_STRUCTURE;
      d->binds = (pa_strbind *)pa_alloc (cx->rt, (n->count ? n->count : 1u)
                                                     * sizeof (*d->binds));
      if (d->binds == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      d->nbinds = n->count > 1 ? n->count - 1 : 0;
      for (i = 0; i < d->nbinds; i++)
        {
          psx *sb = n->items[1 + i];
          if (!sx_is (sb, "strbind") || sb->count < 3)
            {
              cx_fail (cx, "malformed structure binding");
              return NULL;
            }
          d->binds[i].name = pa_strdup (cx->rt, sb->items[1]->atom);
          d->binds[i].def = cx_strexp (cx, sb->items[sb->count - 1]);
          if (cx->failed)
            return NULL;
        }
      return d;
    }
  if (strcmp (tag, "local_strdec") == 0)
    {
      psx *local_part = n->count > 1 ? n->items[1] : NULL;
      psx *in_part = n->count > 2 ? n->items[2] : NULL;
      size_t i;
      if (!sx_is (local_part, "local") || !sx_is (in_part, "in"))
        {
          cx_fail (cx, "malformed local structure declaration");
          return NULL;
        }
      d->kind = PSD_LOCAL;
      d->a = (pa_strdec **)pa_alloc (
          cx->rt,
          (local_part->count ? local_part->count : 1u) * sizeof (pa_strdec *));
      d->b = (pa_strdec **)pa_alloc (cx->rt,
                                     (in_part->count ? in_part->count : 1u)
                                         * sizeof (pa_strdec *));
      if (d->a == NULL || d->b == NULL)
        {
          cx_fail (cx, "out of memory");
          return NULL;
        }
      d->na = local_part->count - 1;
      d->nb = in_part->count - 1;
      for (i = 0; i < d->na; i++)
        {
          d->a[i] = cx_strdec (cx, local_part->items[1 + i]);
          if (cx->failed)
            return NULL;
        }
      for (i = 0; i < d->nb; i++)
        {
          d->b[i] = cx_strdec (cx, in_part->items[1 + i]);
          if (cx->failed)
            return NULL;
        }
      return d;
    }
  if (strcmp (tag, "signature") == 0)
    {
      /* Signatures are erased after elaboration records them (D-0052); at
       * runtime ascription is transparent, so there is nothing to eval. */
      d->kind = PSD_SIGNATURE;
      return d;
    }
  if (strcmp (tag, "functor") == 0)
    {
      psx *fb = n->count > 1 ? n->items[1] : NULL;
      size_t k;
      if (!sx_is (fb, "fctbind") || fb->count < 3)
        {
          cx_fail (cx, "malformed functor binding");
          return NULL;
        }
      if (n->count > 2)
        {
          cx_fail (cx, "mutually recursive functor bindings are unsupported");
          return NULL;
        }
      d->kind = PSD_FUNCTOR;
      d->fct_name = pa_strdup (cx->rt, fb->items[1]->atom);
      d->fct_param = pa_strdup (cx->rt, "_arg");
      for (k = 2; k + 1 < fb->count; k++)
        if (!fb->items[k]->is_list)
          {
            /* parameter strid atom (spec-list functor forms get _arg) */
            d->fct_param = pa_strdup (cx->rt, fb->items[k]->atom);
            break;
          }
      d->fct_body = cx_strexp (cx, fb->items[fb->count - 1]);
      return cx->failed ? NULL : d;
    }
  if (is_dec_tag (tag) && !(strcmp (tag, "infix") == 0 && !sx_all_atoms (n, 1))
      && !(strcmp (tag, "infixr") == 0 && !sx_all_atoms (n, 1))
      && !(strcmp (tag, "nonfix") == 0 && !sx_all_atoms (n, 1)))
    {
      d->kind = PSD_DEC;
      d->dec = cx_dec (cx, n);
      return cx->failed ? NULL : d;
    }
  /* Anything else at the top level is a bare expression phrase. */
  {
    pa_dec *doe = mk_dec (cx, PD_DO);
    if (doe == NULL)
      return NULL;
    doe->rhs = cx_exp (cx, n);
    if (cx->failed)
      return NULL;
    doe->bind_it = 1;
    d->kind = PSD_DEC;
    d->dec = doe;
    return d;
  }
}

pa_program *
pa_compile_surface (prt *rt, const char *surface, char **error)
{
  pcx cx;
  psx *root;
  pa_program *prog;
  size_t i;
  cx.rt = rt;
  cx.failed = 0;
  cx.err[0] = '\0';
  root = pa_sexp_read (rt, surface, error);
  if (root == NULL)
    return NULL;
  if (!sx_is (root, "program"))
    {
      if (error != NULL)
        *error = pa_strdup (rt, "sml/parthia: surface AST is not a program");
      return NULL;
    }
  prog = (pa_program *)pa_alloc (rt, sizeof (*prog));
  if (prog == NULL)
    return NULL;
  prog->decs = (pa_strdec **)pa_alloc (rt, (root->count ? root->count : 1u)
                                               * sizeof (pa_strdec *));
  if (prog->decs == NULL)
    return NULL;
  prog->count = root->count - 1;
  for (i = 0; i < prog->count; i++)
    {
      prog->decs[i] = cx_strdec (&cx, root->items[1 + i]);
      if (cx.failed)
        {
          if (error != NULL)
            *error = pa_strdup (rt, cx.err);
          return NULL;
        }
    }
  return prog;
}
