/* Parthia evaluator.
 *
 * A tree-walking evaluator over the core AST produced by parthia_compile.
 * Environments are persistent linked frames; values, frames, and strings
 * live in the runtime arena.  Exceptions are SML values raised with
 * setjmp/longjmp through a handler stack (PE_HANDLE installs a frame;
 * pa_raise unwinds to it).  The JIT tier-up pa_tier_up flattens a hot
 * closure's captured environment into one frame above the global frame,
 * which preserves later top-level bindings while shortening lookups. */

#include "parthia_rt.h"
#include "kbarena.h"
#include "kstring.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- arena ---------- */

void *
pa_alloc (prt *rt, size_t size)
{
  size_t aligned;
  if (rt == NULL || rt->arena == NULL)
    return NULL;
  if (size > SIZE_MAX - 15u)
    return NULL;
  aligned = (size + 15u) & ~(size_t)15u;
  if (aligned == 0)
    aligned = 16u;
  if (aligned > UINT_MAX)
    return NULL;
  return kba_alloc (rt->arena, (unsigned)aligned, 16u);
}

char *
pa_strndup (prt *rt, const char *text, size_t length)
{
  char *copy;
  if (text == NULL)
    return NULL;
  copy = (char *)pa_alloc (rt, length + 1u);
  if (copy == NULL)
    return NULL;
  memcpy (copy, text, length);
  copy[length] = '\0';
  return copy;
}

char *
pa_strdup (prt *rt, const char *text)
{
  return text != NULL ? pa_strndup (rt, text, strlen (text)) : NULL;
}

/* ---------- values ---------- */

pv *
pa_new (prt *rt, int kind)
{
  pv *v = (pv *)pa_alloc (rt, sizeof (*v));
  if (v == NULL)
    return NULL;
  memset (v, 0, sizeof (*v));
  v->kind = kind;
  return v;
}

pv *
pa_int (prt *rt, long long value)
{
  pv *v = pa_new (rt, PV_INT);
  if (v != NULL)
    v->i = value;
  return v;
}

pv *
pa_real (prt *rt, double value)
{
  pv *v = pa_new (rt, PV_REAL);
  if (v != NULL)
    v->r = value;
  return v;
}

pv *
pa_string (prt *rt, const char *data, size_t len)
{
  pv *v = pa_new (rt, PV_STRING);
  if (v != NULL)
    {
      v->s.data = pa_strndup (rt, data != NULL ? data : "", len);
      v->s.len = len;
    }
  return v;
}

pv *
pa_unit (prt *rt)
{
  return pa_new (rt, PV_UNIT);
}

pv *
pa_ctor_make (prt *rt, const char *name, pv *arg, int is_exn)
{
  pv *v = pa_new (rt, PV_CTOR);
  if (v != NULL)
    {
      v->c.name = pa_strdup (rt, name);
      v->c.arg = arg;
      v->c.is_exn = is_exn;
    }
  return v;
}

pv *
pa_bool (prt *rt, int truth)
{
  if (truth && rt->v_true != NULL)
    return rt->v_true;
  if (!truth && rt->v_false != NULL)
    return rt->v_false;
  {
    pv *cached = pa_lookup (rt, rt->global, truth ? "true" : "false");
    if (cached != NULL && cached->kind == PV_CTOR)
      {
        if (truth)
          rt->v_true = cached;
        else
          rt->v_false = cached;
        return cached;
      }
  }
  /* Before the Basis prelude is installed, synthesize the constructor. */
  return pa_ctor_make (rt, truth ? "true" : "false", NULL, 0);
}

/* ---------- environments ---------- */

pa_env *
pa_env_new (prt *rt, pa_env *parent)
{
  pa_env *env = (pa_env *)pa_alloc (rt, sizeof (*env));
  if (env == NULL)
    return NULL;
  env->binds = NULL;
  env->parent = parent;
  return env;
}

void
pa_bind (prt *rt, pa_env *frame, const char *name, pv *value)
{
  pa_binding *b = (pa_binding *)pa_alloc (rt, sizeof (*b));
  if (b == NULL)
    return;
  b->name = pa_strdup (rt, name);
  b->val = value;
  b->next = frame->binds;
  frame->binds = b;
}

pv *
pa_lookup (prt *rt, pa_env *env, const char *name)
{
  pa_env *f;
  pa_binding *b;
  (void)rt;
  for (f = env; f != NULL; f = f->parent)
    for (b = f->binds; b != NULL; b = b->next)
      if (strcmp (b->name, name) == 0)
        return b->val;
  return NULL;
}

pv *
pa_lookup_path (prt *rt, pa_env *env, char **path, size_t n)
{
  pv *v;
  size_t i;
  if (n == 0)
    return NULL;
  v = pa_lookup (rt, env, path[0]);
  for (i = 1; v != NULL && i < n; i++)
    {
      if (v->kind != PV_STRUCT)
        return NULL;
      v = pa_lookup (rt, v->str, path[i]);
    }
  return v;
}

void
pa_def_native (prt *rt, pa_env *env, const char *name, int arity,
               pv *(*fn) (prt *, pv *, pv **, int))
{
  pv *v = pa_new (rt, PV_BUILTIN);
  if (v == NULL)
    return;
  v->b.name = pa_strdup (rt, name);
  v->b.arity = arity;
  v->b.fn = fn;
  v->b.got = NULL;
  v->b.ngot = 0;
  pa_bind (rt, env, name, v);
}

