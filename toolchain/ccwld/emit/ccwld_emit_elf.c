/* §6/§8: self-contained ELF64 emitter + producer note.
 *
 * Builds the ELF object model from the laid-out plan: output sections
 * in plan order, program headers from the plan's phdr nodes, the
 * resolved symbol table, and the `.note.ccw` producer note.  All
 * ordering is plan/link order — byte-identical across runs (§7). */
#include "ccwld_emit.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef CCWLD_VERSION
#define CCWLD_VERSION "0.1.0"
#endif

/* --- ELF64 container definitions (writer-side) --- */

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64_D 62
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1

typedef struct
{
  unsigned char e_ident[16];
  uint16_t e_type, e_machine;
  uint32_t e_version;
  uint64_t e_entry, e_phoff, e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} W_Ehdr;

typedef struct
{
  uint32_t sh_name, sh_type;
  uint64_t sh_flags, sh_addr, sh_offset, sh_size;
  uint32_t sh_link, sh_info;
  uint64_t sh_addralign, sh_entsize;
} W_Shdr;

typedef struct
{
  uint32_t p_type, p_flags;
  uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} W_Phdr;

typedef struct
{
  uint32_t st_name;
  unsigned char st_info, st_other;
  uint16_t st_shndx;
  uint64_t st_value, st_size;
} W_Sym;

static void
w_u16 (unsigned char *p, uint16_t v)
{
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
}

static void
w_u32 (unsigned char *p, uint32_t v)
{
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}

static void
w_u64 (unsigned char *p, uint64_t v)
{
  for (int i = 0; i < 8; i++)
    p[i] = (unsigned char)(v >> (8 * i));
}

/* --- producer note (§8) --- */

size_t
ccwld_emit_note_text (ccwld_state *st, char *buf, size_t buflen)
{
  ccwld_plan *p = st->plan;
  size_t n = 0;

#define NOTE_APPEND(...)                                                       \
  do                                                                           \
    {                                                                          \
      if (n + 1 < buflen)                                                      \
        n += (size_t)snprintf (buf + n, buflen - n, __VA_ARGS__);              \
    }                                                                          \
  while (0)

  NOTE_APPEND ("producer=ccwld-%s\n", CCWLD_VERSION);
  NOTE_APPEND ("frontends=%s\n", p->frontend ? p->frontend : "api");
  NOTE_APPEND ("plan-hash=%s\n", p->plan_hash);
  if (st->lto_backend_used)
    NOTE_APPEND ("lto=%s@%u\n", st->lto_backend_used, st->lto_abi_used);
  if (p->nplugins > 0)
    {
      NOTE_APPEND ("plugins=");
      for (size_t i = 0; i < p->nplugins; i++)
        NOTE_APPEND ("%s%s@%u", i ? "," : "",
                     p->plugins[i].name ? p->plugins[i].name
                                        : p->plugins[i].path,
                     (unsigned)CCWLD_PLUGIN_ABI_VERSION);
      NOTE_APPEND ("\n");
    }
  NOTE_APPEND ("reproducible=%s\n", p->reproducible ? "true" : "false");
  {
    const char *sde = getenv ("SOURCE_DATE_EPOCH");
    if (sde)
      NOTE_APPEND ("source-date-epoch=%s\n", sde);
  }
  for (size_t i = 0; i < st->nnotes; i++)
    NOTE_APPEND ("%s=%s\n", st->notes[i].key, st->notes[i].value);
#undef NOTE_APPEND
  if (n + 1 < buflen)
    buf[n] = 0;
  return n;
}

/* --- string table builder --- */

typedef struct
{
  char *buf;
  size_t len, cap;
} strtab_t;

static int
strtab_init (strtab_t *t)
{
  t->cap = 256;
  t->len = 1;
  t->buf = calloc (t->cap, 1);
  return t->buf != NULL;
}

static uint32_t
strtab_add (strtab_t *t, const char *s)
{
  if (!s || !s[0])
    return 0;
  size_t sl = strlen (s) + 1;
  if (t->len + sl > t->cap)
    {
      size_t c = t->cap * 2;
      while (c < t->len + sl)
        c *= 2;
      char *b = realloc (t->buf, c);
      if (!b)
        return 0;
      t->buf = b;
      memset (t->buf + t->cap, 0, c - t->cap);
      t->cap = c;
    }
  uint32_t off = (uint32_t)t->len;
  memcpy (t->buf + t->len, s, sl);
  t->len += sl;
  return off;
}

