/* expr/ — the deferred expression engine (§2.3, D-0039): value-level
 * unit checks against the evaluator, symbol resolution through the
 * plan, region queries, pre-layout ADDR/SIZEOF refusal, cycle
 * detection through a real link, and the phase-0 value-read fatal. */
#include "../../ccwld.h"
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- unit evaluation over a bare plan --- */

static ccwld_plan *P;

static uint64_t
v (ccwld_expr *e)
{
  uint64_t out = 0xdead;
  char *err = NULL;
  int ok = ccwld_expr_eval (e, P, 0, &out, &err);
  if (!ok)
    {
      fprintf (stderr, "expr eval failed: %s\n", err ? err : "?");
      free (err);
      ccwld_expr_free (e);
      CHECK (0);
      return 0;
    }
  free (err);
  ccwld_expr_free (e);
  return out;
}

#define EADD(a, b) ccwld_expr_binary (CCWLD_OP_ADD, (a), (b))
#define ESUB(a, b) ccwld_expr_binary (CCWLD_OP_SUB, (a), (b))
#define EMUL(a, b) ccwld_expr_binary (CCWLD_OP_MUL, (a), (b))
#define EDIV(a, b) ccwld_expr_binary (CCWLD_OP_DIV, (a), (b))
#define EMOD(a, b) ccwld_expr_binary (CCWLD_OP_MOD, (a), (b))
#define ESHL(a, b) ccwld_expr_binary (CCWLD_OP_SHL, (a), (b))
#define ESHR(a, b) ccwld_expr_binary (CCWLD_OP_SHR, (a), (b))
#define EXOR(a, b) ccwld_expr_binary (CCWLD_OP_XOR, (a), (b))
#define EAND(a, b) ccwld_expr_binary (CCWLD_OP_AND, (a), (b))
#define EOR(a, b) ccwld_expr_binary (CCWLD_OP_OR, (a), (b))
#define ENEG(a) ccwld_expr_unary (CCWLD_OP_NEG, (a))
#define ENOT(a) ccwld_expr_unary (CCWLD_OP_NOT, (a))
#define EABSU(a) ccwld_expr_unary (CCWLD_OP_ABS, (a))
#define I(x) ccwld_expr_int ((uint64_t)(x))

/* symbol resolver over a fixed table */
static const char *RES_KEYS[] = { "have", "ten" };
static const uint64_t RES_VALS[] = { 0x1234, 10 };
static size_t RES_N = 2;

static int
table_resolver (const ccwld_plan *p, const char *name, uint64_t *value)
{
  (void)p;
  for (size_t i = 0; i < RES_N; i++)
    if (!strcmp (RES_KEYS[i], name))
      {
        *value = RES_VALS[i];
        return 1;
      }
  return 0;
}

static void
test_arith (void)
{
  CHECK (v (I (7)) == 7);
  CHECK (v (EADD (I (2), I (40))) == 42);
  CHECK (v (ESUB (I (50), I (8))) == 42);
  CHECK (v (EMUL (I (6), I (7))) == 42);
  CHECK (v (EDIV (I (85), I (2))) == 42);
  CHECK (v (EMOD (I (85), I (43))) == 42);
}

static void
test_bits_unary (void)
{
  CHECK (v (ESHL (I (1), I (6))) == 64);
  CHECK (v (ESHR (I (256), I (4))) == 16);
  CHECK (v (EAND (I (0xf0), I (0x3c))) == 0x30);
  CHECK (v (EOR (I (0xf0), I (0x0f))) == 0xff);
  CHECK (v (EXOR (I (0xff), I (0x0f))) == 0xf0);
  /* ~x == x ^ ~0 */
  CHECK (v (EXOR (I (0xff), I (~(uint64_t)0))) == ~(uint64_t)0xff);
  CHECK (v (EXOR (I (0), I (~(uint64_t)0))) == ~(uint64_t)0);
  CHECK (v (ENEG (I (5))) == (uint64_t)-5);
  CHECK (v (EABSU (ENEG (I (5)))) == 5);
  CHECK (v (ENOT (I (0))) == 1);
  CHECK (v (ENOT (I (3))) == 0);
}

static void
test_builtin_nodes (void)
{
  CHECK (v (ccwld_expr_max (I (3), I (9))) == 9);
  CHECK (v (ccwld_expr_min (I (3), I (9))) == 3);
  CHECK (v (ccwld_expr_cond (ccwld_expr_int (1), I (10), I (20))) == 10);
  CHECK (v (ccwld_expr_cond (ccwld_expr_int (0), I (10), I (20))) == 20);
  CHECK (v (ccwld_expr_align (I (0x1005), 0x1000)) == 0x2000);
  CHECK (v (ccwld_expr_align_to (I (0x1005), I (0x100))) == 0x1100);
}