/* ---------- exceptions ---------- */

void
pa_raise (prt *rt, pv *packet)
{
  rt->exn = packet;
  if (rt->handlers == NULL)
    {
      fprintf (stderr, "sml/parthia: exception raised outside evaluation\n");
      abort ();
    }
  longjmp (rt->handlers->jb, 1);
}

void
pa_fail (prt *rt, const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  pv *packet;
  va_start (ap, fmt);
  vsnprintf (buf, sizeof (buf), fmt, ap);
  va_end (ap);
  packet = pa_ctor_make (rt, "Fail", pa_string (rt, buf, strlen (buf)), 1);
  pa_raise (rt, packet);
}

static void
pa_raise_named (prt *rt, const char *name)
{
  pa_raise (rt, pa_ctor_make (rt, name, NULL, 1));
}

/* ---------- equality and selection ---------- */

static int
pa_lit_eq (prt *rt, pv *lit, pv *v)
{
  (void)rt;
  if (lit == NULL || v == NULL || lit->kind != v->kind)
    return 0;
  switch (lit->kind)
    {
    case PV_INT: return lit->i == v->i;
    case PV_WORD: return lit->w == v->w;
    case PV_REAL: return lit->r == v->r;
    case PV_CHAR: return lit->ch == v->ch;
    case PV_STRING:
      return lit->s.len == v->s.len
             && memcmp (lit->s.data, v->s.data, lit->s.len) == 0;
    default: return 0;
    }
}

int
pa_equal (prt *rt, pv *left, pv *right)
{
  size_t i;
  if (left == right)
    return 1;
  if (left == NULL || right == NULL || left->kind != right->kind)
    return 0;
  switch (left->kind)
    {
    case PV_UNIT: return 1;
    case PV_INT: return left->i == right->i;
    case PV_WORD: return left->w == right->w;
    case PV_REAL: return left->r == right->r;
    case PV_CHAR: return left->ch == right->ch;
    case PV_STRING:
      return left->s.len == right->s.len
             && memcmp (left->s.data, right->s.data, left->s.len) == 0;
    case PV_TUPLE:
      if (left->t.n != right->t.n)
        return 0;
      for (i = 0; i < left->t.n; i++)
        if (!pa_equal (rt, left->t.items[i], right->t.items[i]))
          return 0;
      return 1;
    case PV_RECORD:
      if (left->rec.n != right->rec.n)
        return 0;
      for (i = 0; i < left->rec.n; i++)
        if (strcmp (left->rec.labs[i], right->rec.labs[i]) != 0
            || !pa_equal (rt, left->rec.items[i], right->rec.items[i]))
          return 0;
      return 1;
    case PV_CTOR:
      if (strcmp (left->c.name, right->c.name) != 0)
        return 0;
      if (left->c.arg == NULL || right->c.arg == NULL)
        return left->c.arg == right->c.arg;
      return pa_equal (rt, left->c.arg, right->c.arg);
    case PV_REF: return left->refcell == right->refcell;
    default: return 0; /* functions, structures, streams: not equal */
    }
}

static pv *
pv_select (pv *v, const char *lab)
{
  size_t i;
  if (v->kind == PV_RECORD)
    {
      for (i = 0; i < v->rec.n; i++)
        if (strcmp (v->rec.labs[i], lab) == 0)
          return v->rec.items[i];
      return NULL;
    }
  if (v->kind == PV_TUPLE)
    {
      size_t index = 0;
      const char *p;
      for (p = lab; *p; p++)
        {
          if (*p < '0' || *p > '9')
            return NULL;
          index = index * 10u + (size_t)(*p - '0');
        }
      if (index == 0 || index > v->t.n)
        return NULL;
      return v->t.items[index - 1];
    }
  return NULL;
}

/* ---------- showing values ---------- */

static void show_value (prt *rt, pv *v, kstring_t *out, int depth);

static void
show_escaped (kstring_t *out, const char *data, size_t len)
{
  size_t i;
  kputc ('"', out);
  for (i = 0; i < len; i++)
    {
      unsigned char c = (unsigned char)data[i];
      char buf[8];
      switch (c)
        {
        case '"': kputs ("\\\"", out); break;
        case '\\': kputs ("\\\\", out); break;
        case '\n': kputs ("\\n", out); break;
        case '\t': kputs ("\\t", out); break;
        case '\r': kputs ("\\r", out); break;
        default:
          if (c < 32 || c > 126)
            {
              snprintf (buf, sizeof (buf), "\\%03u", c);
              kputs (buf, out);
            }
          else
            kputc ((char)c, out);
        }
    }
  kputc ('"', out);
}

static int
is_list_cons (pv *v)
{
  return v->kind == PV_CTOR && strcmp (v->c.name, "::") == 0
         && v->c.arg != NULL && v->c.arg->kind == PV_TUPLE
         && v->c.arg->t.n == 2;
}