static void
strtab_free (strtab_t *t)
{
  free (t->buf);
}

/* plan section index for emitted index i (inverse of the emit filter) */
static size_t
sec_of_emit (const ccwld_plan *p, size_t i)
{
  size_t seen = 0;
  for (size_t s = 0; s < p->nsecs; s++)
    {
      if (!(p->secs[s].size > 0 || p->secs[s].load))
        continue;
      if (seen == i)
        return s;
      seen++;
    }
  return 0;
}

/* --- phdr type mapping --- */

static uint32_t
phdr_type (const char *type)
{
  if (!type)
    return PT_LOAD;
  if (!strcmp (type, "LOAD"))
    return PT_LOAD;
  if (!strcmp (type, "DYNAMIC"))
    return PT_DYNAMIC;
  if (!strcmp (type, "INTERP"))
    return PT_INTERP;
  if (!strcmp (type, "NOTE"))
    return PT_NOTE;
  if (!strcmp (type, "PHDR"))
    return PT_PHDR;
  return PT_LOAD;
}

static uint64_t
largest_congruent_alignment (uint64_t offset, uint64_t vaddr,
                             uint64_t requested)
{
  uint64_t align = requested;
  if (align == 0 || (align & (align - 1)) != 0)
    align = 1;
  while (align > 1 && ((offset ^ vaddr) & (align - 1)) != 0)
    align >>= 1;
  return align;
}

/* --- the ELF64 writer --- */

