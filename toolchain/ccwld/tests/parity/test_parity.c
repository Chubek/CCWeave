/* parity/ — D-0034/D-0039 enforcement: the mpc ld-script frontend and
 * lccwld must lower to byte-identical canonical serializations of the
 * declarative plan (provenance sites, hooks, and driver options are
 * excluded by construction).  Each case runs the same link intent
 * through both frontends and string-compares the sealed plans. */
#include "../../ccwld.h"
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  const char *name;
  const char *ld;
  const char *lua;
} parity_case;

static const parity_case CASES[] = {
  {
    "io-commands",
    "ENTRY(_start)\n"
    "OUTPUT_FORMAT(\"elf\")\n"
    "SEARCH_DIR(\"/opt/lib\")\n"
    "SEARCH_DIR(\"/opt/lib64\")\n"
    "INPUT(a.o, b.o)\n"
    "STARTUP(crt0.o)\n"
    "GROUP(g1.a, g2.a)\n",
    "ccwld.output{kind = 'exe', format = 'elf', entry = '_start'}\n"
    "ccwld.search_path('/opt/lib')\n"
    "ccwld.search_path('/opt/lib64')\n"
    "ccwld.input('a.o', 'b.o')\n"
    "ccwld.startup('crt0.o')\n"
    "ccwld.group{'g1.a', 'g2.a'}\n",
  },
  {
    "memory-sections",
    "MEMORY { ram (rwx) : ORIGIN = 0x100000, LENGTH = 0x10000\n"
    "         rom (r)   : ORIGIN = 0x200000, LENGTH = 64K }\n"
    "SECTIONS {\n"
    "  .text 0x100000 : ALIGN(16) SUBALIGN(8)\n"
    "    { KEEP(*(.init)) *(.text .text.*) foo = . ; PROVIDE(pv = 1) }\n"
    "    > ram : text = 0x90\n"
    "  .data : { *(.data*) } > ram AT> rom\n"
    "  .bss (NOLOAD) : { *(.bss*) } > ram\n"
    "}\n"
    "__start = ADDR(.text);\n"
    ". = 0x1000;\n",
    "ccwld.memory{\n"
    "  {name='ram', attrs='rwx', origin=0x100000, length=0x10000},\n"
    "  {name='rom', attrs='r',   origin=0x200000, length=65536}}\n"
    "local foo = ccwld.assign('foo', ccwld.dot())\n"
    "local pv = ccwld.provide('pv', 1)\n"
    "ccwld.out('.text', {vma=0x100000, align=16, subalign=8,\n"
    "  input={ccwld.keep('*', '.init'),\n"
    "         ccwld.match('*', '.text .text.*')},\n"
    "  region='ram', phdr='text', fill=0x90, foo, pv})\n"
    "ccwld.out('.data', {input=ccwld.match('*', '.data*'),\n"
    "  region='ram', at_region='rom'})\n"
    "ccwld.out('.bss', {input=ccwld.match('*', '.bss*'),\n"
    "  region='ram', load=false})\n"
    "ccwld.assign('__start', ccwld.addr('.text'))\n"
    "ccwld.assign('.', 0x1000)\n",
  },
  {
    "phdrs-version",
    "PHDRS { text PT_LOAD FLAGS(rw) ; note PT_NOTE ; }\n"
    "VERSION { V1 { global: foo; bar; local: *; } V2 { global: baz; } }\n",
    "ccwld.phdrs{{name='text', type='PT_LOAD', flags='rw'},\n"
    "            {name='note', type='PT_NOTE'}}\n"
    "ccwld.version{{name='V1', globals={'foo', 'bar'}},\n"
    "              {name='V2', globals={'baz'}}}\n",
  },
  {
    "expressions",
    "PROVIDE_HIDDEN(ph = 0x40);\n"
    "__x = __a + 4 * __b;\n"
    "__y = MAX(__a, MIN(__b, __c));\n"
    "__z = DEFINED(__a) ? __b : __c;\n"
    "__w = ABSOLUTE(__a);\n"
    "__u = -__a + ~__b;\n"
    "__t = 0x10 << 2 | 1;\n",
    "ccwld.provide_hidden('ph', 0x40)\n"
    "ccwld.assign('__x', ccwld.symbol('__a') + 4 * ccwld.symbol('__b'))\n"
    "ccwld.assign('__y',\n"
    "  ccwld.max(ccwld.symbol('__a'),\n"
    "            ccwld.min(ccwld.symbol('__b'), ccwld.symbol('__c'))))\n"
    "ccwld.assign('__z',\n"
    "  ccwld.cond(ccwld.defined('__a'),\n"
    "             ccwld.symbol('__b'), ccwld.symbol('__c')))\n"
    "ccwld.assign('__w', ccwld.abs(ccwld.symbol('__a')))\n"
    "ccwld.assign('__u', -ccwld.symbol('__a') + ~ccwld.symbol('__b'))\n"
    "ccwld.assign('__t', 0x10 << 2 | 1)\n",
  },
};

int
main (void)
{
  for (size_t i = 0; i < sizeof (CASES) / sizeof (CASES[0]); i++)
    {
      const parity_case *c = &CASES[i];
      ccwld_error e;
      ccwld_plan *pl = NULL, *pd = NULL;
      memset (&e, 0, sizeof (e));

      char *lpath = ccwld_test_tmp ("p.lua");
      CHECK (ccwld_test_write_file (lpath, c->lua, strlen (c->lua)));
      int lu = ccwld_run_lua (lpath, "x86_64-ccweave", NULL, NULL, 0, NULL,
                              &pl, &e);
      free (lpath);
      if (!lu)
        {
          fprintf (stderr, "parity/%s lua: %s\n", c->name, e.message);
          CHECK (0);
          continue;
        }
      memset (&e, 0, sizeof (e));
      int ld_ok = ccwld_run_ldscript (c->ld, "p.ld", "x86_64-ccweave", NULL,
                                      &pd, &e);
      if (!ld_ok)
        {
          fprintf (stderr, "parity/%s ld: %s\n", c->name, e.message);
          CHECK (0);
          ccwld_plan_free (pl);
          continue;
        }
      if (!pl->serialized || !pd->serialized)
        {
          CHECK (pl->serialized != NULL && pd->serialized != NULL);
        }
      else if (strcmp (pl->serialized, pd->serialized) != 0)
        {
          fprintf (stderr, "parity/%s MISMATCH\n  ld  : %s\n  lua : %s\n",
                   c->name, pd->serialized, pl->serialized);
          CHECK (0);
        }
      ccwld_plan_free (pl);
      ccwld_plan_free (pd);
    }
  return ccwld_test_failures () ? 1 : 0;
}
