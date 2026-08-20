#include "parthia_rt.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static pv *
need_int (prt *rt, pv *v)
{
  if (v == NULL || v->kind != PV_INT)
    pa_fail (rt, "integer value expected");
  return v;
}

static pv *
binary_int (prt *rt, pv **args, int n, int op)
{
  long long a, b, out = 0;
  if (n != 2)
    pa_fail (rt, "integer operator arity");
  a = need_int (rt, args[0])->i;
  b = need_int (rt, args[1])->i;
  switch (op)
    {
    case 0: out = a + b; break;
    case 1: out = a - b; break;
    case 2: out = a * b; break;
    case 3:
      if (b == 0) pa_raise (rt, pa_ctor_make (rt, "Div", NULL, 1));
      out = a / b;
      break;
    case 4:
      if (b == 0) pa_raise (rt, pa_ctor_make (rt, "Div", NULL, 1));
      out = a % b;
      break;
    default: break;
    }
  return pa_int (rt, out);
}

static pv *op_add (prt *r, pv *s, pv **a, int n) { (void)s; return binary_int (r, a, n, 0); }
static pv *op_sub (prt *r, pv *s, pv **a, int n) { (void)s; return binary_int (r, a, n, 1); }
static pv *op_mul (prt *r, pv *s, pv **a, int n) { (void)s; return binary_int (r, a, n, 2); }
static pv *op_div (prt *r, pv *s, pv **a, int n) { (void)s; return binary_int (r, a, n, 3); }
static pv *op_mod (prt *r, pv *s, pv **a, int n) { (void)s; return binary_int (r, a, n, 4); }

static pv *
op_cons (prt *rt, pv *self, pv **args, int n)
{
  pv *pair;
  (void)self;
  if (n != 2)
    pa_fail (rt, "list constructor arity");
  pair = pa_new (rt, PV_TUPLE);
  if (pair == NULL)
    pa_fail (rt, "out of memory");
  pair->t.n = 2;
  pair->t.items = (pv **)pa_alloc (rt, 2u * sizeof (pv *));
  if (pair->t.items == NULL)
    pa_fail (rt, "out of memory");
  pair->t.items[0] = args[0];
  pair->t.items[1] = args[1];
  return pa_ctor_make (rt, "::", pair, 0);
}

static pv *
op_concat (prt *rt, pv *self, pv **args, int n)
{
  size_t left_len, right_len;
  pv *left;
  pv *right;
  char *data;
  (void)self;
  if (n != 2)
    pa_fail (rt, "string concatenation arity");
  left = args[0];
  right = args[1];
  if (left == NULL || right == NULL || left->kind != PV_STRING
      || right->kind != PV_STRING)
    pa_fail (rt, "string value expected");
  left_len = left->s.len;
  right_len = right->s.len;
  if (right_len > SIZE_MAX - left_len)
    pa_fail (rt, "string concatenation overflow");
  data = (char *)pa_alloc (rt, left_len + right_len + 1u);
  if (data == NULL)
    pa_fail (rt, "out of memory");
  memcpy (data, left->s.data, left_len);
  memcpy (data + left_len, right->s.data, right_len);
  data[left_len + right_len] = '\0';
  {
    pv *result = pa_new (rt, PV_STRING);
    if (result == NULL)
      pa_fail (rt, "out of memory");
    result->s.data = data;
    result->s.len = left_len + right_len;
    return result;
  }
}

static pv *
op_int_to_string (prt *rt, pv *self, pv **args, int n)
{
  char *shown;
  pv *result;
  (void)self;
  if (n != 1 || args[0] == NULL || args[0]->kind != PV_INT)
    pa_fail (rt, "integer value expected");
  shown = pa_show (rt, args[0]);
  result = pa_string (rt, shown, strlen (shown));
  return result;
}

static pv *
op_bool_to_string (prt *rt, pv *self, pv **args, int n)
{
  (void)self;
  if (n != 1 || args[0] == NULL || args[0]->kind != PV_CTOR
      || args[0]->c.arg != NULL
      || (strcmp (args[0]->c.name, "true") != 0
          && strcmp (args[0]->c.name, "false") != 0))
    pa_fail (rt, "boolean value expected");
  return pa_string (rt, args[0]->c.name, strlen (args[0]->c.name));
}

static pv *
op_string_size (prt *rt, pv *self, pv **args, int n)
{
  (void)self;
  if (n != 1 || args[0] == NULL || args[0]->kind != PV_STRING)
    pa_fail (rt, "string value expected");
  return pa_int (rt, (long long)args[0]->s.len);
}

