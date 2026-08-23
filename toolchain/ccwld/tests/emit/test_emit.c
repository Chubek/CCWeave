/* emit/ — D-0043 no-passthrough: laid-out links emit real ELF64
 * (magic + class + machine), NOBITS sections take no file space, and
 * an alloc input section no selector places is a fatal, never a
 * silent copy into the output. */
#include "../../ccwld.h"
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char CODE[] = { 0x55, 0x48, 0x89, 0xe5,
                                      0x31, 0xc0, 0x5d, 0xc3 };

static int
link_script_out (const char *body, char **out_path)
{
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));
  char *path = ccwld_test_tmp ("emit.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  int ok = ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                          &e);
  free (path);
  if (!ok)
    {
      fprintf (stderr, "emit: script failed: %s\n", e.message);
      ccwld_plan_free (p);
      return e.code ? e.code : 2;
    }
  char *out = ccwld_test_tmp ("emit.out");
  int linked = ccwld_link_run (p, out, &e);
  if (out_path && linked)
    *out_path = out;
  else
    free (out);
  ccwld_plan_free (p);
  return linked ? 0 : e.code;
}

static void
test_elf_output (void)
{
  char *obj = ccwld_test_tmp ("emit.o");
  ccwld_test_sec secs[] = { { ".text", 1, 0x6, 16, CODE, sizeof (CODE) } };
  ccwld_test_sym syms[] = { { "_start", 1, 1, 0, sizeof (CODE) } };
  CHECK (ccwld_test_write_rel (obj, secs, 1, syms, 1));

  char buf[512];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    obj);
  char *out = NULL;
  CHECK (link_script_out (buf, &out) == 0);
  CHECK (out != NULL);

  /* parse the emitted ELF header */
  FILE *f = fopen (out, "rb");
  CHECK (f != NULL);
  unsigned char eh[64];
  CHECK (fread (eh, 1, sizeof (eh), f) == sizeof (eh));
  fclose (f);
  CHECK (eh[0] == 0x7f && eh[1] == 'E' && eh[2] == 'L' && eh[3] == 'F');
  CHECK (eh[4] == 2); /* ELFCLASS64 */
  CHECK (eh[5] == 1); /* ELFDATA2LSB */
  CHECK (eh[18] == 62 && eh[19] == 0); /* EM_X86_64 */

  /* the linked .text bytes must appear in the file */
  f = fopen (out, "rb");
  CHECK (f != NULL);
  unsigned char *all = malloc (ccwld_test_file_size (out));
  size_t n = fread (all, 1, ccwld_test_file_size (out), f);
  fclose (f);
  int found = 0;
  for (size_t i = 0; i + sizeof (CODE) <= n; i++)
    if (!memcmp (all + i, CODE, sizeof (CODE)))
      found = 1;
  CHECK (found);
  free (all);
  free (out);
  free (obj);
}

static void
test_nobits_no_filespace (void)
{
  static const unsigned char bss_marker[1] = { 0 };
  char *obj = ccwld_test_tmp ("bss.o");
  ccwld_test_sec secs[] = {
    { ".text", 1, 0x6, 16, CODE, sizeof (CODE) },
    { ".bss", 8, 0x3, 8, bss_marker, 4096 }, /* NOBITS: no contents */
  };
  ccwld_test_sym syms[] = { { "_start", 1, 1, 0, sizeof (CODE) } };
  CHECK (ccwld_test_write_rel (obj, secs, 2, syms, 1));

  char buf[512];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n"
    "ccwld.out('.bss', {input=ccwld.match('*', '.bss*'), load=false})\n",
    obj);
  char *out = NULL;
  CHECK (link_script_out (buf, &out) == 0);
  CHECK (out != NULL);

  /* the file must be far smaller than the 4 KiB .bss would need */
  size_t sz = ccwld_test_file_size (out);
  CHECK (sz != (size_t)-1);
  CHECK (sz < 4096);
  free (out);
  free (obj);
}

static void
test_unplaced_fatal (void)
{
  static const unsigned char custom[4] = { 1, 2, 3, 4 };
  char *obj = ccwld_test_tmp ("custom.o");
  ccwld_test_sec secs[] = {
    { ".text", 1, 0x6, 16, CODE, sizeof (CODE) },
    { ".weird", 1, 0x3, 8, custom, sizeof (custom) }, /* alloc, unplaced */
  };
  ccwld_test_sym syms[] = { { "_start", 1, 1, 0, sizeof (CODE) } };
  CHECK (ccwld_test_write_rel (obj, secs, 2, syms, 1));

  /* selectors only match .text*: .weird stays unplaced → exit 1 */
  char buf[512];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    obj);
  CHECK (link_script_out (buf, NULL) == 1);

  /* explicit placement of the same section succeeds */
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n"
    "ccwld.out('.weird', {input=ccwld.match('*', '.weird')})\n",
    obj);
  CHECK (link_script_out (buf, NULL) == 0);
  free (obj);
}

static void
test_undefined_strong_fatal (void)
{
  char *obj = ccwld_test_tmp ("undef.o");
  ccwld_test_sec secs[] = { { ".text", 1, 0x6, 16, CODE, sizeof (CODE) } };
  ccwld_test_sym syms[] = {
    { "_start", 1, 1, 0, sizeof (CODE) },
    { "missing_fn", 1, 0, 0, 0 }, /* SHN_UNDEF: strong undefined */
  };
  CHECK (ccwld_test_write_rel (obj, secs, 1, syms, 2));

  char buf[512];
  snprintf (buf, sizeof (buf),
    "ccwld.input('%s')\n"
    "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
    "ccwld.out('.text', {input=ccwld.match('*', '.text*')})\n",
    obj);
  CHECK (link_script_out (buf, NULL) == 1);
  free (obj);
}

int
main (void)
{
  test_elf_output ();
  test_nobits_no_filespace ();
  test_unplaced_fatal ();
  test_undefined_strong_fatal ();
  return ccwld_test_failures () ? 1 : 0;
}
