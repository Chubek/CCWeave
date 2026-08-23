/* lto_ref — the in-tree reference LTO backend (CCWLD.md §4, ccwld-lto.h).
 *
 * Built as a MODULE and dlopened by the pipeline.  It consumes the
 * "CCWIR1" text IR carried in a `.ccw.lto` input section and lowers
 * each module to a native ELF64 little-endian ET_REL object through
 * the emit callback, which re-enters the pipeline before gc (D-0041).
 *
 * IR format (one module per .ccw.lto payload, line-oriented):
 *   CCWIR1                 header (required, first line)
 *   text <hex bytes>       concatenated into the module's .text
 *   sym <name> <bind> <value-hex>
 *                          defined symbol; bind is global or weak,
 *                          value is a hex offset into .text
 * Everything is laid out in first-occurrence order: the output is a
 * pure function of the IR text (§ determinism).  The generated object
 * also defines `__lto_jobs_<n>` recording the session's job count so
 * tests can assert the reproducible jobs=1 pinning. */
#include "../../abi/ccwld-lto.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- backend state --- */

typedef struct
{
  unsigned char *text;
  size_t ntext, ctext;
  char **names;
  unsigned char *binds; /* 0 global, 1 weak */
  uint64_t *values;
  size_t nsyms, csyms;
  char error[256];
  int abi_seen;
  unsigned jobs_seen;
  int module_open;
} ref_lto;

ccwld_lto_ctx *
ccwld_lto_begin (const ccwld_lto_config *cfg)
{
  if (!cfg || cfg->abi_version != CCWLD_LTO_ABI_VERSION)
    return NULL; /* ccwld reports exit 3 with our last_error */
  ref_lto *r = calloc (1, sizeof (*r));
  if (r)
    {
      r->abi_seen = cfg->abi_version;
      r->jobs_seen = cfg->jobs;
    }
  return (ccwld_lto_ctx *)r;
}

static void
ref_reset_module (ref_lto *r)
{
  free (r->text);
  r->text = NULL;
  r->ntext = r->ctext = 0;
  for (size_t i = 0; i < r->nsyms; i++)
    free (r->names[i]);
  free (r->names);
  free (r->binds);
  free (r->values);
  r->names = NULL;
  r->binds = NULL;
  r->values = NULL;
  r->nsyms = r->csyms = 0;
  r->module_open = 0;
}

static int
ref_fail (ref_lto *r, const char *msg)
{
  snprintf (r->error, sizeof (r->error), "%s", msg);
  return -1;
}