static pv *
op_string_concat (prt *rt, pv *self, pv **args, int n)
{
  pv *cursor;
  size_t length = 0;
  char *data;
  size_t offset = 0;
  (void)self;
  if (n != 1)
    pa_fail (rt, "string concatenation arity");
  cursor = args[0];
  while (cursor != NULL && cursor->kind == PV_CTOR
         && strcmp (cursor->c.name, "::") == 0 && cursor->c.arg != NULL
         && cursor->c.arg->kind == PV_TUPLE && cursor->c.arg->t.n == 2)
    {
      pv *item = cursor->c.arg->t.items[0];
      if (item == NULL || item->kind != PV_STRING
          || item->s.len > SIZE_MAX - length)
        pa_fail (rt, "string value expected");
      length += item->s.len;
      cursor = cursor->c.arg->t.items[1];
    }
  if (cursor == NULL || cursor->kind != PV_CTOR
      || strcmp (cursor->c.name, "nil") != 0 || cursor->c.arg != NULL)
    pa_fail (rt, "list value expected");
  data = (char *)pa_alloc (rt, length + 1u);
  if (data == NULL)
    pa_fail (rt, "out of memory");
  cursor = args[0];
  while (cursor != NULL && cursor->kind == PV_CTOR
         && strcmp (cursor->c.name, "::") == 0 && cursor->c.arg != NULL
         && cursor->c.arg->kind == PV_TUPLE && cursor->c.arg->t.n == 2)
    {
      pv *item = cursor->c.arg->t.items[0];
      memcpy (data + offset, item->s.data, item->s.len);
      offset += item->s.len;
      cursor = cursor->c.arg->t.items[1];
    }
  data[length] = '\0';
  {
    pv *result = pa_new (rt, PV_STRING);
    if (result == NULL)
      pa_fail (rt, "out of memory");
    result->s.data = data;
    result->s.len = length;
    return result;
  }
}

static pv *
compare_int (prt *rt, pv **args, int n, int op)
{
  long long a, b;
  if (n != 2)
    pa_fail (rt, "comparison arity");
  a = need_int (rt, args[0])->i;
  b = need_int (rt, args[1])->i;
  return pa_bool (rt, op == 0 ? a < b : op == 1 ? a <= b
                              : op == 2 ? a > b : a >= b);
}
static pv *op_lt (prt *r, pv *s, pv **a, int n) { (void)s; return compare_int (r, a, n, 0); }
static pv *op_le (prt *r, pv *s, pv **a, int n) { (void)s; return compare_int (r, a, n, 1); }
static pv *op_gt (prt *r, pv *s, pv **a, int n) { (void)s; return compare_int (r, a, n, 2); }
static pv *op_ge (prt *r, pv *s, pv **a, int n) { (void)s; return compare_int (r, a, n, 3); }

static pv *
op_equal (prt *rt, pv *self, pv **args, int n)
{
  (void)self;
  if (n != 2) pa_fail (rt, "equality arity");
  return pa_bool (rt, pa_equal (rt, args[0], args[1]));
}
static pv *
op_notequal (prt *rt, pv *self, pv **args, int n)
{
  pv *v = op_equal (rt, self, args, n);
  return pa_bool (rt, !(v->kind == PV_CTOR && v->c.arg == NULL
                        && strcmp (v->c.name, "true") == 0));
}

static pv *
op_ref (prt *rt, pv *self, pv **args, int n)
{
  pv *v;
  (void)self;
  if (n != 1) pa_fail (rt, "ref arity");
  v = pa_new (rt, PV_REF);
  if (!v) pa_fail (rt, "out of memory");
  v->refcell = args[0];
  return v;
}
static pv *
op_deref (prt *rt, pv *self, pv **args, int n)
{
  (void)self;
  if (n != 1 || args[0]->kind != PV_REF) pa_fail (rt, "reference expected");
  return args[0]->refcell;
}
static pv *
op_assign (prt *rt, pv *self, pv **args, int n)
{
  (void)self;
  if (n != 2 || args[0]->kind != PV_REF) pa_fail (rt, "reference expected");
  args[0]->refcell = args[1];
  return pa_unit (rt);
}
static pv *
op_print (prt *rt, pv *self, pv **args, int n)
{
  (void)self;
  if (n != 1 || args[0]->kind != PV_STRING) pa_fail (rt, "string expected");
  fputs (args[0]->s.data, stdout);
  fflush (stdout);
  return pa_unit (rt);
}