static void
show_ctor (prt *rt, pv *v, kstring_t *out, int depth)
{
  /* List constructors print in bracket notation. */
  if (is_list_cons (v))
    {
      pv *cursor = v;
      int first = 1;
      kputc ('[', out);
      while (is_list_cons (cursor))
        {
          if (!first)
            kputs (", ", out);
          show_value (rt, cursor->c.arg->t.items[0], out, depth + 1);
          cursor = cursor->c.arg->t.items[1];
          first = 0;
        }
      if (!(cursor->kind == PV_CTOR && strcmp (cursor->c.name, "nil") == 0
            && cursor->c.arg == NULL))
        {
          kputs (" | ", out);
          show_value (rt, cursor, out, depth + 1);
        }
      kputc (']', out);
      return;
    }
  kputs (v->c.name, out);
  if (v->c.arg != NULL)
    {
      int paren = v->c.arg->kind == PV_CTOR && v->c.arg->c.arg != NULL;
      kputc (' ', out);
      if (paren)
        kputc ('(', out);
      show_value (rt, v->c.arg, out, depth + 1);
      if (paren)
        kputc (')', out);
    }
}

static void
show_value (prt *rt, pv *v, kstring_t *out, int depth)
{
  char buf[64];
  size_t i;
  if (v == NULL)
    {
      kputs ("<null>", out);
      return;
    }
  if (depth > 8)
    {
      kputs ("...", out);
      return;
    }
  switch (v->kind)
    {
    case PV_UNIT: kputs ("()", out); break;
    case PV_INT:
      snprintf (buf, sizeof (buf), "%lld", v->i);
      kputs (buf, out);
      break;
    case PV_WORD:
      snprintf (buf, sizeof (buf), "0w%llu", v->w);
      kputs (buf, out);
      break;
    case PV_REAL:
      snprintf (buf, sizeof (buf), "%g", v->r);
      kputs (buf, out);
      if (strchr (buf, '.') == NULL && strchr (buf, 'e') == NULL
          && strchr (buf, 'n') == NULL)
        kputs (".0", out);
      break;
    case PV_CHAR:
      {
        char body[2] = { (char)v->ch, '\0' };
        kputs ("#", out);
        show_escaped (out, body, v->ch != 0 ? 1 : 0);
        break;
      }
    case PV_STRING: show_escaped (out, v->s.data, v->s.len); break;
    case PV_TUPLE:
      kputc ('(', out);
      for (i = 0; i < v->t.n; i++)
        {
          if (i != 0)
            kputs (", ", out);
          show_value (rt, v->t.items[i], out, depth + 1);
        }
      kputc (')', out);
      break;
    case PV_RECORD:
      kputc ('{', out);
      for (i = 0; i < v->rec.n; i++)
        {
          if (i != 0)
            kputs (", ", out);
          kputs (v->rec.labs[i], out);
          kputs (" = ", out);
          show_value (rt, v->rec.items[i], out, depth + 1);
        }
      kputc ('}', out);
      break;
    case PV_CTOR: show_ctor (rt, v, out, depth); break;
    case PV_CTORFN:
      kputs ("con ", out);
      kputs (v->cf.name, out);
      break;
    case PV_CLOSURE:
      kputs (v->f.name != NULL ? "<fn " : "<fn>", out);
      if (v->f.name != NULL)
        kputs (v->f.name, out);
      if (v->f.name != NULL)
        kputc ('>', out);
      break;
    case PV_BUILTIN:
      kputs ("<builtin ", out);
      kputs (v->b.name != NULL ? v->b.name : "?", out);
      kputc ('>', out);
      break;
    case PV_REF:
      kputs ("ref ", out);
      show_value (rt, v->refcell, out, depth + 1);
      break;
    case PV_STRUCT: kputs ("<structure>", out); break;
    case PV_FUNCTOR: kputs ("<functor>", out); break;
    case PV_STREAM: kputs ("<stream>", out); break;
    default: kputs ("<value>", out); break;
    }
}

char *
pa_show (prt *rt, pv *value)
{
  kstring_t out = { 0, 0, NULL };
  char *result;
  show_value (rt, value, &out, 0);
  result = pa_strndup (rt, out.s != NULL ? out.s : "",
                       out.s != NULL ? out.l : 0);
  free (out.s);
  return result;
}

/* ---------- pattern matching ---------- */

