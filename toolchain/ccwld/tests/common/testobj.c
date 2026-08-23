/* Shared helpers for the ccwld test suites — implementation. */
#include "testobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- assertion ledger --- */

static int g_failures = 0;

int
ccwld_test_failures (void)
{
  return g_failures;
}

void
ccwld_test_fail_inc (const char *file, int line, const char *what)
{
  g_failures++;
  fprintf (stderr, "FAIL %s:%d: %s\n", file, line, what);
}

/* --- temp paths --- */

static int g_tmp_seq = 0;

char *
ccwld_test_tmp (const char *leaf)
{
  const char *dir = getenv ("TMPDIR");
  if (!dir || !dir[0])
    dir = "/tmp";
  char *p = malloc (strlen (dir) + 64 + strlen (leaf));
  if (!p)
    return NULL;
  snprintf (p, strlen (dir) + 64 + strlen (leaf), "%s/ccwldtest-%ld-%d-%s",
            dir, (long)getpid (), g_tmp_seq++, leaf);
  return p;
}

int
ccwld_test_write_file (const char *path, const void *data, size_t n)
{
  FILE *f = fopen (path, "wb");
  if (!f)
    return 0;
  int ok = fwrite (data, 1, n, f) == n;
  fclose (f);
  return ok;
}

/* --- minimal ELF64 LE ET_REL writer ---------------------------------------
 * Layout: ehdr | section data | symtab | strtab | shstrtab | shdrs.
 * User sections come first (ELF indices 1..nsecs), then symtab,
 * strtab, shstrtab. */

static int
bput (unsigned char **b, size_t *n, size_t *c, const void *d, size_t k)
{
  if (*n + k > *c)
    {
      size_t nc = *c ? *c * 2 : 256;
      while (nc < *n + k)
        nc *= 2;
      unsigned char *nb = realloc (*b, nc);
      if (!nb)
        return 0;
      *b = nb;
      *c = nc;
    }
  memcpy (*b + *n, d, k);
  *n += k;
  return 1;
}

