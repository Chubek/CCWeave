/* lto/ — D-0041: the backend behind ccwld-lto.h.  A real .ccw.lto IR
 * module flows through the reference backend, re-enters the pipeline
 * as a native object before gc, and the reproducible flag pins jobs=1.
 * ABI-version rejection and missing entry points are exit-3 fatals. */
#include "../../ccwld.h"
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* one .text byte per entry symbol so sizes stay obvious */
static const unsigned char CODE[] = { 0x55, 0x31, 0xc0, 0x5d, 0xc3 };

/* an ET_REL carrying a CCWIR1 module in a .ccw.lto section */
static char *
lto_module_obj (const char *leaf, const char *ir)
{
  ccwld_test_sec secs[] = {
    { ".ccw.lto", 1, 0x2, 1, (const unsigned char *)ir, strlen (ir) },
  };
  ccwld_test_sym syms[] = {
    { "ir_dummy", 1, 1, 0, 1 },
  };
  char *path = ccwld_test_tmp (leaf);
  CHECK (ccwld_test_write_rel (path, secs, 1, syms, 1));
  return path;
}

static int
link_script_ret (const char *body, char **out_path)
{
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));
  char *path = ccwld_test_tmp ("lto.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  int ok = ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                          &e);
  free (path);
  if (!ok)
    {
      fprintf (stderr, "lto: script failed: %s\n", e.message);
      ccwld_plan_free (p);
      return e.code ? e.code : 2;
    }
  char *out = ccwld_test_tmp ("lto.out");
  int linked = ccwld_link_run (p, out, &e);
  if (out_path && linked)
    *out_path = out;
  else
    free (out);
  ccwld_plan_free (p);
  return linked ? 0 : e.code;
}

static void
test_roundtrip (void)
{
  /* native .text plus one LTO module defining __lto_fn at 0 */
  static const char IR[] = "CCWIR1\ntext 5531c05dc3\n"
                           "sym __lto_fn global 0\n";
  char *mod = lto_module_obj ("mod.o", IR);
  char buf[1024];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='__lto_fn'}\n"
    "ccwld.lto{pipeline='%s', jobs=1}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n"
    "ccwld.on('layout', function (link)\n"
    "  local n = 0\n"
    "  for i = 1, #link.symbols do\n"
    "    local s = link.symbols[i]\n"
    "    if s.name == '__lto_fn' then\n"
    "      n = n + 1\n"
    "      if not s.defined then\n"
    "        ccwld.error('__lto_fn not defined after LTO')\n"
    "      end\n"
    "    end\n"
    "    if s.name == '__lto_jobs_1' and s.value ~= 1 then\n"
    "      ccwld.error('jobs pinning not observed')\n"
    "    end\n"
    "  end\n"
    "  if n ~= 1 then ccwld.error('LTO symbol missing') end\n"
    "end)\n",
    mod, CCWLD_TEST_LTO_REF);
  char *out = NULL;
  CHECK (link_script_ret (buf, &out) == 0);
  CHECK (out != NULL);
  CHECK (ccwld_test_file_size (out) > 64);
  free (out);
  free (mod);
}

static void
test_jobs_pinned_under_reproducible (void)
{
  /* jobs=4 requested; reproducible links must pin to 1, which the
   * reference backend reports via the __lto_jobs_<n> symbol */
  static const char IR[] = "CCWIR1\ntext 90\nsym __p global 0\n";
  char *mod = lto_module_obj ("pin.o", IR);
  char buf[1024];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='__p'}\n"
    "ccwld.lto{pipeline='%s', jobs=4}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n"
    "ccwld.on('layout', function (link)\n"
    "  for i = 1, #link.symbols do\n"
    "    local s = link.symbols[i]\n"
    "    if s.name == '__lto_jobs_1' and s.value ~= 1 then\n"
    "      ccwld.error('expected jobs=1 pinning')\n"
    "    end\n"
    "    if s.name == '__lto_jobs_4' then\n"
    "      ccwld.error('jobs=4 leaked into a reproducible link')\n"
    "    end\n"
    "  end\n"
    "end)\n",
    mod, CCWLD_TEST_LTO_REF);
  CHECK (link_script_ret (buf, NULL) == 0);
  free (mod);
}

static void
test_abi_fatals (void)
{
  static const char IR[] = "CCWIR1\ntext 90\n";
  char *mod = lto_module_obj ("bad.o", IR);
  char buf[512];

  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='__p'}\n"
    "ccwld.lto{pipeline='%s', jobs=1}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    mod, CCWLD_TEST_LTO_BADABI);
  CHECK (link_script_ret (buf, NULL) == 3);

  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='__p'}\n"
    "ccwld.lto{pipeline='%s', jobs=1}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    mod, CCWLD_TEST_LTO_MISSING);
  CHECK (link_script_ret (buf, NULL) == 3);

  free (mod);
}

static void
test_bad_ir_fatal (void)
{
  static const char IR[] = "NOTCCWIR\ntext 90\n";
  char *mod = lto_module_obj ("badir.o", IR);
  char buf[512];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='__p'}\n"
    "ccwld.lto{pipeline='%s', jobs=1}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    mod, CCWLD_TEST_LTO_REF);
  CHECK (link_script_ret (buf, NULL) == 3);
  free (mod);
}

int
main (void)
{
  test_roundtrip ();
  test_jobs_pinned_under_reproducible ();
  test_abi_fatals ();
  test_bad_ir_fatal ();
  return ccwld_test_failures () ? 1 : 0;
}