static int
pa_match (prt *rt, pa_pat *pat, pv *v, pa_env *frame)
{
  size_t i;
  if (pat == NULL || v == NULL)
    return 0;
  switch (pat->kind)
    {
    case PP_WILD: return 1;
    case PP_LIT: return pa_lit_eq (rt, pat->lit, v);
    case PP_UNIT: return v->kind == PV_UNIT;
    case PP_VID:
      {
        pv *bound = pa_lookup_path (rt, frame, pat->path, pat->path_len);
        if (bound != NULL && bound->kind == PV_CTOR)
          /* Nullary constructor pattern (true, false, nil, NONE, ...). */
          return v->kind == PV_CTOR && v->c.arg == NULL
                 && strcmp (v->c.name, bound->c.name) == 0;
        if (bound != NULL && bound->kind == PV_CTORFN)
          return 0; /* constructor without its argument: never matches */
        if (bound == NULL && pat->path_len == 1
            && (strcmp (pat->path[0], "true") == 0
                || strcmp (pat->path[0], "false") == 0
                || strcmp (pat->path[0], "nil") == 0))
          return v->kind == PV_CTOR && v->c.arg == NULL
                 && strcmp (v->c.name, pat->path[0]) == 0;
        if (pat->path_len != 1)
          return 0; /* longvid that is not a constructor */
        pa_bind (rt, frame, pat->path[0], v);
        return 1;
      }
    case PP_TUPLE:
      if (v->kind != PV_TUPLE || v->t.n != pat->count)
        return 0;
      for (i = 0; i < pat->count; i++)
        if (!pa_match (rt, pat->items[i], v->t.items[i], frame))
          return 0;
      return 1;
    case PP_LIST:
      {
        pv *cursor = v;
        for (i = 0; i < pat->count; i++)
          {
            if (!is_list_cons (cursor))
              return 0;
            if (!pa_match (rt, pat->items[i], cursor->c.arg->t.items[0],
                           frame))
              return 0;
            cursor = cursor->c.arg->t.items[1];
          }
        return cursor->kind == PV_CTOR
               && strcmp (cursor->c.name, "nil") == 0
               && cursor->c.arg == NULL;
      }
    case PP_RECORD:
      for (i = 0; i < pat->count; i++)
        {
          pv *field;
          if (pat->labs[i] == NULL)
            continue; /* ellipsis row */
          field = pv_select (v, pat->labs[i]);
          if (field == NULL)
            return 0;
          if (!pa_match (rt, pat->items[i], field, frame))
            return 0;
        }
      return pat->ellipsis || v->kind == PV_TUPLE
             || (v->kind == PV_RECORD && v->rec.n == pat->count);
    case PP_CTOR:
      {
        const char *name = pat->path[pat->path_len - 1];
        if (pat->path_len == 1 && strcmp (name, "ref") == 0)
          return v->kind == PV_REF
                 && pa_match (rt, pat->arg, v->refcell, frame);
        return v->kind == PV_CTOR && strcmp (v->c.name, name) == 0
               && v->c.arg != NULL
               && pa_match (rt, pat->arg, v->c.arg, frame);
      }
    case PP_AS:
      if (!pa_match (rt, pat->asub, v, frame))
        return 0;
      pa_bind (rt, frame, pat->aname, v);
      return 1;
    case PP_OR:
      {
        pa_env *scratch = pa_env_new (rt, frame->parent);
        pa_binding *b;
        if (pa_match (rt, pat->l, v, scratch))
          goto commit;
        scratch = pa_env_new (rt, frame->parent);
        if (!pa_match (rt, pat->r, v, scratch))
          return 0;
      commit:
        for (b = scratch->binds; b != NULL; b = b->next)
          pa_bind (rt, frame, b->name, b->val);
        return 1;
      }
    default: return 0;
    }
}

/* ---------- JIT tier-up ---------- */

/* Flattens the closure's captured frames (everything between its innermost
 * frame and the global root) into one frame whose parent is the root.  The
 * root stays linked so bindings added by later phrases remain visible. */
static void
pa_tier_up (prt *rt, pv *closure)
{
  pa_env *env = closure->f.env;
  pa_env *root = env;
  pa_env *f;
  pa_binding *b;
  pa_env *flat;
  while (root != NULL && root->parent != NULL)
    root = root->parent;
  if (root == NULL || root == env)
    return; /* nothing to flatten */
  flat = pa_env_new (rt, root);
  if (flat == NULL)
    return;
  for (f = env; f != NULL && f != root; f = f->parent)
    for (b = f->binds; b != NULL; b = b->next)
      if (pa_lookup (rt, flat, b->name) == NULL)
        pa_bind (rt, flat, b->name, b->val);
  closure->f.env = flat;
  closure->f.hot = 1;
  rt->jit_spec++;
}

/* ---------- application ---------- */

pv *
pa_apply (prt *rt, pv *fn, pv *arg)
{
  size_t i;
  if (fn == NULL)
    pa_fail (rt, "attempted to apply a non-function");
  if (fn->kind == PV_CLOSURE)
    {
      fn->f.calls++;
      if (!fn->f.hot && fn->f.calls >= PA_JIT_HOT_THRESHOLD)
        pa_tier_up (rt, fn);
      for (i = 0; i < fn->f.nrules; i++)
        {
          pa_env *scratch = pa_env_new (rt, fn->f.env);
          if (pa_match (rt, fn->f.rules[i].pat, arg, scratch))
            return pa_eval_exp (rt, scratch, fn->f.rules[i].body);
        }
      pa_raise_named (rt, "Match");
    }
  if (fn->kind == PV_CTORFN)
    return pa_ctor_make (rt, fn->cf.name, arg, fn->cf.is_exn);
  if (fn->kind == PV_BUILTIN)
    {
      int total = fn->b.ngot + 1;
      if (total < fn->b.arity)
        {
          pv *partial = pa_new (rt, PV_BUILTIN);
          int k;
          if (partial == NULL)
            pa_fail (rt, "out of memory");
          partial->b.name = fn->b.name;
          partial->b.arity = fn->b.arity;
          partial->b.fn = fn->b.fn;
          partial->b.ngot = total;
          partial->b.got
              = (pv **)pa_alloc (rt, (size_t)total * sizeof (pv *));
          if (partial->b.got == NULL)
            pa_fail (rt, "out of memory");
          for (k = 0; k < fn->b.ngot; k++)
            partial->b.got[k] = fn->b.got[k];
          partial->b.got[fn->b.ngot] = arg;
          return partial;
        }
      {
        pv **args = (pv **)pa_alloc (rt, (size_t)total * sizeof (pv *));
        int k;
        if (args == NULL)
          pa_fail (rt, "out of memory");
        for (k = 0; k < fn->b.ngot; k++)
          args[k] = fn->b.got[k];
        args[fn->b.ngot] = arg;
        return fn->b.fn (rt, fn, args, total);
      }
    }
  pa_fail (rt, "attempted to apply a non-function");
  return NULL; /* unreachable */
}

