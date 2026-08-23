/* §3 load: input parsing.
 *
 * Reads ELF64 little-endian relocatable objects and DSOs, GNU-flavour
 * `ar` archives, and detects CCWld LTO modules (objects carrying a
 * `.ccw.lto` IR member).  Input order is preserved; every collection
 * here is ordered (§7). */
#include "ccwld_phases.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* --- ELF64 container definitions (reader-side) --- */

#define EI_NIDENT 16
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_REL 1
#define ET_DYN 3
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_DYNSYM 11
#define SHN_UNDEF 0
#define SHN_LORESERVE 0xff00
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define SHF_ALLOC 0x2

typedef uint64_t E64_Addr;
typedef uint64_t E64_Off;
typedef uint16_t E64_Half;
typedef uint32_t E64_Word;
typedef uint64_t E64_Xword;
typedef int64_t E64_Sxword;

typedef struct
{
  unsigned char e_ident[EI_NIDENT];
  E64_Half e_type;
  E64_Half e_machine;
  E64_Word e_version;
  E64_Addr e_entry;
  E64_Off e_phoff;
  E64_Off e_shoff;
  E64_Word e_flags;
  E64_Half e_ehsize;
  E64_Half e_phentsize;
  E64_Half e_phnum;
  E64_Half e_shentsize;
  E64_Half e_shnum;
  E64_Half e_shstrndx;
} E64_Ehdr;

typedef struct
{
  E64_Word sh_name;
  E64_Word sh_type;
  E64_Xword sh_flags;
  E64_Addr sh_addr;
  E64_Off sh_offset;
  E64_Xword sh_size;
  E64_Word sh_link;
  E64_Word sh_info;
  E64_Xword sh_addralign;
  E64_Xword sh_entsize;
} E64_Shdr;

typedef struct
{
  E64_Word st_name;
  unsigned char st_info;
  unsigned char st_other;
  E64_Half st_shndx;
  E64_Addr st_value;
  E64_Xword st_size;
} E64_Sym;

typedef struct
{
  E64_Addr r_offset;
  E64_Xword r_info;
  E64_Sxword r_addend;
} E64_Rela;

/* --- bounds-checked little-endian readers --- */

typedef struct
{
  const unsigned char *p;
  size_t n;
  const char *name;
} rd_buf;

static int
rd_bytes (rd_buf *b, size_t off, size_t len, void *out)
{
  if (off > b->n || len > b->n - off)
    return 0;
  memcpy (out, b->p + off, len);
  return 1;
}

static int
rd_u16 (rd_buf *b, size_t off, uint16_t *out)
{
  unsigned char t[2];
  if (!rd_bytes (b, off, 2, t))
    return 0;
  *out = (uint16_t)(t[0] | ((uint16_t)t[1] << 8));
  return 1;
}

static int
rd_u32 (rd_buf *b, size_t off, uint32_t *out)
{
  unsigned char t[4];
  if (!rd_bytes (b, off, 4, t))
    return 0;
  *out = (uint32_t)t[0] | ((uint32_t)t[1] << 8) | ((uint32_t)t[2] << 16)
         | ((uint32_t)t[3] << 24);
  return 1;
}

static int
rd_u64 (rd_buf *b, size_t off, uint64_t *out)
{
  unsigned char t[8];
  if (!rd_bytes (b, off, 8, t))
    return 0;
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--)
    v = (v << 8) | t[i];
  *out = v;
  return 1;
}

static char *
rd_cstr (rd_buf *b, size_t off, size_t limit)
{
  /* NUL-terminated string inside [off, off+limit) */
  if (off >= b->n)
    return NULL;
  size_t max = (limit && limit <= b->n - off) ? limit : b->n - off;
  const unsigned char *s = b->p + off;
  size_t len = 0;
  while (len < max && s[len])
    len++;
  char *out = malloc (len + 1);
  if (!out)
    return NULL;
  memcpy (out, s, len);
  out[len] = 0;
  return out;
}

/* --- small file reader --- */

