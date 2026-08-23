/* negative/ — every fatal path in the spec surfaces as the right
 * exit class (§9): 2 for usage/config, 1 for link errors, 3 for
 * plugin/LTO ABI, 4 internal.  Nothing degrades into a silent skip. */
#include "../../ccwld.h"
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char CODE[] = { 0x55, 0x48, 0x89, 0xe5,
                                      0x31, 0xc0, 0x5d, 0xc3 };

/* returns the exit class a Lua script produced (0 when it linked) */
static int
lua_ret (const char *body)
{
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));
  char *path = ccwld_test_tmp ("neg.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  int ok = ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                          &e);
  free (path);
  if (!ok)
    {
      ccwld_plan_free (p);
      return e.code ? e.code : 2;
    }
  char *out = ccwld_test_tmp ("neg.out");
  int linked = ccwld_link_run (p, out, &e);
  free (out);
  ccwld_plan_free (p);
  return linked ? 0 : e.code;
}

/* returns the exit class an ld script produced */
static int
ld_ret (const char *text)
{
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));
  int ok = ccwld_run_ldscript (text, "neg.ld", "x86_64-ccweave", NULL, &p,
                               &e);
  if (!ok)
    {
      ccwld_plan_free (p);
      return e.code ? e.code : 2;
    }
  char *out = ccwld_test_tmp ("neg.out");
  int linked = ccwld_link_run (p, out, &e);
  free (out);
  ccwld_plan_free (p);
  return linked ? 0 : e.code;
}

static void
test_frontend_usage_errors (void)
{
  /* Lua syntax error */
  CHECK (lua_ret ("ccwld.input(") == 2);
  /* sandbox: io is nil unless --unsafe-lua */
  CHECK (lua_ret ("io.open('/etc/passwd')") == 2);
  /* unknown ccwld entry point */
  CHECK (lua_ret ("ccwld.no_such_function()") == 2);
  /* deferred value read at phase 0 */
  CHECK (lua_ret ("tostring(ccwld.symbol('x'))") == 2);
  /* output.soname outside dso */
  CHECK (lua_ret ("ccwld.output{kind='exe', format='elf',\n"
                  "               soname='libx.so.1'}") == 2);
  /* ccwld.error is fatal */
  CHECK (lua_ret ("ccwld.error('nope')") == 2);
  /* ccwld.assert failure is fatal */
  CHECK (lua_ret ("ccwld.assert(false, 'bad')") == 2);
  /* LTO without a pipeline backend */
  CHECK (lua_ret ("ccwld.lto{enable=true}") == 2);

  /* ld-script parse error */
  CHECK (ld_ret ("SECTIONS { .text { *(.text*) } ") == 2);
  /* unknown region reference */
  CHECK (ld_ret ("SECTIONS { .text : { *(.text*) } > nowhere }") == 2);
  /* memory region with non-constant ORIGIN */
  CHECK (ld_ret ("MEMORY { r : ORIGIN = __a, LENGTH = 0x100 }") == 2);
}

static void
test_include_and_cycle (void)
{
  /* include of a missing file */
  CHECK (lua_ret ("ccwld.include('does-not-exist.lua')") == 2);
  /* include must be .lua */
  CHECK (lua_ret ("ccwld.include('other.ld')") == 2);

  /* include cycle: a -> b -> a */
  char *a = ccwld_test_tmp ("a.lua");
  char *b = ccwld_test_tmp ("b.lua");
  char body[512];
  snprintf (body, sizeof (body), "ccwld.include('%s')\n", b);
  CHECK (ccwld_test_write_file (a, body, strlen (body)));
  snprintf (body, sizeof (body), "ccwld.include('%s')\n", a);
  CHECK (ccwld_test_write_file (b, body, strlen (body)));
  snprintf (body, sizeof (body), "ccwld.include('%s')\n", a);
  CHECK (lua_ret (body) == 2);
  free (a);
  free (b);
}

static void
test_link_errors (void)
{
  char *obj = ccwld_test_tmp ("neg.o");
  ccwld_test_sec secs[] = { { ".text", 1, 0x6, 16, CODE, sizeof (CODE) } };
  ccwld_test_sym syms[] = { { "_start", 1, 1, 0, sizeof (CODE) } };
  CHECK (ccwld_test_write_rel (obj, secs, 1, syms, 1));

  char buf[512];

  /* missing input file */
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s/absent.o')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n",
    ccwld_test_tmp (""));
  CHECK (lua_ret (buf) == 1);

  /* unplaced alloc section (D-0043) */
  static const unsigned char custom[4] = { 9, 9, 9, 9 };
  char *obj2 = ccwld_test_tmp ("neg2.o");
  ccwld_test_sec secs2[] = {
    { ".text", 1, 0x6, 16, CODE, sizeof (CODE) },
    { ".orphan", 1, 0x3, 8, custom, sizeof (custom) },
  };
  CHECK (ccwld_test_write_rel (obj2, secs2, 2, syms, 1));
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    obj2);
  CHECK (lua_ret (buf) == 1);
  free (obj2);

  /* expression cycle */
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.assign('a', ccwld.symbol('b'))\n"
    "ccwld.assign('b', ccwld.symbol('a'))\n",
    obj);
  CHECK (lua_ret (buf) == 1);

  /* unknown symbol in a deferred expression */
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.assign('v', ccwld.symbol('never_defined') + 1)\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    obj);
  CHECK (lua_ret (buf) == 1);

  free (obj);
}

static void
test_seal_discipline (void)
{
  ccwld_error e;
  memset (&e, 0, sizeof (e));
  ccwld_plan *p = ccwld_plan_new ("x86_64-ccweave");
  CHECK (p != NULL);
  ccwld_plan_set_frontend (p, "api");
  ccwld_output o;
  memset (&o, 0, sizeof (o));
  o.kind = (char *)"exe";
  o.format = (char *)"elf";
  CHECK (ccwld_plan_output (p, &o, &e));
  CHECK (ccwld_plan_seal (p, &e));

  /* every builder refuses a sealed plan */
  CHECK (!ccwld_plan_input (p, "x.o", 0, 0, &e));
  CHECK (e.code == CCWLD_EXIT_LINK);
  memset (&e, 0, sizeof (e));
  CHECK (!ccwld_plan_search_path (p, "/x", &e));
  CHECK (!ccwld_plan_plugin (p, "/x.so", "{}", &e));
  CHECK (!ccwld_plan_hook (p, CCWLD_PHASE_EMIT, NULL, NULL, &e));
  /* double seal is refused */
  CHECK (!ccwld_plan_seal (p, &e));
  ccwld_plan_free (p);

  /* seal without an output declaration is a usage error */
  p = ccwld_plan_new ("x86_64-ccweave");
  CHECK (p != NULL);
  memset (&e, 0, sizeof (e));
  CHECK (!ccwld_plan_seal (p, &e));
  CHECK (e.code == CCWLD_EXIT_USAGE);
  ccwld_plan_free (p);
}

static void
test_bad_inputs (void)
{
  /* a non-ELF input must not be passed through (D-0043) */
  char *notelf = ccwld_test_tmp ("notelf.o");
  CHECK (ccwld_test_write_file (notelf, "definitely not an elf", 21));
  char buf[512];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n",
    notelf);
  CHECK (lua_ret (buf) == 1);
  free (notelf);
}

int
main (void)
{
  test_frontend_usage_errors ();
  test_include_and_cycle ();
  test_link_errors ();
  test_seal_discipline ();
  test_bad_inputs ();
  return ccwld_test_failures () ? 1 : 0;
}
