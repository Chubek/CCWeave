/* phases/ — the fixed pipeline order (D-0040) and phase-scoped
 * mutability (§5, lccwld §4.9/§4.10).  Lua hooks observe and mutate
 * through the link handle; scope violations fail the link, and the
 * hook sequence itself proves dispatch order. */
#include "../../ccwld.h"
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
obj_with_text (const char *leaf)
{
  static const unsigned char code[] = { 0x55, 0x48, 0x89, 0xe5,
                                        0x5d, 0xc3 };
  ccwld_test_sec secs[] = {
    { ".text", 1, 0x6, 16, code, sizeof (code) },
  };
  ccwld_test_sym syms[] = {
    { "_start", 1, 1, 0, sizeof (code) },
  };
  char *path = ccwld_test_tmp (leaf);
  CHECK (ccwld_test_write_rel (path, secs, 1, syms, 1));
  return path;
}

/* link `lua` (a script body) and return the exit code ccwld_link_run
 * produced (0 on success) */
static int
link_lua (const char *body, ccwld_plan **keep)
{
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));
  char *path = ccwld_test_tmp ("phases.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  int ok = ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                          &e);
  free (path);
  if (!ok)
    {
      fprintf (stderr, "phases: script failed: %s\n", e.message);
      CHECK (0);
      return e.code ? e.code : 2;
    }
  char *out = ccwld_test_tmp ("phases.out");
  int linked = ccwld_link_run (p, out, &e);
  free (out);
  if (keep && linked)
    *keep = p;
  else
    ccwld_plan_free (p);
  return linked ? 0 : e.code;
}

static void
test_order_and_scopes (char *obj)
{
  /* hooks run resolved ▸ gc ▸ layout ▸ emit; the emit hook both
   * verifies the observed order and the mutability that succeeded */
  char buf[2048];
  snprintf (buf, sizeof (buf),
    "local order = {}\n"
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.on('resolved', function (link)\n"
    "  table.insert(order, 'resolved')\n"
    "  link.set_symbol('_start', 0x4096)\n"
    "end)\n"
    "ccwld.on('gc', function (link)\n"
    "  table.insert(order, 'gc')\n"
    "  link.keep_section('.text*')\n"
    "end)\n"
    "ccwld.on('layout', function (link)\n"
    "  table.insert(order, 'layout')\n"
    "  local v = nil\n"
    "  for i = 1, #link.symbols do\n"
    "    if link.symbols[i].name == '_start' then\n"
    "      v = link.symbols[i].value\n"
    "    end\n"
    "  end\n"
    "  if v ~= 0x4096 then ccwld.error('_start not set at resolved') end\n"
    "  if #link.sections == 0 then ccwld.error('no sections') end\n"
    "end)\n"
    "ccwld.on('emit', function (link)\n"
    "  table.insert(order, 'emit')\n"
    "  if order[1] ~= 'resolved' or order[2] ~= 'gc'\n"
    "     or order[3] ~= 'layout' or order[4] ~= 'emit' then\n"
    "    ccwld.error('phase order violated: ' .. table.concat(order, ','))\n"
    "  end\n"
    "  link.add_note('phases', 'ok')\n"
    "end)\n",
    obj);
  CHECK (link_lua (buf, NULL) == 0);
}

static void
test_scope_violations (char *obj)
{
  char buf[1024];
  /* set_symbol outside resolved/layout is a scope violation */
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.on('gc', function (link) link.set_symbol('_start', 1) end)\n",
    obj);
  CHECK (link_lua (buf, NULL) == 1);

  /* add_note outside emit is a scope violation */
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.on('layout', function (link) link.add_note('k', 'v') end)\n",
    obj);
  CHECK (link_lua (buf, NULL) == 1);

  /* keep_section outside gc is a scope violation */
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.on('resolved', function (link) link.keep_section('.text') end)\n",
    obj);
  CHECK (link_lua (buf, NULL) == 1);
}

static void
test_conflict_order (char *obj)
{
  /* plugin and hook both set the same symbol at resolved: CCWld is
   * the conflict authority — deterministic plugin-first order plus a
   * mandatory warning; the link itself still succeeds */
  char buf[1024];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.plugin('%s', {set_name='_start', set_value=7})\n"
    "ccwld.on('resolved', function (link)\n"
    "  link.set_symbol('_start', 9)\n"
    "end)\n",
    obj, CCWLD_TEST_PLUGIN);
  CHECK (link_lua (buf, NULL) == 0);
}

int
main (void)
{
  char *obj = obj_with_text ("phases.o");
  test_order_and_scopes (obj);
  test_scope_violations (obj);
  test_conflict_order (obj);
  free (obj);
  return ccwld_test_failures () ? 1 : 0;
}