static unsigned char *
read_file (const char *path, size_t *out_len)
{
  FILE *f = fopen (path, "rb");
  if (!f)
    return NULL;
  if (fseek (f, 0, SEEK_END) != 0)
    {
      fclose (f);
      return NULL;
    }
  long sz = ftell (f);
  if (sz < 0)
    {
      fclose (f);
      return NULL;
    }
  rewind (f);
  unsigned char *buf = malloc ((size_t)sz + 1);
  if (!buf)
    {
      fclose (f);
      return NULL;
    }
  if (sz > 0 && fread (buf, 1, (size_t)sz, f) != (size_t)sz)
    {
      free (buf);
      fclose (f);
      return NULL;
    }
  fclose (f);
  buf[sz] = 0;
  *out_len = (size_t)sz;
  return buf;
}

/* --- object construction helpers (shared with resolve/lto) --- */

ccwld_obj *
ccwld_state_add_obj (ccwld_state *st, const char *path, const char *kind,
                     const char *format)
{
  if ((st->nobjs + 1) > st->cobjs)
    {
      size_t c = st->cobjs ? st->cobjs * 2 : 8;
      ccwld_obj *o = realloc (st->objs, c * sizeof (*st->objs));
      if (!o)
        return NULL;
      st->objs = o;
      st->cobjs = c;
    }
  ccwld_obj *o = &st->objs[st->nobjs];
  memset (o, 0, sizeof (*o));
  o->path = strdup (path);
  o->kind = strdup (kind);
  o->format = strdup (format);
  if (!o->path || !o->kind || !o->format)
    return NULL;
  st->nobjs++;
  return o;
}

static ccwld_isec *
obj_add_sec (ccwld_obj *o, const char *name, const unsigned char *data,
             size_t size, uint64_t align, uint64_t flags, uint32_t type)
{
  ccwld_isec *secs = realloc (o->secs, (o->nsecs + 1) * sizeof (*secs));
  if (!secs)
    return NULL;
  o->secs = secs;
  ccwld_isec *s = &o->secs[o->nsecs];
  memset (s, 0, sizeof (*s));
  s->name = strdup (name);
  if (!s->name)
    return NULL;
  if (data && size && type != SHT_NOBITS)
    {
      s->data = malloc (size);
      if (!s->data)
        return NULL;
      memcpy (s->data, data, size);
    }
  s->size = size;
  s->align = align ? align : 1;
  s->flags = flags;
  s->type = type;
  s->obj = -1;
  s->shndx = (int)o->nsecs;
  s->out_sec = -1;
  s->live = 1;
  o->nsecs++;
  return s;
}

static ccwld_isym *
obj_add_sym (ccwld_obj *o, const char *name, int binding, int visibility,
             int shndx, uint64_t value, uint64_t size)
{
  ccwld_isym *syms = realloc (o->syms, (o->nsyms + 1) * sizeof (*syms));
  if (!syms)
    return NULL;
  o->syms = syms;
  ccwld_isym *s = &o->syms[o->nsyms];
  memset (s, 0, sizeof (*s));
  s->name = strdup (name ? name : "");
  if (!s->name)
    return NULL;
  s->binding = binding;
  s->visibility = visibility;
  s->shndx = shndx;
  s->value = value;
  s->size = size;
  s->obj = -1;
  o->nsyms++;
  return s;
}

static ccwld_ireloc *
obj_add_reloc (ccwld_obj *o, int sec, uint64_t offset, uint32_t type,
               const char *sym, int64_t addend)
{
  ccwld_ireloc *r
      = realloc (o->relocs, (o->nrelocs + 1) * sizeof (*o->relocs));
  if (!r)
    return NULL;
  o->relocs = r;
  ccwld_ireloc *x = &o->relocs[o->nrelocs];
  memset (x, 0, sizeof (*x));
  x->obj = -1;
  x->sec = sec;
  x->offset = offset;
  x->type = type;
  x->sym = strdup (sym ? sym : "");
  x->addend = addend;
  if (!x->sym)
    return NULL;
  o->nrelocs++;
  return x;
}

/* True when the ELF section is materialized as a ccwld_isec (i.e. it
 * is content, not metadata). */
static int
sec_materialized (E64_Word type, const char *name)
{
  if (type == SHT_SYMTAB || type == SHT_STRTAB || type == SHT_RELA
      || type == SHT_DYNSYM)
    return 0;
  return name[0] != 0;
}

/* Map an ELF section index to our isec index (0-based within the
 * object), or -1 when the ELF section was metadata. */