/* ---------- evaluation ---------- */

static int
pa_truth (prt *rt, pv *v)
{
  if (v != NULL && v->kind == PV_CTOR && v->c.arg == NULL)
    {
      if (strcmp (v->c.name, "true") == 0)
        return 1;
      if (strcmp (v->c.name, "false") == 0)
        return 0;
    }
  pa_fail (rt, "boolean value expected");
  return 0; /* unreachable */
}

static pv *eval_exp (prt *rt, pa_env *env, pa_exp *e);
static pv *select_impl (prt *rt, pv *self, pv **args, int n);

static void eval_dec (prt *rt, pa_dec *d, pa_env *env, pa_env *frame);
static void eval_strdec (prt *rt, pa_strdec *sd, pa_env *env, pa_env *frame);

static pv *
eval_exp (prt *rt, pa_env *env, pa_exp *e)
{
  size_t i;
  if (e == NULL)
    pa_fail (rt, "malformed expression");
  switch (e->kind)
    {
    case PE_LIT: return e->lit;
    case PE_VID:
      {
        pv *v = pa_lookup_path (rt, env, e->path, e->path_len);
        if (v == NULL)
          {
            char *name = pa_strdup (rt, e->path[0]);
            for (i = 1; i < e->path_len; i++)
              {
                char *joined = (char *)pa_alloc (
                    rt, strlen (name) + strlen (e->path[i]) + 2u);
                if (joined != NULL)
                  {
                    strcpy (joined, name);
                    strcat (joined, ".");
                    strcat (joined, e->path[i]);
                    name = joined;
                  }
              }
            pa_fail (rt, "unbound variable %s", name);
          }
        return v;
      }
    case PE_RECORD:
      {
        pv *v = pa_new (rt, PV_RECORD);
        if (v == NULL)
          pa_fail (rt, "out of memory");
        v->rec.n = e->count;
        v->rec.labs = e->labs;
        v->rec.items
            = (pv **)pa_alloc (rt, (e->count ? e->count : 1u) * sizeof (pv *));
        if (v->rec.items == NULL)
          pa_fail (rt, "out of memory");
        for (i = 0; i < e->count; i++)
          v->rec.items[i] = eval_exp (rt, env, e->items[i]);
        return v;
      }
    case PE_SEL:
      {
        /* Record selector #lab: a builtin whose own name is the label. */
        pv *v = pa_new (rt, PV_BUILTIN);
        if (v == NULL)
          pa_fail (rt, "out of memory");
        v->b.name = e->sel;
        v->b.arity = 1;
        v->b.fn = select_impl;
        v->b.got = NULL;
        v->b.ngot = 0;
        return v;
      }
    case PE_UNIT: return pa_unit (rt);
    case PE_TUPLE:
      {
        pv *v = pa_new (rt, PV_TUPLE);
        if (v == NULL)
          pa_fail (rt, "out of memory");
        v->t.n = e->count;
        v->t.items
            = (pv **)pa_alloc (rt, (e->count ? e->count : 1u) * sizeof (pv *));
        if (v->t.items == NULL)
          pa_fail (rt, "out of memory");
        for (i = 0; i < e->count; i++)
          v->t.items[i] = eval_exp (rt, env, e->items[i]);
        return v;
      }
    case PE_LIST:
      {
        pv *tail = pa_ctor_make (rt, "nil", NULL, 0);
        i = e->count;
        while (i-- > 0)
          {
            pv *pair = pa_new (rt, PV_TUPLE);
            if (pair == NULL)
              pa_fail (rt, "out of memory");
            pair->t.n = 2;
            pair->t.items = (pv **)pa_alloc (rt, 2u * sizeof (pv *));
            if (pair->t.items == NULL)
              pa_fail (rt, "out of memory");
            pair->t.items[0] = eval_exp (rt, env, e->items[i]);
            pair->t.items[1] = tail;
            tail = pa_ctor_make (rt, "::", pair, 0);
          }
        return tail;
      }
    case PE_SEQ:
      {
        pv *v = pa_unit (rt);
        for (i = 0; i < e->count; i++)
          v = eval_exp (rt, env, e->items[i]);
        return v;
      }
    case PE_LET:
      {
        pa_env *frame = pa_env_new (rt, env);
        for (i = 0; i < e->ndecs; i++)
          eval_dec (rt, e->decs[i], frame, frame);
        return eval_exp (rt, frame, e->a);
      }
    case PE_APP:
      {
        pv *v = eval_exp (rt, env, e->items[0]);
        for (i = 1; i < e->count; i++)
          v = pa_apply (rt, v, eval_exp (rt, env, e->items[i]));
        return v;
      }
    case PE_CONJ:
      if (!pa_truth (rt, eval_exp (rt, env, e->a)))
        return pa_bool (rt, 0);
      return pa_bool (rt, pa_truth (rt, eval_exp (rt, env, e->b)));
    case PE_DISJ:
      if (pa_truth (rt, eval_exp (rt, env, e->a)))
        return pa_bool (rt, 1);
      return pa_bool (rt, pa_truth (rt, eval_exp (rt, env, e->b)));
    case PE_HANDLE:
      {
        pa_handler h;
        pv *result;
        h.prev = rt->handlers;
        rt->handlers = &h;
        if (setjmp (h.jb) != 0)
          {
            pv *packet = rt->exn;
            rt->handlers = h.prev;
            for (i = 0; i < e->nrules; i++)
              {
                pa_env *scratch = pa_env_new (rt, env);
                if (pa_match (rt, e->rules[i].pat, packet, scratch))
                  return eval_exp (rt, scratch, e->rules[i].body);
              }
            pa_raise (rt, packet); /* propagate to the next handler out */
          }
        result = eval_exp (rt, env, e->a);
        rt->handlers = h.prev;
        return result;
      }
    case PE_RAISE:
      {
        pv *packet = eval_exp (rt, env, e->a);
        if (packet->kind != PV_CTOR)
          packet
              = pa_ctor_make (rt, "Fail",
                              pa_string (rt, "raised a non-exception", 22),
                              1);
        pa_raise (rt, packet);
      }
      break;
    case PE_IF:
      if (pa_truth (rt, eval_exp (rt, env, e->a)))
        return eval_exp (rt, env, e->b);
      return e->c != NULL ? eval_exp (rt, env, e->c) : pa_unit (rt);
    case PE_WHILE:
      while (pa_truth (rt, eval_exp (rt, env, e->a)))
        (void)eval_exp (rt, env, e->b);
      return pa_unit (rt);
    case PE_CASE:
      {
        pv *sel = eval_exp (rt, env, e->a);
        for (i = 0; i < e->nrules; i++)
          {
            pa_env *scratch = pa_env_new (rt, env);
            if (pa_match (rt, e->rules[i].pat, sel, scratch))
              return eval_exp (rt, scratch, e->rules[i].body);
          }
        pa_raise_named (rt, "Match");
      }
      break;
    case PE_FN:
      {
        pv *v = pa_new (rt, PV_CLOSURE);
        if (v == NULL)
          pa_fail (rt, "out of memory");
        v->f.rules = e->rules;
        v->f.nrules = e->nrules;
        v->f.env = env;
        v->f.name = NULL;
        v->f.calls = 0;
        v->f.hot = 0;
        return v;
      }
    default: pa_fail (rt, "malformed expression"); break;
    }
  return NULL; /* unreachable */
}