static void
test_resolver_and_defined (void)
{
  P->resolve_sym = table_resolver;
  CHECK (v (ccwld_expr_symbol ("have")) == 0x1234);
  CHECK (v (EADD (ccwld_expr_symbol ("ten"), I (32))) == 42);
  CHECK (v (ccwld_expr_defined ("have")) == 1);
  CHECK (v (ccwld_expr_defined ("missing")) == 0);
  /* dot resolves to the evaluation cursor */
  CHECK (v (EADD (ccwld_expr_dot (), I (4))) == 4);
  P->resolve_sym = NULL;
}

static void
test_regions (void)
{
  ccwld_error e;
  memset (&e, 0, sizeof (e));
  CHECK (ccwld_plan_memory (P, "ram", "rwx", 0x400000, 0x20000, &e));
  CHECK (v (ccwld_expr_region_origin ("ram")) == 0x400000);
  CHECK (v (ccwld_expr_region_length ("ram")) == 0x20000);
  /* unknown region is an evaluation error, not a silent 0 */
  uint64_t out = 0xdead;
  char *err = NULL;
  ccwld_expr *e2 = ccwld_expr_region_origin ("nowhere");
  CHECK (!ccwld_expr_eval (e2, P, 0, &out, &err));
  free (err);
  ccwld_expr_free (e2);
}

static void
test_prelayout_addr_refused (void)
{
  ccwld_error e;
  memset (&e, 0, sizeof (e));
  CHECK (ccwld_plan_section (P, ".text", NULL, 1, ".text*", NULL, &e));
  uint64_t out = 0xdead;
  char *err = NULL;
  ccwld_expr *a = ccwld_expr_addr (".text");
  CHECK (!ccwld_expr_eval (a, P, 0, &out, &err));
  free (err);
  ccwld_expr_free (a);
  ccwld_expr *s = ccwld_expr_sizeof (".text");
  CHECK (!ccwld_expr_eval (s, P, 0, &out, &err));
  free (err);
  ccwld_expr_free (s);
}

/* --- through a real link --- */

static char *
obj_with_text (const char *leaf)
{
  static const unsigned char code[] = { 0x55, 0x48, 0x89, 0xe5,
                                        0x5d, 0xc3 };
  ccwld_test_sec secs[] = { { ".text", 1, 0x6, 16, code, sizeof (code) } };
  ccwld_test_sym syms[] = { { "_start", 1, 1, 0, sizeof (code) } };
  char *path = ccwld_test_tmp (leaf);
  CHECK (ccwld_test_write_rel (path, secs, 1, syms, 1));
  return path;
}

static int
link_script (const char *body)
{
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));
  char *path = ccwld_test_tmp ("expr.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  int ok = ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                          &e);
  free (path);
  if (!ok)
    {
      ccwld_plan_free (p);
      return e.code ? e.code : 2;
    }
  char *out = ccwld_test_tmp ("expr.out");
  int linked = ccwld_link_run (p, out, &e);
  free (out);
  ccwld_plan_free (p);
  return linked ? 0 : e.code;
}

static void
test_layout_values (char *obj)
{
  char buf[1024];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.assign('__base', 0x1000)\n"
    "local endh = ccwld.assign('__end', ccwld.dot())\n"
    "ccwld.out('.text', {vma=ccwld.symbol('__base') + 4,\n"
    "                    input=ccwld.match('*', '.text*'), endh})\n"
    "ccwld.on('layout', function (link)\n"
    "  if link.sections[1].addr ~= 0x1004 then\n"
    "    ccwld.error('.text at ' .. link.sections[1].addr .. ', want 0x1004')\n"
    "  end\n"
    "  for i = 1, #link.symbols do\n"
    "    local s = link.symbols[i]\n"
    "    if s.name == '__end' and s.value ~= 0x1004 + 6 then\n"
    "      ccwld.error('__end = ' .. s.value .. ', want ' .. 0x1004 + 6)\n"
    "    end\n"
    "  end\n"
    "end)\n",
    obj);
  CHECK (link_script (buf) == 0);
}

static void
test_cycle_fatal (char *obj)
{
  char buf[512];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n"
    "ccwld.assign('a', ccwld.symbol('b'))\n"
    "ccwld.assign('b', ccwld.symbol('a'))\n",
    obj);
  CHECK (link_script (buf) == 1);
}

static void
test_phase0_read_fatal (void)
{
  CHECK (link_script ("local e = ccwld.symbol('x') + 1\n"
                      "tostring(e)\n") == 2);
}

int
main (void)
{
  P = ccwld_plan_new ("x86_64-ccweave");
  CHECK (P != NULL);
  test_arith ();
  test_bits_unary ();
  test_builtin_nodes ();
  test_resolver_and_defined ();
  test_regions ();
  test_prelayout_addr_refused ();
  ccwld_plan_free (P);

  char *obj = obj_with_text ("expr.o");
  test_layout_values (obj);
  test_cycle_fatal (obj);
  free (obj);
  test_phase0_read_fatal ();
  return ccwld_test_failures () ? 1 : 0;
}