static int
map_shndx (int shnum, const E64_Shdr *sh, const char *shstr, int elfndx)
{
  if (elfndx <= 0 || elfndx >= shnum)
    return -1;
  int cnt = 0;
  for (int j = 0; j < elfndx; j++)
    {
      if (sec_materialized (sh[j].sh_type, shstr + sh[j].sh_name))
        cnt++;
    }
  return cnt;
}

/* --- ELF64 object loading (from memory; also used by the LTO path) --- */

int
ccwld_load_elf_mem (ccwld_state *st, const char *path,
                    const unsigned char *buf, size_t len, ccwld_error *e)
{
  rd_buf rb = { buf, len, path };
  E64_Ehdr eh;
  if (len < sizeof (eh)
      || !rd_bytes (&rb, 0, sizeof (eh), &eh)
      || eh.e_shentsize != sizeof (E64_Shdr))
    {
      ccwld_error_set (e, CCWLD_EXIT_LINK,
                       "%s: truncated or unsupported ELF header", path);
      return 0;
    }
  int is_dso = (eh.e_type == ET_DYN);
  if (eh.e_type != ET_REL && !is_dso)
    {
      ccwld_error_set (e, CCWLD_EXIT_LINK,
                       "%s: not a relocatable object or DSO (e_type=%u)", path,
                       (unsigned)eh.e_type);
      return 0;
    }
  if (eh.e_shnum == 0 || eh.e_shoff == 0)
    {
      ccwld_error_set (e, CCWLD_EXIT_LINK, "%s: object has no sections", path);
      return 0;
    }

  ccwld_obj *o = ccwld_state_add_obj (st, path, is_dso ? "dso" : "object",
                                      "elf64");
  if (!o)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  o->machine = eh.e_machine;
  o->is_dso = is_dso;
  int obj_idx = (int)(st->nobjs - 1);

  E64_Shdr *sh = malloc ((size_t)eh.e_shnum * sizeof (*sh));
  if (!sh)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  for (int i = 0; i < eh.e_shnum; i++)
    {
      size_t off = (size_t)eh.e_shoff + (size_t)i * sizeof (E64_Shdr);
      if (!rd_u32 (&rb, off, &sh[i].sh_name)
          || !rd_u32 (&rb, off + 4, &sh[i].sh_type)
          || !rd_u64 (&rb, off + 8, &sh[i].sh_flags)
          || !rd_u64 (&rb, off + 16, &sh[i].sh_addr)
          || !rd_u64 (&rb, off + 24, &sh[i].sh_offset)
          || !rd_u64 (&rb, off + 32, &sh[i].sh_size)
          || !rd_u32 (&rb, off + 40, &sh[i].sh_link)
          || !rd_u32 (&rb, off + 44, &sh[i].sh_info)
          || !rd_u64 (&rb, off + 48, &sh[i].sh_addralign)
          || !rd_u64 (&rb, off + 56, &sh[i].sh_entsize))
        {
          free (sh);
          ccwld_error_set (e, CCWLD_EXIT_LINK,
                           "%s: truncated section header %d", path, i);
          return 0;
        }
    }

  char *shstr = NULL;
  if (eh.e_shstrndx < eh.e_shnum)
    shstr = rd_cstr (&rb, sh[eh.e_shstrndx].sh_offset,
                     sh[eh.e_shstrndx].sh_size);
  if (!shstr)
    {
      free (sh);
      ccwld_error_set (e, CCWLD_EXIT_LINK, "%s: cannot read shstrtab", path);
      return 0;
    }

  /* content sections, in ELF order */
  for (int i = 0; i < eh.e_shnum; i++)
    {
      if (!sec_materialized (sh[i].sh_type, shstr + sh[i].sh_name))
        continue;
      const char *nm = shstr + sh[i].sh_name;
      const unsigned char *data = NULL;
      if (sh[i].sh_offset <= len)
        data = buf + sh[i].sh_offset;
      if (!obj_add_sec (o, nm, data, (size_t)sh[i].sh_size,
                        sh[i].sh_addralign, sh[i].sh_flags, sh[i].sh_type))
        {
          free (shstr);
          free (sh);
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
          return 0;
        }
      if (!strcmp (nm, ".ccw.lto"))
        o->is_lto = 1;
    }
  if (o->is_lto)
    o->kind = "lto-module";

  /* symbols: prefer SHT_SYMTAB; fall back to SHT_DYNSYM (DSOs) */
  for (int pass = 0; pass < 2; pass++)
    {
      E64_Word want = pass == 0 ? SHT_SYMTAB : SHT_DYNSYM;
      int found = -1;
      for (int i = 0; i < eh.e_shnum; i++)
        if (sh[i].sh_type == want)
          {
            found = i;
            break;
          }
      if (found < 0)
        continue;
      E64_Shdr *symsh = &sh[found];
      if (symsh->sh_link >= eh.e_shnum
          || sh[symsh->sh_link].sh_type != SHT_STRTAB)
        continue;
      char *strtab
          = rd_cstr (&rb, sh[symsh->sh_link].sh_offset,
                     sh[symsh->sh_link].sh_size);
      if (!strtab)
        continue;
      size_t n = (size_t)(symsh->sh_size / sizeof (E64_Sym));
      for (size_t k = 0; k < n; k++)
        {
          size_t off = (size_t)symsh->sh_offset + k * sizeof (E64_Sym);
          uint32_t st_name = 0;
          unsigned char st_info = 0, st_other = 0;
          uint16_t st_shndx = 0;
          uint64_t st_value = 0, st_size = 0;
          if (!rd_u32 (&rb, off, &st_name)
              || !rd_bytes (&rb, off + 4, 1, &st_info)
              || !rd_bytes (&rb, off + 5, 1, &st_other)
              || !rd_u16 (&rb, off + 6, &st_shndx)
              || !rd_u64 (&rb, off + 8, &st_value)
              || !rd_u64 (&rb, off + 16, &st_size))
            break;
          if (st_name >= sh[symsh->sh_link].sh_size)
            continue;
          const char *nm = strtab + st_name;
          if (!nm[0])
            continue;
          /* our isym.shndx: 0 = undefined, -1 = ABS/COMMON, else
           * isec index + 1 */
          int isec_idx;
          if (st_shndx == SHN_UNDEF)
            isec_idx = 0;
          else if (st_shndx >= SHN_LORESERVE)
            isec_idx = -1;
          else
            {
              int m = map_shndx (eh.e_shnum, sh, shstr, (int)st_shndx);
              isec_idx = m >= 0 ? m + 1 : -1;
            }
          if (!obj_add_sym (o, nm, st_info >> 4, st_other & 3, isec_idx,
                            st_value, st_size))
            {
              free (strtab);
              free (shstr);
              free (sh);
              ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
              return 0;
            }
        }
      free (strtab);
      break; /* one symbol table per object */
    }

  /* relocations */
  for (int i = 0; i < eh.e_shnum; i++)
    {
      if (sh[i].sh_type != SHT_RELA)
        continue;
      int target = map_shndx (eh.e_shnum, sh, shstr, (int)sh[i].sh_info);
      if (target < 0)
        continue;
      if (sh[i].sh_link >= eh.e_shnum)
        continue;
      E64_Shdr *symsh = &sh[sh[i].sh_link];
      if (symsh->sh_type != SHT_SYMTAB && symsh->sh_type != SHT_DYNSYM)
        continue;
      if (symsh->sh_link >= eh.e_shnum
          || sh[symsh->sh_link].sh_type != SHT_STRTAB)
        continue;
      char *strtab
          = rd_cstr (&rb, sh[symsh->sh_link].sh_offset,
                     sh[symsh->sh_link].sh_size);
      if (!strtab)
        continue;
      size_t n = (size_t)(sh[i].sh_size / sizeof (E64_Rela));
      for (size_t k = 0; k < n; k++)
        {
          size_t off = (size_t)sh[i].sh_offset + k * sizeof (E64_Rela);
          uint64_t r_offset = 0, info = 0, addend = 0;
          if (!rd_u64 (&rb, off, &r_offset) || !rd_u64 (&rb, off + 8, &info)
              || !rd_u64 (&rb, off + 16, &addend))
            break;
          uint32_t rtype = (uint32_t)(info & 0xffffffffu);
          size_t symidx = (size_t)(info >> 32);
          const char *sname = "";
          if (symidx > 0)
            {
              size_t soff
                  = (size_t)symsh->sh_offset + symidx * sizeof (E64_Sym);
              uint32_t st_name = 0;
              if (rd_u32 (&rb, soff, &st_name)
                  && st_name < sh[symsh->sh_link].sh_size)
                sname = strtab + st_name;
            }
          if (!obj_add_reloc (o, target, r_offset, rtype, sname,
                              (int64_t)addend))
            {
              free (strtab);
              free (shstr);
              free (sh);
              ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
              return 0;
            }
        }
      free (strtab);
    }

  free (shstr);
  free (sh);

  for (size_t i = 0; i < o->nsecs; i++)
    o->secs[i].obj = obj_idx;
  for (size_t i = 0; i < o->nsyms; i++)
    o->syms[i].obj = obj_idx;
  for (size_t i = 0; i < o->nrelocs; i++)
    o->relocs[i].obj = obj_idx;
  return 1;
}