pv *
pa_eval_exp (prt *rt, pa_env *env, pa_exp *e)
{
  pv *v;
  if (++rt->depth > PA_MAX_EVAL_DEPTH)
    {
      --rt->depth;
      pa_fail (rt, "evaluation stack exhausted");
    }
  v = eval_exp (rt, env, e);
  --rt->depth;
  return v;
}

/* Record selector implementation: the label is the builtin's own name. */
static pv *
select_impl (prt *rt, pv *self, pv **args, int n)
{
  pv *field;
  if (n != 1)
    pa_fail (rt, "record selector arity");
  field = pv_select (args[0], self->b.name);
  if (field == NULL)
    pa_raise_named (rt, "Subscript");
  return field;
}

/* Copies the bindings of `from` into `into` (shallow: values shared). */
static void
copy_binds (prt *rt, pa_env *into, pa_env *from)
{
  pa_binding *b;
  size_t n = 0, i;
  pa_binding **order;
  for (b = from->binds; b != NULL; b = b->next)
    n++;
  order = (pa_binding **)pa_alloc (rt, (n ? n : 1u) * sizeof (*order));
  if (order == NULL)
    pa_fail (rt, "out of memory");
  i = n;
  for (b = from->binds; b != NULL; b = b->next)
    order[--i] = b;
  for (i = 0; i < n; i++)
    pa_bind (rt, into, order[i]->name, order[i]->val);
}

static void
eval_datatype (prt *rt, pa_datdef *dts, size_t ndts, pa_env *frame)
{
  size_t i, k;
  for (i = 0; i < ndts; i++)
    {
      pa_datdef *def = &dts[i];
      pa_tyc *tyc = (pa_tyc *)pa_alloc (rt, sizeof (*tyc));
      pv **ctors
          = (pv **)pa_alloc (rt, (def->ncons ? def->ncons : 1u)
                                     * sizeof (pv *));
      if (tyc == NULL || ctors == NULL)
        pa_fail (rt, "out of memory");
      for (k = 0; k < def->ncons; k++)
        {
          pv *ctor;
          if (def->cons[k].has_arg)
            {
              ctor = pa_new (rt, PV_CTORFN);
              if (ctor != NULL)
                {
                  ctor->cf.name = pa_strdup (rt, def->cons[k].name);
                  ctor->cf.is_exn = 0;
                }
            }
          else
            ctor = pa_ctor_make (rt, def->cons[k].name, NULL, 0);
          if (ctor == NULL)
            pa_fail (rt, "out of memory");
          ctors[k] = ctor;
          pa_bind (rt, frame, def->cons[k].name, ctor);
        }
      tyc->name = pa_strdup (rt, def->tycon);
      tyc->ctors = ctors;
      tyc->n = def->ncons;
      tyc->next = rt->tycs;
      rt->tycs = tyc;
    }
}