static int
bu8 (unsigned char **b, size_t *n, size_t *c, unsigned char v)
{
  return bput (b, n, c, &v, 1);
}
static int
bu16 (unsigned char **b, size_t *n, size_t *c, uint16_t v)
{
  unsigned char d[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
  return bput (b, n, c, d, 2);
}
static int
bu32 (unsigned char **b, size_t *n, size_t *c, uint32_t v)
{
  unsigned char d[4] = { (unsigned char)v, (unsigned char)(v >> 8),
                         (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
  return bput (b, n, c, d, 4);
}
static int
bu64 (unsigned char **b, size_t *n, size_t *c, uint64_t v)
{
  unsigned char d[8];
  for (int i = 0; i < 8; i++)
    d[i] = (unsigned char)(v >> (8 * i));
  return bput (b, n, c, d, 8);
}

int
ccwld_test_write_rel (const char *path, const ccwld_test_sec *secs,
                      size_t nsecs, const ccwld_test_sym *syms, size_t nsyms)
{
  size_t shnum = nsecs + 4;
  unsigned char *o = NULL;
  size_t n = 0, cap = 0;

  /* strtab for symbols */
  size_t strtab_cap = 64 + nsyms * 32;
  unsigned char *strtab = malloc (strtab_cap);
  size_t strn = 0, strc = strtab_cap;
  uint32_t *sym_name_off = calloc (nsyms ? nsyms : 1, sizeof (uint32_t));
  if (!strtab || !sym_name_off)
    goto oom;
  bu8 (&strtab, &strn, &strc, 0);
  for (size_t i = 0; i < nsyms; i++)
    {
      sym_name_off[i] = (uint32_t)strn;
      if (!bput (&strtab, &strn, &strc, syms[i].name,
                 strlen (syms[i].name) + 1))
        goto oom;
    }

  /* shstrtab */
  unsigned char *shstr = malloc (256);
  size_t shn = 0, shc = 256;
  uint32_t *sec_name_off = calloc (shnum ? shnum : 1, sizeof (uint32_t));
  if (!shstr || !sec_name_off)
    goto oom;
  bu8 (&shstr, &shn, &shc, 0);
  for (size_t i = 0; i < nsecs; i++)
    {
      sec_name_off[i + 1] = (uint32_t)shn;
      if (!bput (&shstr, &shn, &shc, secs[i].name, strlen (secs[i].name) + 1))
        goto oom;
    }
  uint32_t off_symtab = (uint32_t)shn;
  bput (&shstr, &shn, &shc, ".symtab", 8);
  uint32_t off_strtab = (uint32_t)shn;
  bput (&shstr, &shn, &shc, ".strtab", 8);
  uint32_t off_shstrtab = (uint32_t)shn;
  bput (&shstr, &shn, &shc, ".shstrtab", 10);

  /* ehdr */
  if (!bput (&o, &n, &cap, "\177ELF", 4))
    goto oom;
  if (!bu8 (&o, &n, &cap, 2) || !bu8 (&o, &n, &cap, 1) || !bu8 (&o, &n, &cap, 1))
    goto oom;
  for (int i = 0; i < 9; i++)
    if (!bu8 (&o, &n, &cap, 0))
      goto oom;
  if (!bu16 (&o, &n, &cap, 1) || !bu16 (&o, &n, &cap, 62) /* EM_X86_64 */
      || !bu32 (&o, &n, &cap, 1))
    goto oom;
  if (!bu64 (&o, &n, &cap, 0) || !bu64 (&o, &n, &cap, 0))
    goto oom;
  size_t shoff_pos = n;
  if (!bu64 (&o, &n, &cap, 0))
    goto oom;
  if (!bu32 (&o, &n, &cap, 0))
    goto oom;
  if (!bu16 (&o, &n, &cap, 64) || !bu16 (&o, &n, &cap, 0)
      || !bu16 (&o, &n, &cap, 0))
    goto oom;
  if (!bu16 (&o, &n, &cap, 64) || !bu16 (&o, &n, &cap, (uint16_t)shnum)
      || !bu16 (&o, &n, &cap, (uint16_t) (nsecs + 3)))
    goto oom;

  /* section data */
  size_t *sec_off = calloc (nsecs ? nsecs : 1, sizeof (size_t));
  if (!sec_off)
    goto oom;
  for (size_t i = 0; i < nsecs; i++)
    {
      while (n % 8)
        if (!bu8 (&o, &n, &cap, 0))
          goto oom;
      sec_off[i] = n;
      if (secs[i].type != 8 /* NOBITS */ && secs[i].size
          && !bput (&o, &n, &cap, secs[i].data, secs[i].size))
        goto oom;
    }

  /* symtab: null symbol then user symbols */
  while (n % 8)
    if (!bu8 (&o, &n, &cap, 0))
      goto oom;
  size_t symtab_off = n;
  for (int i = 0; i < 24; i++)
    if (!bu8 (&o, &n, &cap, 0))
      goto oom;
  for (size_t i = 0; i < nsyms; i++)
    {
      if (!bu32 (&o, &n, &cap, sym_name_off[i]))
        goto oom;
      if (!bu8 (&o, &n, &cap,
                (unsigned char)((syms[i].bind << 4) | 1 /* STT_OBJECT */)))
        goto oom;
      if (!bu8 (&o, &n, &cap, 0))
        goto oom;
      if (!bu16 (&o, &n, &cap, syms[i].shndx))
        goto oom;
      if (!bu64 (&o, &n, &cap, syms[i].value))
        goto oom;
      if (!bu64 (&o, &n, &cap, syms[i].size))
        goto oom;
    }
  size_t strtab_off = n;
  if (!bput (&o, &n, &cap, strtab, strn))
    goto oom;
  size_t shstr_off = n;
  if (!bput (&o, &n, &cap, shstr, shn))
    goto oom;
  while (n % 8)
    if (!bu8 (&o, &n, &cap, 0))
      goto oom;
  size_t shoff = n;

  /* shdrs */
  for (size_t s = 0; s < shnum; s++)
    {
      uint32_t name = 0, type = 0, link = 0, info = 0;
      uint64_t flags = 0, addr = 0, offset = 0, size = 0, align = 1,
               entsize = 0;
      if (s == 0)
        {
          /* NULL */
        }
      else if (s <= nsecs)
        {
          const ccwld_test_sec *t = &secs[s - 1];
          name = sec_name_off[s];
          type = t->type;
          flags = t->flags;
          offset = t->type == 8 ? 0 : sec_off[s - 1];
          size = t->size;
          align = t->align ? t->align : 1;
        }
      else if (s == nsecs + 1)
        {
          name = off_symtab;
          type = 2;
          link = (uint32_t) (nsecs + 2);
          info = 1;
          offset = symtab_off;
          size = (nsyms + 1) * 24;
          align = 8;
          entsize = 24;
        }
      else if (s == nsecs + 2)
        {
          name = off_strtab;
          type = 3;
          offset = strtab_off;
          size = strn;
          align = 1;
        }
      else
        {
          name = off_shstrtab;
          type = 3;
          offset = shstr_off;
          size = shn;
          align = 1;
        }
      if (!bu32 (&o, &n, &cap, name) || !bu32 (&o, &n, &cap, type)
          || !bu64 (&o, &n, &cap, flags) || !bu64 (&o, &n, &cap, addr)
          || !bu64 (&o, &n, &cap, offset) || !bu64 (&o, &n, &cap, size)
          || !bu32 (&o, &n, &cap, link) || !bu32 (&o, &n, &cap, info)
          || !bu64 (&o, &n, &cap, align) || !bu64 (&o, &n, &cap, entsize))
        goto oom;
    }
  for (int i = 0; i < 8; i++)
    o[shoff_pos + i] = (unsigned char)(((uint64_t)shoff) >> (8 * i));

  int ok = ccwld_test_write_file (path, o, n);
  free (o);
  free (strtab);
  free (shstr);
  free (sym_name_off);
  free (sec_name_off);
  free (sec_off);
  return ok;

oom:
  free (o);
  free (strtab);
  free (shstr);
  free (sym_name_off);
  free (sec_name_off);
  free (sec_off);
  return 0;
}

size_t
ccwld_test_file_size (const char *path)
{
  FILE *f = fopen (path, "rb");
  if (!f)
    return (size_t)-1;
  fseek (f, 0, SEEK_END);
  long n = ftell (f);
  fclose (f);
  return n < 0 ? (size_t)-1 : (size_t)n;
}

int
ccwld_test_files_equal (const char *a, const char *b)
{
  size_t na = ccwld_test_file_size (a), nb = ccwld_test_file_size (b);
  if (na == (size_t)-1 || nb == (size_t)-1 || na != nb)
    return 0;
  FILE *fa = fopen (a, "rb"), *fb = fopen (b, "rb");
  if (!fa || !fb)
    {
      if (fa)
        fclose (fa);
      if (fb)
        fclose (fb);
      return 0;
    }
  int same = 1;
  for (size_t i = 0; i < na && same; i++)
    {
      int ca = fgetc (fa), cb = fgetc (fb);
      if (ca != cb)
        same = 0;
    }
  fclose (fa);
  fclose (fb);
  return same;
}
