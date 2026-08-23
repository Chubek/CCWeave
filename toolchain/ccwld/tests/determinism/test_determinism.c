/* determinism/ — §7/§6: the same plan links to byte-identical
 * artifacts, the canonical serialization is stable across runs, the
 * content cache short-circuits to the stored bytes, and a mutated
 * input changes the cache key. */
#include "../../cache/ccwld_cache.h"
#include "../../ccwld.h"
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static const unsigned char CODE[] = { 0x55, 0x48, 0x89, 0xe5,
                                      0x31, 0xc0, 0x5d, 0xc3 };

static const char *SCRIPT_FMT =
  "ccwld.input('%s')\n"
  "ccwld.output{kind='exe', format='elf', entry='_start'}\n"
  "ccwld.assign('stamp', 0x1000)\n"
  "ccwld.out('.text', {vma=ccwld.symbol('stamp') + 0x10,\n"
  "                    input=ccwld.match('*', '.text*')})\n";

static char *
make_obj (const char *leaf, unsigned char tweak)
{
  unsigned char code[sizeof (CODE)];
  memcpy (code, CODE, sizeof (code));
  code[0] = tweak;
  ccwld_test_sec secs[] = { { ".text", 1, 0x6, 16, code, sizeof (code) } };
  ccwld_test_sym syms[] = { { "_start", 1, 1, 0, sizeof (code) } };
  char *path = ccwld_test_tmp (leaf);
  CHECK (ccwld_test_write_rel (path, secs, 1, syms, 1));
  return path;
}

static int
link_to (const char *obj, const char *out, char **serialized)
{
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));
  char body[1024];
  snprintf (body, sizeof (body), SCRIPT_FMT, obj);
  char *path = ccwld_test_tmp ("det.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  int ok = ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                          &e);
  free (path);
  if (!ok)
    {
      fprintf (stderr, "determinism: script failed: %s\n", e.message);
      ccwld_plan_free (p);
      return e.code ? e.code : 2;
    }
  if (serialized)
    *serialized = strdup (p->serialized ? p->serialized : "");
  int linked = ccwld_link_run (p, out, &e);
  ccwld_plan_free (p);
  return linked ? 0 : e.code;
}

static void
test_double_link_identical (char *obj)
{
  char *out1 = ccwld_test_tmp ("d1.out");
  char *out2 = ccwld_test_tmp ("d2.out");
  char *s1 = NULL, *s2 = NULL;
  CHECK (link_to (obj, out1, &s1) == 0);
  CHECK (link_to (obj, out2, &s2) == 0);
  CHECK (s1 && s2 && strcmp (s1, s2) == 0);
  CHECK (ccwld_test_files_equal (out1, out2));
  free (s1);
  free (s2);
  free (out1);
  free (out2);
}

static void
test_cache_hit (char *obj)
{
  char *cachedir = ccwld_test_tmp ("cachedir");
  char *out1 = ccwld_test_tmp ("c1.out");
  char *out2 = ccwld_test_tmp ("c2.out");

  /* pre-create the cache directory (the driver normally does) */
  mkdir (cachedir, 0777);

  /* two independent plans over the same inputs: the first stores,
   * the second must hit and copy the stored bytes */
  char body[1024];
  snprintf (body, sizeof (body), SCRIPT_FMT, obj);
  for (int run = 0; run < 2; run++)
    {
      ccwld_error e;
      ccwld_plan *p = NULL;
      memset (&e, 0, sizeof (e));
      char *path = ccwld_test_tmp ("cache.lua");
      CHECK (ccwld_test_write_file (path, body, strlen (body)));
      CHECK (ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                            &e));
      free (path);
      CHECK (p != NULL);
      p->options.cache_dir = cachedir;
      CHECK (ccwld_link_run (p, run ? out2 : out1, &e));
      ccwld_plan_free (p);
    }
  CHECK (ccwld_test_files_equal (out1, out2));
  free (cachedir);
  free (out1);
  free (out2);
}

static void
test_key_sensitivity (char *obj)
{
  /* same object, same plan → same key; a changed input → new key */
  char *obj2 = make_obj ("det-tweaked.o", 0x56);
  char k1[65], k2[65], k3[65];
  ccwld_error e;
  ccwld_plan *p = NULL;
  memset (&e, 0, sizeof (e));

  char body[1024];
  snprintf (body, sizeof (body), SCRIPT_FMT, obj);
  char *path = ccwld_test_tmp ("key.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  CHECK (ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                        &e));
  free (path);
  CHECK (p != NULL);
  CHECK (ccwld_cache_key (p, k1));
  CHECK (ccwld_cache_key (p, k2));
  CHECK (strcmp (k1, k2) == 0);

  /* mutating the plan's options byte must change the key */
  p->options.gc_sections = 1;
  CHECK (ccwld_cache_key (p, k3));
  CHECK (strcmp (k1, k3) != 0);
  ccwld_plan_free (p);

  /* a changed input changes the key too */
  memset (&e, 0, sizeof (e));
  p = NULL;
  snprintf (body, sizeof (body), SCRIPT_FMT, obj2);
  path = ccwld_test_tmp ("key2.lua");
  CHECK (ccwld_test_write_file (path, body, strlen (body)));
  CHECK (ccwld_run_lua (path, "x86_64-ccweave", NULL, NULL, 0, NULL, &p,
                        &e));
  free (path);
  CHECK (p != NULL);
  char k4[65];
  CHECK (ccwld_cache_key (p, k4));
  CHECK (strcmp (k1, k4) != 0);
  ccwld_plan_free (p);
  free (obj2);
}

int
main (void)
{
  char *obj = make_obj ("det.o", 0x55);
  test_double_link_identical (obj);
  test_cache_hit (obj);
  test_key_sensitivity (obj);
  free (obj);
  return ccwld_test_failures () ? 1 : 0;
}