static void
eval_dec (prt *rt, pa_dec *d, pa_env *env, pa_env *frame)
{
  size_t i;
  if (d == NULL)
    pa_fail (rt, "malformed declaration");
  switch (d->kind)
    {
    case PD_SEQ:
      for (i = 0; i < d->nseq; i++)
        eval_dec (rt, d->seq[i], env, frame);
      return;
    case PD_VAL:
      {
        pv *v = pa_eval_exp (rt, env, d->rhs);
        if (!pa_match (rt, d->pat, v, frame))
          pa_raise_named (rt, "Bind");
        return;
      }
    case PD_VALREC:
      /* Bindings land directly in the target frame; closures capture the
       * environment chain that contains it, so (mutual) recursion works. */
      for (i = 0; i < d->nrec; i++)
        {
          pv *fn = pa_new (rt, PV_CLOSURE);
          if (fn == NULL)
            pa_fail (rt, "out of memory");
          fn->f.rules = d->recfns[i]->rules;
          fn->f.nrules = d->recfns[i]->nrules;
          fn->f.env = env;
          fn->f.name = pa_strdup (rt, d->recnames[i]);
          fn->f.calls = 0;
          fn->f.hot = 0;
          pa_bind (rt, frame, d->recnames[i], fn);
        }
      return;
    case PD_TYPE: return;
    case PD_DATATYPE:
      eval_datatype (rt, d->dts, d->ndts, frame);
      return;
    case PD_ABSTYPE:
      /* Abstype hides nothing at runtime: the representation and the
       * with-body are both elaborated into the frame (transparent). */
      eval_datatype (rt, d->dts, d->ndts, frame);
      for (i = 0; i < d->nb; i++)
        eval_dec (rt, d->b_decs[i], env, frame);
      return;
    case PD_DATAREPL:
      {
        pa_tyc *tyc;
        const char *source = d->repl_path[d->repl_path_len - 1];
        for (tyc = rt->tycs; tyc != NULL; tyc = tyc->next)
          if (strcmp (tyc->name, source) == 0)
            break;
        if (tyc == NULL)
          pa_fail (rt, "datatype replication of unknown type %s", source);
        for (i = 0; i < tyc->n; i++)
          {
            pv *ctor = tyc->ctors[i];
            pa_bind (rt, frame,
                     ctor->kind == PV_CTOR ? ctor->c.name : ctor->cf.name,
                     ctor);
          }
        {
          pa_tyc *copy = (pa_tyc *)pa_alloc (rt, sizeof (*copy));
          if (copy == NULL)
            pa_fail (rt, "out of memory");
          copy->name = pa_strdup (rt, d->repl_name);
          copy->ctors = tyc->ctors;
          copy->n = tyc->n;
          copy->next = rt->tycs;
          rt->tycs = copy;
        }
        return;
      }
    case PD_EXN:
      for (i = 0; i < d->nexns; i++)
        {
          pa_condef *con = &d->exns[i];
          pv *ctor;
          if (con->alias_path != NULL)
            {
              ctor = pa_lookup_path (rt, env, con->alias_path,
                                     con->alias_len);
              if (ctor == NULL
                  || (ctor->kind != PV_CTOR && ctor->kind != PV_CTORFN))
                pa_fail (rt, "exception replication of unknown constructor");
            }
          else if (con->has_arg)
            {
              ctor = pa_new (rt, PV_CTORFN);
              if (ctor != NULL)
                {
                  ctor->cf.name = pa_strdup (rt, con->name);
                  ctor->cf.is_exn = 1;
                }
            }
          else
            ctor = pa_ctor_make (rt, con->name, NULL, 1);
          if (ctor == NULL)
            pa_fail (rt, "out of memory");
          pa_bind (rt, frame, con->name, ctor);
        }
      return;
    case PD_LOCAL:
      {
        pa_env *local_frame = pa_env_new (rt, env);
        pa_env *body_frame;
        for (i = 0; i < d->na; i++)
          eval_dec (rt, d->a_decs[i], local_frame, local_frame);
        body_frame = pa_env_new (rt, local_frame);
        for (i = 0; i < d->nb; i++)
          eval_dec (rt, d->b_decs[i], body_frame, body_frame);
        copy_binds (rt, frame, body_frame);
        return;
      }
    case PD_OPEN:
      for (i = 0; i < d->nopen; i++)
        {
          pv *v = pa_lookup_path (rt, env, d->open_paths[i],
                                  d->open_lens[i]);
          if (v == NULL || v->kind != PV_STRUCT)
            pa_fail (rt, "open: not a structure");
          copy_binds (rt, frame, v->str);
        }
      return;
    case PD_FIXITY: return;
    case PD_DO:
      {
        pv *v = pa_eval_exp (rt, env, d->rhs);
        if (d->bind_it)
          pa_bind (rt, frame, "it", v);
        return;
      }
    default: pa_fail (rt, "malformed declaration");
    }
}