static int
emit_elf64 (ccwld_state *st, const char *path, ccwld_error *e)
{
  ccwld_plan *p = st->plan;
  int is_reloc = p->output.kind && !strcmp (p->output.kind, "reloc");
  int is_dsoish = p->output.kind
                  && (!strcmp (p->output.kind, "dso")
                      || !strcmp (p->output.kind, "pie"));

  /* map plan section index → emitted index (-1 = not emitted) */
  int *sec_emit_idx = malloc ((p->nsecs ? p->nsecs : 1) * sizeof (int));
  if (!sec_emit_idx)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  {
    size_t idx = 0;
    for (size_t i = 0; i < p->nsecs; i++)
      {
        if (p->secs[i].size > 0 || p->secs[i].load)
          sec_emit_idx[i] = (int)idx++;
        else
          sec_emit_idx[i] = -1;
      }
  }

  /* ---- section content ---- */
  size_t nout = 0;
  for (size_t i = 0; i < p->nsecs; i++)
    if (p->secs[i].size > 0 || p->secs[i].load)
      nout++;

  unsigned char **content = calloc (nout ? nout : 1, sizeof (*content));
  uint64_t *out_flags = calloc (nout ? nout : 1, sizeof (*out_flags));
  size_t *out_sizes = calloc (nout ? nout : 1, sizeof (*out_sizes));
  if (!content || !out_flags || !out_sizes)
    {
      free (content);
      free (out_flags);
      free (out_sizes);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }

  size_t oi = 0;
  for (size_t i = 0; i < p->nsecs; i++)
    {
      ccwld_sec *sec = &p->secs[i];
      if (!(sec->size > 0 || sec->load))
        continue;
      unsigned char *buf = calloc (sec->size ? sec->size : 1, 1);
      if (!buf)
        {
          for (size_t j = 0; j < oi; j++)
            free (content[j]);
          free (content);
          free (out_flags);
          free (out_sizes);
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
          return 0;
        }
      /* fill value from the deferred fill expression (default zero) */
      if (sec->fill)
        {
          char *emsg = NULL;
          uint64_t v = 0;
          if (ccwld_expr_eval (sec->fill, p, sec->vma + sec->size, &v, &emsg))
            memset (buf, (int)(v & 0xff), sec->size);
          free (emsg);
        }
      /* copy member data (relocations were applied in place) */
      for (size_t j = 0; j < st->nobjs; j++)
        {
          ccwld_obj *o = &st->objs[j];
          for (size_t k = 0; k < o->nsecs; k++)
            {
              ccwld_isec *is = &o->secs[k];
              if (!is->placed || is->out_sec != (int)i || !is->data)
                continue;
              if (is->out_off + is->size > sec->size)
                continue;
              memcpy (buf + is->out_off, is->data, is->size);
            }
        }
      /* output flags: or of member flags (exec/write), alloc implied */
      uint64_t fl = SHF_ALLOC;
      for (size_t j = 0; j < st->nobjs; j++)
        for (size_t k = 0; k < st->objs[j].nsecs; k++)
          {
            ccwld_isec *is = &st->objs[j].secs[k];
            if (is->placed && is->out_sec == (int)i)
              fl |= is->flags & (SHF_WRITE | SHF_EXECINSTR);
          }
      content[oi] = buf;
      out_flags[oi] = fl;
      out_sizes[oi] = sec->size;
      oi++;
    }

  /* ---- producer note ---- */
  char notebuf[2048];
  size_t notelen = ccwld_emit_note_text (st, notebuf, sizeof (notebuf));
  if (notelen >= sizeof (notebuf)) /* truncated: the note never overreads */
    notelen = sizeof (notebuf) - 1;
  size_t notetotal = 12 + 4 + ((notelen + 3) & ~(size_t)3);

  /* ---- symbol table ---- */
  strtab_t strtab, shstrtab;
  if (!strtab_init (&strtab) || !strtab_init (&shstrtab))
    {
      strtab_free (&strtab);
      strtab_free (&shstrtab);
      for (size_t j = 0; j < nout; j++)
        free (content[j]);
      free (content);
      free (out_flags);
      free (out_sizes);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }

  size_t nsym = 1; /* null symbol */
  for (size_t i = 0; i < st->nrsyms; i++)
    nsym++;
  W_Sym *syms = calloc (nsym, sizeof (*syms));
  if (!syms)
    {
      strtab_free (&strtab);
      strtab_free (&shstrtab);
      for (size_t j = 0; j < nout; j++)
        free (content[j]);
      free (content);
      free (out_flags);
      free (out_sizes);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }

  size_t sym_i = 1;
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      ccwld_rsym *r = &st->rsyms[i];
      if (!r->referenced && !r->defined)
        continue;
      syms[sym_i].st_name = strtab_add (&strtab, r->name);
      int bind = r->weak ? STB_WEAK : STB_GLOBAL;
      int type = STT_NOTYPE;
      uint16_t shndx = SHN_UNDEF;
      if (r->defined)
        {
          if (r->from_script || r->obj < 0 || r->isym < 0)
            {
              shndx = SHN_ABS;
            }
          else
            {
              ccwld_obj *o = &st->objs[r->obj];
              ccwld_isym *s
                  = ((size_t)r->isym < o->nsyms) ? &o->syms[r->isym] : NULL;
              if (s)
                {
                  ccwld_isec *is = ccwld_state_isec (st, r->obj, s->shndx);
                  if (is && is->out_sec >= 0)
                    {
                      int em = sec_emit_idx[is->out_sec];
                      shndx = em >= 0 ? (uint16_t)(em + 1) : SHN_ABS;
                      if (st->plan->secs[is->out_sec].size == 0)
                        shndx = SHN_ABS;
                      if (is->flags & SHF_EXECINSTR)
                        type = STT_FUNC;
                      else
                        type = STT_OBJECT;
                    }
                  else
                    shndx = SHN_ABS;
                }
              else
                shndx = SHN_ABS;
            }
        }
      syms[sym_i].st_info = (unsigned char)((bind << 4) | type);
      syms[sym_i].st_other = 0;
      syms[sym_i].st_shndx = shndx;
      syms[sym_i].st_value = r->value_known ? r->value : 0;
      syms[sym_i].st_size = r->size;
      sym_i++;
    }
  nsym = sym_i; /* unreferenced+undefined were skipped */

  /* ---- file layout ---- */
  size_t phnum = 0;
  if (!is_reloc)
    phnum = p->nphdrs ? p->nphdrs : 1;

  size_t off = sizeof (W_Ehdr) + phnum * sizeof (W_Phdr);
  off = (off + 15) & ~(size_t)15;

  size_t *sec_off = calloc (nout ? nout : 1, sizeof (*sec_off));
  if (!sec_off)
    {
      free (sec_emit_idx);
      free (syms);
      strtab_free (&strtab);
      strtab_free (&shstrtab);
      for (size_t j = 0; j < nout; j++)
        free (content[j]);
      free (content);
      free (out_flags);
      free (out_sizes);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  {
    size_t oi2 = 0;
    for (size_t i = 0; i < p->nsecs; i++)
      {
        if (!(p->secs[i].size > 0 || p->secs[i].load))
          continue;
        off = (off + 15) & ~(size_t)15;
        sec_off[oi2] = off;
        if (p->secs[i].load) /* NOBITS (load==0) occupies no file space */
          off += out_sizes[oi2];
        oi2++;
      }
  }

  off = (off + 3) & ~(size_t)3;
  size_t note_off = off;
  off += notetotal;

  off = (off + 7) & ~(size_t)7;
  size_t symtab_off = off;
  off += nsym * sizeof (W_Sym);

  size_t strtab_off = off;
  off += strtab.len;

  size_t shstrtab_off = off;
  uint32_t shstr_names[8];
  const char *meta_names[] = { "",      ".note.ccw", ".symtab",
                               ".strtab", ".shstrtab" };
  for (int i = 0; i < 5; i++)
    shstr_names[i] = strtab_add (&shstrtab, meta_names[i]);
  /* output section names */
  uint32_t *out_names = malloc ((nout ? nout : 1) * sizeof (*out_names));
  if (!out_names)
    {
      free (sec_off);
      free (sec_emit_idx);
      free (syms);
      strtab_free (&strtab);
      strtab_free (&shstrtab);
      for (size_t j = 0; j < nout; j++)
        free (content[j]);
      free (content);
      free (out_flags);
      free (out_sizes);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  {
    size_t idx = 0;
    for (size_t i = 0; i < p->nsecs; i++)
      if (p->secs[i].size > 0 || p->secs[i].load)
        out_names[idx++] = strtab_add (&shstrtab, p->secs[i].name);
  }
  off += shstrtab.len;

  off = (off + 7) & ~(size_t)7;
  size_t shoff = off;
  size_t shnum = 1 + nout + 4; /* NULL + outs + note + symtab + strtab + shstrtab */
  size_t shstrndx = shnum - 1;
  size_t symtab_idx = 1 + nout + 1;
  size_t total = shoff + shnum * sizeof (W_Shdr);

  /* ---- serialize ---- */
  unsigned char *img = calloc (total ? total : 1, 1);
  if (!img)
    {
      free (out_names);
      free (sec_off);
      free (sec_emit_idx);
      free (syms);
      strtab_free (&strtab);
      strtab_free (&shstrtab);
      for (size_t j = 0; j < nout; j++)
        free (content[j]);
      free (content);
      free (out_flags);
      free (out_sizes);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }

  W_Ehdr eh;
  memset (&eh, 0, sizeof (eh));
  eh.e_ident[0] = 0x7f;
  eh.e_ident[1] = 'E';
  eh.e_ident[2] = 'L';
  eh.e_ident[3] = 'F';
  eh.e_ident[4] = 2; /* ELFCLASS64 */
  eh.e_ident[5] = 1; /* ELFDATA2LSB */
  eh.e_ident[6] = 1; /* EV_CURRENT */
  eh.e_type = is_reloc ? ET_REL : is_dsoish ? ET_DYN : ET_EXEC;
  eh.e_machine = (uint16_t)(st->machine ? st->machine : EM_X86_64_D);
  eh.e_version = 1;
  eh.e_entry = st->entry_value;
  eh.e_phoff = phnum ? sizeof (W_Ehdr) : 0;
  eh.e_shoff = shoff;
  eh.e_ehsize = sizeof (W_Ehdr);
  eh.e_phentsize = (uint16_t)(phnum ? sizeof (W_Phdr) : 0);
  eh.e_phnum = (uint16_t)phnum;
  eh.e_shentsize = (uint16_t)sizeof (W_Shdr);
  eh.e_shnum = (uint16_t)shnum;
  eh.e_shstrndx = (uint16_t)shstrndx;

  unsigned char *q = img;
  memcpy (q, eh.e_ident, 16);
  w_u16 (q + 16, eh.e_type);
  w_u16 (q + 18, eh.e_machine);
  w_u32 (q + 20, eh.e_version);
  w_u64 (q + 24, eh.e_entry);
  w_u64 (q + 32, eh.e_phoff);
  w_u64 (q + 40, eh.e_shoff);
  w_u32 (q + 48, eh.e_flags);
  w_u16 (q + 52, eh.e_ehsize);
  w_u16 (q + 54, eh.e_phentsize);
  w_u16 (q + 56, eh.e_phnum);
  w_u16 (q + 58, eh.e_shentsize);
  w_u16 (q + 60, eh.e_shnum);
  w_u16 (q + 62, eh.e_shstrndx);

  /* program headers (exe/dso/pie) */
  if (phnum)
    {
      for (size_t i = 0; i < phnum; i++)
        {
          W_Phdr ph;
          memset (&ph, 0, sizeof (ph));
          uint64_t minv = (uint64_t)-1, maxe = 0, maxe_file = 0;
          size_t first_off = 0;
          int any = 0;
          const char *pname = p->nphdrs ? p->phdrs[i].name : NULL;
          uint32_t pflags = 0;
          for (size_t s = 0; s < p->nsecs; s++)
            {
              if (!(p->secs[s].size > 0 || p->secs[s].load))
                continue;
              if (p->nphdrs)
                {
                  if (!p->secs[s].phdr || !pname
                      || strcmp (p->secs[s].phdr, pname))
                    continue;
                }
              if (p->secs[s].vma < minv)
                {
                  minv = p->secs[s].vma;
                  first_off = sec_off[sec_emit_idx[s]];
                }
              if (p->secs[s].vma + p->secs[s].size > maxe)
                maxe = p->secs[s].vma + p->secs[s].size;
              if (p->secs[s].load
                  && p->secs[s].vma + p->secs[s].size > maxe_file)
                maxe_file = p->secs[s].vma + p->secs[s].size;
              any = 1;
            }
          ph.p_type = phdr_type (p->nphdrs ? p->phdrs[i].type : "LOAD");
          pflags = p->nphdrs ? p->phdrs[i].flags : (PF_R | PF_W | PF_X);
          ph.p_flags = pflags;
          if (any)
            {
              if (i == 0 && phdr_type (p->nphdrs ? p->phdrs[i].type
                                                   : "LOAD")
                              == PT_LOAD)
                {
                  ph.p_offset = 0;
                  ph.p_vaddr = minv - first_off;
                }
              else
                {
                  ph.p_offset = first_off;
                  ph.p_vaddr = minv;
                }
              ph.p_paddr = ph.p_vaddr;
              ph.p_filesz = maxe_file > ph.p_vaddr ? maxe_file - ph.p_vaddr
                                                   : 0;
              ph.p_memsz = maxe > ph.p_vaddr ? maxe - ph.p_vaddr : 0;
            }
          ph.p_align = largest_congruent_alignment (
              ph.p_offset, ph.p_vaddr, p->nphdrs ? p->phdrs[i].align : 0x1000);

          unsigned char *pp = img + sizeof (W_Ehdr)
                              + i * sizeof (W_Phdr);
          w_u32 (pp, ph.p_type);
          w_u32 (pp + 4, ph.p_flags);
          w_u64 (pp + 8, ph.p_offset);
          w_u64 (pp + 16, ph.p_vaddr);
          w_u64 (pp + 24, ph.p_paddr);
          w_u64 (pp + 32, ph.p_filesz);
          w_u64 (pp + 40, ph.p_memsz);
          w_u64 (pp + 48, ph.p_align);
        }
    }

  /* section contents (NOBITS occupies no file space) */
  for (size_t i = 0; i < nout; i++)
    if (content[i] && out_sizes[i] && p->secs[sec_of_emit (p, i)].load)
      memcpy (img + sec_off[i], content[i], out_sizes[i]);

  /* note */
  {
    unsigned char *np = img + note_off;
    w_u32 (np, 4); /* namesz */
    w_u32 (np + 4, (uint32_t)notelen);
    w_u32 (np + 8, 1); /* type: producer note */
    memcpy (np + 12, "CCW", 4);
    memcpy (np + 16, notebuf, notelen);
  }

  /* symtab */
  for (size_t i = 0; i < nsym; i++)
    {
      unsigned char *sp = img + symtab_off + i * sizeof (W_Sym);
      w_u32 (sp, syms[i].st_name);
      sp[4] = syms[i].st_info;
      sp[5] = syms[i].st_other;
      w_u16 (sp + 6, syms[i].st_shndx);
      w_u64 (sp + 8, syms[i].st_value);
      w_u64 (sp + 16, syms[i].st_size);
    }

  memcpy (img + strtab_off, strtab.buf, strtab.len);
  memcpy (img + shstrtab_off, shstrtab.buf, shstrtab.len);

  /* section headers */
  W_Shdr *sh = (W_Shdr *)(img + shoff);
  memset (sh, 0, shnum * sizeof (W_Shdr));
  for (size_t i = 0; i < nout; i++)
    {
      W_Shdr *s = &sh[1 + i];
      s->sh_name = out_names[i];
      s->sh_type = SHT_PROGBITS;
      s->sh_flags = out_flags[i];
      s->sh_addr = 0; /* filled below from the plan */
      s->sh_offset = (uint64_t)sec_off[i];
      s->sh_size = out_sizes[i];
      s->sh_addralign = 16;
    }
  {
    size_t idx = 0;
    for (size_t i = 0; i < p->nsecs; i++)
      {
        if (!(p->secs[i].size > 0 || p->secs[i].load))
          continue;
        sh[1 + idx].sh_addr = p->secs[i].vma;
        if (!p->secs[i].load)
          {
            sh[1 + idx].sh_type = SHT_NOBITS;
            sh[1 + idx].sh_offset = (uint64_t)sec_off[idx];
            sh[1 + idx].sh_size = p->secs[i].size;
          }
        idx++;
      }
  }
  {
    W_Shdr *s = &sh[1 + nout];
    s->sh_name = shstr_names[1];
    s->sh_type = SHT_NOTE;
    s->sh_offset = (uint64_t)note_off;
    s->sh_size = (uint64_t)notetotal;
    s->sh_addralign = 4;
  }
  {
    W_Shdr *s = &sh[symtab_idx];
    s->sh_name = shstr_names[2];
    s->sh_type = SHT_SYMTAB;
    s->sh_offset = (uint64_t)symtab_off;
    s->sh_size = nsym * sizeof (W_Sym);
    s->sh_link = (uint32_t)(symtab_idx + 1);
    s->sh_info = 1; /* first global index */
    s->sh_addralign = 8;
    s->sh_entsize = sizeof (W_Sym);
  }
  {
    W_Shdr *s = &sh[symtab_idx + 1];
    s->sh_name = shstr_names[3];
    s->sh_type = SHT_STRTAB;
    s->sh_offset = (uint64_t)strtab_off;
    s->sh_size = strtab.len;
    s->sh_addralign = 1;
  }
  {
    W_Shdr *s = &sh[shstrndx];
    s->sh_name = shstr_names[4];
    s->sh_type = SHT_STRTAB;
    s->sh_offset = (uint64_t)shstrtab_off;
    s->sh_size = shstrtab.len;
    s->sh_addralign = 1;
  }

  FILE *f = fopen (path, "wb");
  if (!f)
    {
      free (img);
      ccwld_error_set (e, CCWLD_EXIT_LINK, "cannot write output '%s'", path);
      goto fail;
    }
  int wok = (fwrite (img, 1, total, f) == total);
  fclose (f);
  free (img);
  if (!wok)
    {
      ccwld_error_set (e, CCWLD_EXIT_LINK, "short write on output '%s'",
                       path);
      goto fail;
    }
  if (!is_reloc && chmod (path, 0755) != 0)
    {
      ccwld_error_set (e, CCWLD_EXIT_LINK,
                       "cannot make executable output '%s'", path);
      goto fail;
    }

  free (out_names);
  free (sec_off);
  free (sec_emit_idx);
  free (syms);
  strtab_free (&strtab);
  strtab_free (&shstrtab);
  for (size_t j = 0; j < nout; j++)
    free (content[j]);
  free (content);
  free (out_flags);
  free (out_sizes);
  return 1;

fail:
  free (out_names);
  free (sec_off);
  free (sec_emit_idx);
  free (syms);
  strtab_free (&strtab);
  strtab_free (&shstrtab);
  for (size_t j = 0; j < nout; j++)
    free (content[j]);
  free (content);
  free (out_flags);
  free (out_sizes);
  return 0;
}

/* --- dispatch (§6) --- */

int
ccwld_emit_object (ccwld_state *st, const char *path, ccwld_error *e)
{
  ccwld_plan *p = st->plan;
  const char *fmt = p->output.format ? p->output.format : "elf";
  if (!strcmp (fmt, "wasm"))
    return ccwld_emit_binaryen (path, p->output.entry, e);
  /* elf (pe/macho are rejected at seal-time by the format table until
   * those emitters exist: never silently mis-emitted, D-0043) */
  if (!strcmp (fmt, "pe") || !strcmp (fmt, "macho"))
    {
      ccwld_error_set (e, CCWLD_EXIT_USAGE,
                       "output format '%s' requires a matching emitter; "
                       "not silently emitted as another format",
                       fmt);
      return 0;
    }
  return emit_elf64 (st, path, e);
}