static int
hex_nibble (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

int
ccwld_lto_add_module (ccwld_lto_ctx *ctx, const void *bytes, size_t n,
                      const char *name)
{
  ref_lto *r = (ref_lto *)ctx;
  if (!r || !bytes)
    return ref_fail (r, "invalid module");
  (void)name;

  ref_reset_module (r);

  /* work on a NUL-terminated copy so line parsing is safe */
  char *src = malloc (n + 1);
  if (!src)
    return ref_fail (r, "out of memory");
  memcpy (src, bytes, n);
  src[n] = 0;

  char *save = NULL;
  int first = 1;
  int rc = 0;
  for (char *line = strtok_r (src, "\n", &save); line;
       line = strtok_r (NULL, "\n", &save))
    {
      while (*line == ' ' || *line == '\t' || *line == '\r')
        line++;
      if (!*line)
        continue;
      if (first)
        {
          first = 0;
          if (strcmp (line, "CCWIR1") != 0)
            {
              rc = ref_fail (r, "module does not start with the CCWIR1 "
                                "header");
              break;
            }
          r->module_open = 1;
          continue;
        }
      if (!r->module_open)
        {
          rc = ref_fail (r, "content before the CCWIR1 header");
          break;
        }
      if (!strncmp (line, "text ", 5))
        {
          const char *h = line + 5;
          for (;;)
            {
              while (*h == ' ' || *h == '\t')
                h++;
              int hi = hex_nibble (h[0]), lo = hex_nibble (h[1]);
              if (hi < 0 || lo < 0)
                break;
              if (r->ntext == r->ctext)
                {
                  r->ctext = r->ctext ? r->ctext * 2 : 64;
                  unsigned char *t = realloc (r->text, r->ctext);
                  if (!t)
                    {
                      rc = ref_fail (r, "out of memory");
                      goto out;
                    }
                  r->text = t;
                }
              r->text[r->ntext++] = (unsigned char)((hi << 4) | lo);
              h += 2;
            }
        }
      else if (!strncmp (line, "sym ", 4))
        {
          char nm[128], bind[16];
          unsigned long long val = 0;
          if (sscanf (line, "sym %127s %15s %llx", nm, bind, &val) != 3)
            {
              rc = ref_fail (r, "malformed sym line");
              break;
            }
          if (r->nsyms == r->csyms)
            {
              r->csyms = r->csyms ? r->csyms * 2 : 8;
              char **nn = realloc (r->names, r->csyms * sizeof (char *));
              unsigned char *nb = realloc (r->binds, r->csyms);
              uint64_t *nv = realloc (r->values, r->csyms * sizeof (uint64_t));
              if (!nn || !nb || !nv)
                {
                  free (nn);
                  free (nb);
                  free (nv);
                  rc = ref_fail (r, "out of memory");
                  goto out;
                }
              r->names = nn;
              r->binds = nb;
              r->values = nv;
            }
          r->names[r->nsyms] = strdup (nm);
          r->binds[r->nsyms] = (unsigned char)(strcmp (bind, "weak") == 0);
          r->values[r->nsyms] = (uint64_t)val;
          r->nsyms++;
        }
      else
        {
          rc = ref_fail (r, "unknown IR line");
          break;
        }
    }
  if (!rc && first)
    rc = ref_fail (r, "empty module");

out:
  free (src);
  return rc;
}

/* --- minimal ELF64 LE ET_REL writer --------------------------------- */

typedef struct
{
  unsigned char *b;
  size_t n, c;
} buf;

static int
bput (buf *o, const void *d, size_t n)
{
  if (o->n + n > o->c)
    {
      size_t nc = o->c ? o->c * 2 : 256;
      while (nc < o->n + n)
        nc *= 2;
      unsigned char *nb = realloc (o->b, nc);
      if (!nb)
        return 0;
      o->b = nb;
      o->c = nc;
    }
  memcpy (o->b + o->n, d, n);
  o->n += n;
  return 1;
}

static int
bu8 (buf *o, unsigned char v) { return bput (o, &v, 1); }
static int
bu16 (buf *o, uint16_t v)
{
  unsigned char d[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
  return bput (o, d, 2);
}
static int
bu32 (buf *o, uint32_t v)
{
  unsigned char d[4] = { (unsigned char)v, (unsigned char)(v >> 8),
                         (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
  return bput (o, d, 4);
}
static int
bu64 (buf *o, uint64_t v)
{
  unsigned char d[8];
  for (int i = 0; i < 8; i++)
    d[i] = (unsigned char)(v >> (8 * i));
  return bput (o, d, 8);
}

/* Lower the accumulated module to one ELF64 ET_REL image in `o`. */
static int
emit_elf (ref_lto *r, buf *o, const char *jobs_name)
{
  /* section order: NULL, .text, .symtab, .strtab, .shstrtab */
  size_t extra = jobs_name ? 1 : 0;
  size_t nsyms_total = 1 + r->nsyms + extra;

  /* string tables */
  buf strtab = { 0 }, shstrtab = { 0 };
  if (!bu8 (&strtab, 0))
    goto oom;
  size_t nname_offs = r->nsyms + extra;
  uint32_t *name_off = calloc (nname_offs ? nname_offs : 1, sizeof (uint32_t));
  if (!name_off)
    goto oom;
  for (size_t i = 0; i < r->nsyms; i++)
    {
      name_off[i] = (uint32_t)strtab.n;
      if (!bput (&strtab, r->names[i], strlen (r->names[i]) + 1))
        goto oom;
    }
  uint32_t jobs_off = 0;
  if (jobs_name)
    {
      jobs_off = (uint32_t)strtab.n;
      if (!bput (&strtab, jobs_name, strlen (jobs_name) + 1))
        goto oom;
    }

  uint32_t off_text, off_symtab, off_strtab, off_shstrtab;
  if (!bu8 (&shstrtab, 0))
    goto oom;
  off_text = (uint32_t)shstrtab.n;
  if (!bput (&shstrtab, ".text", 6))
    goto oom;
  off_symtab = (uint32_t)shstrtab.n;
  if (!bput (&shstrtab, ".symtab", 8))
    goto oom;
  off_strtab = (uint32_t)shstrtab.n;
  if (!bput (&shstrtab, ".strtab", 8))
    goto oom;
  off_shstrtab = (uint32_t)shstrtab.n;
  if (!bput (&shstrtab, ".shstrtab", 10))
    goto oom;

  /* ELF header */
  size_t ehsize = 64, shentsize = 64;
  if (!bu8 (o, "\177ELF", 4))
    goto oom;
  if (!bu8 (o, 2) || !bu8 (o, 1) || !bu8 (o, 1)) /* 64-bit, LSB, SysV */
    goto oom;
  for (int i = 0; i < 9; i++)
    if (!bu8 (o, 0))
      goto oom;
  if (!bu16 (o, 1))  /* ET_REL */
    goto oom;
  if (!bu16 (o, 62)) /* EM_X86_64 */
    goto oom;
  if (!bu32 (o, 1))  /* EV_CURRENT */
    goto oom;
  if (!bu64 (o, 0) || !bu64 (o, 0)) /* entry, phoff */
    goto oom;
  size_t shoff_pos = o->n;
  if (!bu64 (o, 0)) /* shoff patched later */
    goto oom;
  if (!bu32 (o, 0)) /* flags */
    goto oom;
  if (!bu16 (o, 64) || !bu16 (o, 56) || !bu16 (o, 0)) /* ehsize, phentsize,
                                                         phnum */
    goto oom;
  if (!bu16 (o, shentsize) || !bu16 (o, 5) || !bu16 (o, 4)) /* shentsize,
                                                               shnum=5,
                                                               shstrndx=4 */
    goto oom;
  while (o->n < ehsize)
    if (!bu8 (o, 0))
      goto oom;

  size_t text_off = o->n;
  if (r->ntext && !bput (o, r->text, r->ntext))
    goto oom;
  while (o->n % 8)
    if (!bu8 (o, 0))
      goto oom;
  size_t symtab_off = o->n;
  /* null symbol */
  for (int i = 0; i < 24; i++)
    if (!bu8 (o, 0))
      goto oom;
  for (size_t i = 0; i < r->nsyms; i++)
    {
      if (!bu32 (o, name_off[i]))
        goto oom;
      if (!bu8 (o, (unsigned char)((r->binds[i] ? 2 : 1) << 4 | 2))) /* STB_*|STT_FUNC */
        goto oom;
      if (!bu8 (o, 0)) /* st_other */
        goto oom;
      if (!bu16 (o, 1)) /* .text */
        goto oom;
      if (!bu64 (o, r->values[i]) || !bu64 (o, 1))
        goto oom;
    }
  if (jobs_name)
    {
      if (!bu32 (o, jobs_off))
        goto oom;
      if (!bu8 (o, (unsigned char)(1 << 4 | 1))) /* GLOBAL|STT_OBJECT */
        goto oom;
      if (!bu8 (o, 0))
        goto oom;
      if (!bu16 (o, 0xfff1)) /* SHN_ABS */
        goto oom;
      if (!bu64 (o, r->jobs_seen) || !bu64 (o, 8))
        goto oom;
    }
  size_t strtab_off = o->n;
  if (!bput (o, strtab.b, strtab.n))
    goto oom;
  size_t shstrtab_off = o->n;
  if (!bput (o, shstrtab.b, shstrtab.n))
    goto oom;
  while (o->n % 8)
    if (!bu8 (o, 0))
      goto oom;
  size_t shoff = o->n;

  /* section headers: NULL, .text, .symtab, .strtab, .shstrtab */
  for (int i = 0; i < 5; i++)
    {
      uint32_t name = 0, type = 0, link = 0, info = 0;
      uint64_t flags = 0, addr = 0, offset = 0, size = 0, align = 0,
               entsize = 0;
      switch (i)
        {
        case 1:
          name = off_text;
          type = 1; /* PROGBITS */
          flags = 0x6; /* ALLOC|EXECINSTR */
          offset = text_off;
          size = r->ntext;
          align = 16;
          break;
        case 2:
          name = off_symtab;
          type = 2; /* SYMTAB */
          link = 3; /* .strtab */
          info = 1; /* first global = symbol 1 */
          offset = symtab_off;
          size = nsyms_total * 24;
          align = 8;
          entsize = 24;
          break;
        case 3:
          name = off_strtab;
          type = 3; /* STRTAB */
          offset = strtab_off;
          size = strtab.n;
          align = 1;
          break;
        case 4:
          name = off_shstrtab;
          type = 3;
          offset = shstrtab_off;
          size = shstrtab.n;
          align = 1;
          break;
        default:
          break; /* NULL section */
        }
      if (!bu32 (o, name) || !bu32 (o, type) || !bu64 (o, flags)
          || !bu64 (o, addr) || !bu64 (o, offset) || !bu64 (o, size)
          || !bu32 (o, link) || !bu32 (o, info) || !bu64 (o, align)
          || !bu64 (o, entsize))
        goto oom;
    }

  /* patch e_shoff */
  uint64_t shoff_le = (uint64_t)shoff;
  for (int i = 0; i < 8; i++)
    o->b[shoff_pos + i] = (unsigned char)(shoff_le >> (8 * i));

  free (strtab.b);
  free (shstrtab.b);
  free (name_off);
  return 1;

oom:
  free (strtab.b);
  free (shstrtab.b);
  free (name_off);
  return 0;
}

int
ccwld_lto_run (ccwld_lto_ctx *ctx,
               void (*emit) (void *, const void *, size_t, const char *),
               void *user)
{
  ref_lto *r = (ref_lto *)ctx;
  if (!r || !r->module_open)
    return ref_fail (r, "no module was fed to the backend");
  if (!emit)
    return ref_fail (r, "no emit callback");

  char jobs_name[32];
  snprintf (jobs_name, sizeof (jobs_name), "__lto_jobs_%u", r->jobs_seen);

  buf o = { 0 };
  if (!emit_elf (r, &o, jobs_name))
    {
      free (o.b);
      return ref_fail (r, "out of memory while emitting");
    }
  emit (user, o.b, o.n, "lto_ref");
  free (o.b);
  return 0;
}

void
ccwld_lto_end (ccwld_lto_ctx *ctx)
{
  ref_lto *r = (ref_lto *)ctx;
  if (!r)
    return;
  ref_reset_module (r);
  free (r);
}

const char *
ccwld_lto_last_error (ccwld_lto_ctx *ctx)
{
  /* ccwld calls this with NULL right after a rejected begin() */
  static const char *begin_msg = "abi version mismatch";
  return ctx ? ((ref_lto *)ctx)->error : begin_msg;
}