static pv *
stream_value (prt *rt, FILE *file, int readable)
{
  pv *v = pa_new (rt, PV_STREAM);
  if (!v) pa_fail (rt, "out of memory");
  v->st.file = file;
  v->st.fd = -1;
  v->st.readable = readable;
  v->st.is_open = 1;
  return v;
}
static pv *
op_open_in (prt *rt, pv *self, pv **args, int n)
{
  FILE *f;
  (void)self;
  if (n != 1 || args[0]->kind != PV_STRING) pa_fail (rt, "string expected");
  f = fopen (args[0]->s.data, "rb");
  if (!f) pa_fail (rt, "cannot open input: %s", strerror (errno));
  return stream_value (rt, f, 1);
}
static pv *
op_open_out (prt *rt, pv *self, pv **args, int n)
{
  FILE *f;
  (void)self;
  if (n != 1 || args[0]->kind != PV_STRING) pa_fail (rt, "string expected");
  f = fopen (args[0]->s.data, "wb");
  if (!f) pa_fail (rt, "cannot open output: %s", strerror (errno));
  return stream_value (rt, f, 0);
}
static pv *
op_close_stream (prt *rt, pv *self, pv **args, int n)
{
  (void)self;
  if (n != 1 || args[0]->kind != PV_STREAM) pa_fail (rt, "stream expected");
  if (args[0]->st.is_open && args[0]->st.file)
    fclose ((FILE *)args[0]->st.file);
  args[0]->st.is_open = 0;
  return pa_unit (rt);
}
static pv *
op_output (prt *rt, pv *self, pv **args, int n)
{
  pv *stream;
  pv *text;
  (void)self;
  if (n != 1 || args[0]->kind != PV_TUPLE || args[0]->t.n != 2)
    pa_fail (rt, "output expects stream and string");
  stream = args[0]->t.items[0];
  text = args[0]->t.items[1];
  if (stream->kind != PV_STREAM || text->kind != PV_STRING)
    pa_fail (rt, "output expects stream and string");
  if (!stream->st.is_open || !stream->st.file)
    pa_fail (rt, "closed output stream");
  fwrite (text->s.data, 1, text->s.len, (FILE *)stream->st.file);
  return pa_unit (rt);
}

static void
install_struct_value (prt *rt, const char *name, const char **members,
                       pv *(*const *fns)(prt *, pv *, pv **, int),
                       const int *arities, size_t count)
{
  pa_env *env = pa_env_new (rt, NULL);
  pv *structure = pa_new (rt, PV_STRUCT);
  size_t i;
  if (!env || !structure) pa_fail (rt, "out of memory");
  structure->str = env;
  for (i = 0; i < count; i++)
    pa_def_native (rt, env, members[i], arities[i], fns[i]);
  pa_bind (rt, rt->global, name, structure);
}

void
pa_basis_install (prt *rt)
{
  static const char *names[] = { "+", "-", "*", "div", "mod", "::", "^",
                                 "<", "<=", ">", ">=", "=", "<>" };
  static pv *(*fns[])(prt *, pv *, pv **, int)
      = { op_add, op_sub, op_mul, op_div, op_mod, op_cons, op_concat, op_lt,
          op_le, op_gt, op_ge, op_equal, op_notequal };
  static const int arities[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
  static const char *int_members[] = { "toString" };
  static pv *(*int_fns[])(prt *, pv *, pv **, int) = { op_int_to_string };
  static const int int_arities[] = { 1 };
  static const char *bool_members[] = { "toString" };
  static pv *(*bool_fns[])(prt *, pv *, pv **, int) = { op_bool_to_string };
  static const int bool_arities[] = { 1 };
  static const char *string_members[] = { "size", "concat" };
  static pv *(*string_fns[])(prt *, pv *, pv **, int)
      = { op_string_size, op_string_concat };
  static const int string_arities[] = { 1, 1 };
  size_t i;
  pa_bind (rt, rt->global, "true", pa_ctor_make (rt, "true", NULL, 0));
  pa_bind (rt, rt->global, "false", pa_ctor_make (rt, "false", NULL, 0));
  pa_bind (rt, rt->global, "nil", pa_ctor_make (rt, "nil", NULL, 0));
  for (i = 0; i < sizeof (names) / sizeof (names[0]); i++)
    pa_def_native (rt, rt->global, names[i], arities[i], fns[i]);
  install_struct_value (rt, "Int", int_members, int_fns, int_arities,
                        sizeof (int_members) / sizeof (int_members[0]));
  install_struct_value (rt, "Bool", bool_members, bool_fns, bool_arities,
                        sizeof (bool_members) / sizeof (bool_members[0]));
  install_struct_value (rt, "String", string_members, string_fns,
                        string_arities,
                        sizeof (string_members) / sizeof (string_members[0]));
  pa_def_native (rt, rt->global, "ref", 1, op_ref);
  pa_def_native (rt, rt->global, "!", 1, op_deref);
  pa_def_native (rt, rt->global, ":=", 2, op_assign);
  pa_def_native (rt, rt->global, "print", 1, op_print);
}

void
pa_kio_install (prt *rt)
{
  static const char *members[] = { "openIn", "openOut", "closeIn",
                                   "closeOut", "output" };
  static pv *(*fns[])(prt *, pv *, pv **, int)
      = { op_open_in, op_open_out, op_close_stream, op_close_stream,
          op_output };
  static const int arities[] = { 1, 1, 1, 1, 1 };
  install_struct_value (rt, "TextIO", members, fns, arities,
                        sizeof (members) / sizeof (members[0]));
}

const char *
pa_prelude_source (void)
{
  return "";
}