/* --- GNU archive loading --- */

static int
load_archive (ccwld_state *st, const char *path, const unsigned char *buf,
              size_t len, ccwld_error *e)
{
  ccwld_obj *o = ccwld_state_add_obj (st, path, "archive", "archive");
  if (!o)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  /* members alias into this owned copy so extraction can happen lazily
   * during resolve, long after the load-phase buffer is freed */
  o->raw = malloc (len);
  if (!o->raw)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  memcpy (o->raw, buf, len);
  o->raw_len = len;
  buf = o->raw;

  /* long-name table (`//` member), symbol table (`/` member) */
  const char *longnames = NULL;
  size_t longnames_len = 0;
  long symtab_off = -1;

  size_t off = 8; /* skip "!<arch>\n" */
  while (off + 60 <= len)
    {
      char hdr[61];
      memcpy (hdr, buf + off, 60);
      hdr[60] = 0;
      char name[17];
      memcpy (name, hdr, 16);
      name[16] = 0;
      char *end = NULL;
      long size = strtol (hdr + 48, &end, 10);
      if (!end || size < 0)
        {
          ccwld_error_set (e, CCWLD_EXIT_LINK, "%s: bad archive member size",
                           path);
          return 0;
        }
      size_t body = off + 60;
      if (body + (size_t)size > len)
        {
          ccwld_error_set (e, CCWLD_EXIT_LINK, "%s: truncated archive member",
                           path);
          return 0;
        }

      if (name[0] == '/' && name[1] == 0)
        {
          symtab_off = (long)body;
        }
      else if (name[0] == '/' && name[1] == '/' && name[2] == 0)
        {
          longnames = (const char *)(buf + body);
          longnames_len = (size_t)size;
        }
      else
        {
          /* material member: record it */
          char *mname = NULL;
          char shortname[17];
          memcpy (shortname, hdr, 16);
          shortname[16] = 0;
          char *slash = strchr (shortname, '/');
          if (slash)
            *slash = 0;
          if (name[0] == '/' && name[1] != 0 && name[1] != '/')
            {
              /* long name: offset into the `//` table */
              long lo = strtol (name + 1, NULL, 10);
              if (longnames && lo >= 0 && (size_t)lo < longnames_len)
                {
                  const char *ls = longnames + lo;
                  size_t ll = 0;
                  while (ll < longnames_len - (size_t)lo && ls[ll]
                         && ls[ll] != '\n')
                    ll++;
                  mname = malloc (ll + 1);
                  if (mname)
                    {
                      memcpy (mname, ls, ll);
                      mname[ll] = 0;
                    }
                }
            }
          if (!mname)
            mname = strdup (shortname);

          ccwld_ar_member *ms
              = realloc (o->members, (o->nmembers + 1) * sizeof (*ms));
          if (!ms || !mname)
            {
              free (mname);
              ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
              return 0;
            }
          o->members = ms;
          o->members[o->nmembers].name = mname;
          o->members[o->nmembers].data = (unsigned char *)(buf + body);
          o->members[o->nmembers].size = (size_t)size;
          o->members[o->nmembers].hdr_off = (long)off;
          o->members[o->nmembers].extracted = 0;
          o->nmembers++;
        }

      /* members are 2-byte aligned */
      off = body + (size_t)size;
      if (off & 1)
        off++;
    }

  /* parse the symbol index ("/" member): u32be count, count u32be
   * member offsets, then NUL-terminated names */
  if (symtab_off >= 0)
    {
      size_t p = (size_t)symtab_off;
      uint32_t count = 0;
      if (p + 4 <= len)
        {
          count = ((uint32_t)buf[p] << 24) | ((uint32_t)buf[p + 1] << 16)
                  | ((uint32_t)buf[p + 2] << 8) | (uint32_t)buf[p + 3];
          p += 4;
        }
      long *offs = NULL;
      if (count && count < 0x1000000)
        {
          offs = malloc ((size_t)count * sizeof (*offs));
          if (!offs)
            {
              ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
              return 0;
            }
          for (uint32_t i = 0; i < count; i++)
            {
              if (p + 4 > len)
                break;
              offs[i] = (long)(((uint32_t)buf[p] << 24)
                               | ((uint32_t)buf[p + 1] << 16)
                               | ((uint32_t)buf[p + 2] << 8)
                               | (uint32_t)buf[p + 3]);
              p += 4;
            }
          for (uint32_t i = 0; i < count; i++)
            {
              char *nm = rd_cstr (&(rd_buf){ buf, len, path }, p, 0);
              if (!nm)
                break;
              p += strlen (nm) + 1;
              /* member index by header offset */
              int midx = -1;
              for (size_t j = 0; j < o->nmembers; j++)
                if (o->members[j].hdr_off == offs[i])
                  {
                    midx = (int)j;
                    break;
                  }
              if (midx >= 0)
                {
                  char **ns
                      = realloc (o->ar_syms,
                                 (o->nar_syms + 1) * sizeof (*o->ar_syms));
                  int *ni = realloc (o->ar_sym_member,
                                     (o->nar_syms + 1) * sizeof (*ni));
                  if (!ns || !ni)
                    {
                      free (ns);
                      free (ni);
                      free (nm);
                      free (offs);
                      ccwld_error_set (e, CCWLD_EXIT_INTERNAL,
                                       "out of memory");
                      return 0;
                    }
                  o->ar_syms = ns;
                  o->ar_sym_member = ni;
                  o->ar_syms[o->nar_syms] = nm;
                  o->ar_sym_member[o->nar_syms] = midx;
                  o->nar_syms++;
                }
              else
                free (nm);
            }
        }
      free (offs);
    }
  return 1;
}