static pv *
eval_strexp (prt *rt, pa_strexp *sx, pa_env *env)
{
  size_t i;
  if (sx == NULL)
    pa_fail (rt, "malformed structure expression");
  switch (sx->kind)
    {
    case PSE_STRUCT:
      {
        pa_env *frame = pa_env_new (rt, env);
        pa_env *detached;
        pv *v;
        for (i = 0; i < sx->ndecs; i++)
          eval_strdec (rt, sx->decs[i], frame, frame);
        /* The structure value is self-contained: detach its frame from the
         * enclosing scope so longvid lookups see only its own bindings. */
        detached = pa_env_new (rt, NULL);
        if (detached == NULL)
          pa_fail (rt, "out of memory");
        detached->binds = frame->binds;
        v = pa_new (rt, PV_STRUCT);
        if (v == NULL)
          pa_fail (rt, "out of memory");
        v->str = detached;
        return v;
      }
    case PSE_STRID:
      {
        pv *v = pa_lookup_path (rt, env, sx->path, sx->path_len);
        if (v == NULL || v->kind != PV_STRUCT)
          pa_fail (rt, "unbound structure");
        return v;
      }
    case PSE_CONSTRAIN:
      /* Signature ascription is transparent at runtime: signatures were
       * erased at elaboration (D-0052), so the body evaluates as-is. */
      return eval_strexp (rt, sx->sub, env);
    case PSE_FCTAPP:
      {
        pv *functor = pa_lookup (rt, env, sx->fct);
        pv *arg;
        pa_env *frame;
        if (functor == NULL || functor->kind != PV_FUNCTOR)
          pa_fail (rt, "application of a non-functor %s", sx->fct);
        if (sx->arg != NULL)
          arg = eval_strexp (rt, sx->arg, env);
        else
          {
            pa_strexp inline_struct;
            memset (&inline_struct, 0, sizeof (inline_struct));
            inline_struct.kind = PSE_STRUCT;
            inline_struct.decs = sx->argdecs;
            inline_struct.ndecs = sx->nargdecs;
            arg = eval_strexp (rt, &inline_struct, env);
          }
        frame = pa_env_new (rt, functor->fc.env);
        pa_bind (rt, frame, functor->fc.param, arg);
        return eval_strexp (rt, functor->fc.body, frame);
      }
    case PSE_LET:
      {
        pa_env *frame = pa_env_new (rt, env);
        for (i = 0; i < sx->ndecs; i++)
          eval_strdec (rt, sx->decs[i], frame, frame);
        return eval_strexp (rt, sx->sub, frame);
      }
    default: pa_fail (rt, "malformed structure expression");
    }
  return NULL; /* unreachable */
}

static void
eval_strdec (prt *rt, pa_strdec *sd, pa_env *env, pa_env *frame)
{
  size_t i;
  if (sd == NULL)
    pa_fail (rt, "malformed module declaration");
  switch (sd->kind)
    {
    case PSD_DEC:
      eval_dec (rt, sd->dec, env, frame);
      return;
    case PSD_STRUCTURE:
      for (i = 0; i < sd->nbinds; i++)
        {
          pv *v = eval_strexp (rt, sd->binds[i].def, env);
          pa_bind (rt, frame, sd->binds[i].name, v);
        }
      return;
    case PSD_LOCAL:
      {
        pa_env *local_frame = pa_env_new (rt, env);
        pa_env *body_frame;
        for (i = 0; i < sd->na; i++)
          eval_strdec (rt, sd->a[i], local_frame, local_frame);
        body_frame = pa_env_new (rt, local_frame);
        for (i = 0; i < sd->nb; i++)
          eval_strdec (rt, sd->b[i], body_frame, body_frame);
        copy_binds (rt, frame, body_frame);
        return;
      }
    case PSD_SIGNATURE: return;
    case PSD_FUNCTOR:
      {
        pv *v = pa_new (rt, PV_FUNCTOR);
        if (v == NULL)
          pa_fail (rt, "out of memory");
        v->fc.param = sd->fct_param;
        v->fc.body = sd->fct_body;
        v->fc.env = env;
        pa_bind (rt, frame, sd->fct_name, v);
        return;
      }
    default: pa_fail (rt, "malformed module declaration");
    }
}

int
pa_eval_program (prt *rt, pa_program *prog, char **error)
{
  pa_handler h;
  size_t i;
  if (prog == NULL)
    return 1;
  rt->depth = 0;
  h.prev = rt->handlers;
  rt->handlers = &h;
  if (setjmp (h.jb) != 0)
    {
      pv *packet = rt->exn;
      char *shown;
      const char *prefix = "uncaught exception ";
      rt->handlers = h.prev;
      shown = pa_show (rt, packet);
      if (error != NULL)
        {
          size_t len = strlen (prefix) + strlen (shown) + 1u;
          char *message = (char *)malloc (len);
          if (message != NULL)
            {
              strcpy (message, prefix);
              strcat (message, shown);
            }
          *error = message;
        }
      return 0;
    }
  for (i = 0; i < prog->count; i++)
    eval_strdec (rt, prog->decs[i], rt->global, rt->global);
  rt->handlers = h.prev;
  return 1;
}