/* --- the load phase --- */

int
ccwld_phase_load (ccwld_state *st, ccwld_error *e)
{
  ccwld_plan *p = st->plan;
  int machine_set = 0;

  if (p->ninputs == 0)
    {
      ccwld_error_set (e, CCWLD_EXIT_LINK, "no input files");
      return 0;
    }

  /* Startup objects are forced first in output order (lccwld §4.2);
   * the rest keep declaration order. */
  size_t *order = malloc (p->ninputs * sizeof (*order));
  if (!order)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  size_t k = 0;
  for (size_t i = 0; i < p->ninputs; i++)
    if (p->inputs[i].startup)
      order[k++] = i;
  for (size_t i = 0; i < p->ninputs; i++)
    if (!p->inputs[i].startup)
      order[k++] = i;

  for (size_t oi = 0; oi < p->ninputs; oi++)
    {
      size_t i = order[oi];
      char *path = ccwld_find_input (st, p->inputs[i].path);
      if (!path)
        {
          ccwld_error_set (e, CCWLD_EXIT_LINK, "cannot open input '%s'",
                           p->inputs[i].path);
          return 0;
        }
      size_t len = 0;
      unsigned char *buf = read_file (path, &len);
      if (!buf)
        {
          free (path);
          ccwld_error_set (e, CCWLD_EXIT_LINK, "cannot read input '%s'",
                           p->inputs[i].path);
          return 0;
        }

      int ok;
      if (len >= 8 && !memcmp (buf, "!<arch>\n", 8))
        ok = load_archive (st, path, buf, len, e);
      else if (len >= 5 && !memcmp (buf, "\x7f"
                                    "ELF",
                                    4)
               && buf[4] == ELFCLASS64 && buf[5] == ELFDATA2LSB)
        ok = ccwld_load_elf_mem (st, path, buf, len, e);
      else
        {
          ccwld_error_set (e, CCWLD_EXIT_LINK,
                           "%s: unrecognized input format (expected ELF64 "
                           "object or archive)",
                           path);
          ok = 0;
        }

      if (ok && st->nobjs > 0)
        {
          ccwld_obj *o = &st->objs[st->nobjs - 1];
          o->as_needed = p->inputs[i].as_needed || p->options.as_needed_default;
          o->from_group = p->inputs[i].is_group;
          if (!machine_set && o->machine)
            {
              st->machine = o->machine;
              machine_set = 1;
            }
        }
      free (buf);
      free (path);
      if (!ok)
        {
          free (order);
          return 0;
        }
    }
  free (order);
  return 1;
}
