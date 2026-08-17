/* §9: ELF64 relocatable object writer — conforming to Glue ABI v1 */
#include "ccw_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ELF64 definitions */
#define EI_NIDENT 16

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

typedef struct {
  unsigned char e_ident[EI_NIDENT];
  Elf64_Half    e_type;
  Elf64_Half    e_machine;
  Elf64_Word    e_version;
  Elf64_Addr    e_entry;
  Elf64_Off     e_phoff;
  Elf64_Off     e_shoff;
  Elf64_Word    e_flags;
  Elf64_Half    e_ehsize;
  Elf64_Half    e_phentsize;
  Elf64_Half    e_phnum;
  Elf64_Half    e_shentsize;
  Elf64_Half    e_shnum;
  Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  Elf64_Word    sh_name;
  Elf64_Word    sh_type;
  Elf64_Xword   sh_flags;
  Elf64_Addr    sh_addr;
  Elf64_Off     sh_offset;
  Elf64_Xword   sh_size;
  Elf64_Word    sh_link;
  Elf64_Word    sh_info;
  Elf64_Xword   sh_addralign;
  Elf64_Xword   sh_entsize;
} Elf64_Shdr;

typedef struct {
  Elf64_Word    st_name;
  unsigned char st_info;
  unsigned char st_other;
  Elf64_Half    st_shndx;
  Elf64_Addr    st_value;
  Elf64_Xword   st_size;
} Elf64_Sym;

typedef struct {
  Elf64_Addr    r_offset;
  Elf64_Xword   r_info;
  Elf64_Sxword  r_addend;
} Elf64_Rela;

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_REL 1
#define EM_X86_64 62
#define EM_AARCH64 183
#define EM_RISCV 243
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_NOTE 7
#define SHF_ALLOC 2
#define SHF_EXECINSTR 4
#define SHF_WRITE 1
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_FUNC 2
#define STT_OBJECT 1
#define ELF64_R_INFO(s,t) (((Elf64_Xword)(s)<<32) | ((Elf64_Xword)(t)&0xffffffff))
#define ELF64_R_SYM(i) ((i)>>32)
#define ELF64_R_TYPE(i) ((i)&0xffffffff)

/* Relocation types */
#define R_X86_64_64      1
#define R_X86_64_PC32    2
#define R_X86_64_32      10
#define R_X86_64_32S     11
#define R_X86_64_PC16    13
#define R_X86_64_PC8     14
#define R_AARCH64_ABS64  257
#define R_AARCH64_CALL26 283
#define R_AARCH64_ADR_PREL_PG_HI21 275
#define R_AARCH64_ADD_ABS_LO12_NC  277
#define R_RISCV_64        2
#define R_RISCV_PCREL_HI20 23
#define R_RISCV_PCREL_LO12_I 24
#define R_RISCV_PCREL_LO12_S 25

static uint32_t reloc_to_elf_type(ccw_reloc_type_t t, ccw_arch_t arch) {
  switch (arch) {
    case CCW_ARCH_X86_64:
      switch (t) {
        case CCW_RELOC_ABS64: return R_X86_64_64;
        case CCW_RELOC_PC32:
        case CCW_RELOC_REL32: return R_X86_64_PC32;
        case CCW_RELOC_ABS32: return R_X86_64_32;
        case CCW_RELOC_REL16: return R_X86_64_PC16;
        case CCW_RELOC_REL8:  return R_X86_64_PC8;
        default: return R_X86_64_64;
      }
    case CCW_ARCH_AARCH64:
      switch (t) {
        case CCW_RELOC_ABS64: return R_AARCH64_ABS64;
        case CCW_RELOC_CALL:  return R_AARCH64_CALL26;
        case CCW_RELOC_PAGE:  return R_AARCH64_ADR_PREL_PG_HI21;
        case CCW_RELOC_PAGEOFF: return R_AARCH64_ADD_ABS_LO12_NC;
        default: return R_AARCH64_ABS64;
      }
    case CCW_ARCH_RISCV64:
      switch (t) {
        case CCW_RELOC_ABS64: return R_RISCV_64;
        case CCW_RELOC_HI20:  return R_RISCV_PCREL_HI20;
        case CCW_RELOC_LO12_I: return R_RISCV_PCREL_LO12_I;
        case CCW_RELOC_LO12_S: return R_RISCV_PCREL_LO12_S;
        default: return R_RISCV_64;
      }
    default: return 0;
  }
}

static Elf64_Half arch_to_emachine(ccw_arch_t arch) {
  switch (arch) {
    case CCW_ARCH_X86_64: return EM_X86_64;
    case CCW_ARCH_AARCH64: return EM_AARCH64;
    case CCW_ARCH_RISCV64: return EM_RISCV;
    default: return EM_X86_64;
  }
}

/* Build a string table from a list of strings */
static size_t build_strtab(char *buf, size_t cap, const char **strings, size_t n,
                           size_t *offsets) {
  size_t off = 1; /* first byte is NUL */
  if (cap > 0) buf[0] = '\0';
  for (size_t i = 0; i < n; i++) {
    if (strings[i][0] == '\0') {
      offsets[i] = 0;
      continue;
    }
    offsets[i] = off;
    size_t len = strlen(strings[i]) + 1;
    if (off + len <= cap) memcpy(buf + off, strings[i], len);
    off += len;
  }
  return off;
}

/* Write ELF64 object */
static int write_elf64(const ccw_unit_t *u, const char *path, char **error) {
  (void)error;

  /* Gather symbols */
  khiter_t k;
  size_t nsym = 0;
  for (k = kh_begin(u->symtab); k != kh_end(u->symtab); ++k) {
    if (kh_exist(u->symtab, k)) nsym++;
  }

  /* Build string tables */
  /* Section header string table */
  size_t shstr_offsets[16];
  const char *shstr_names[] = {
    "", ".text", ".data", ".bss", ".symtab", ".strtab",
    ".shstrtab", ".note.ccw", ".rela.text", ".rela.data"
  };
  size_t nshstr = sizeof(shstr_names) / sizeof(shstr_names[0]);
  char shstrtab[512];
  size_t shstr_len = build_strtab(shstrtab, sizeof(shstrtab), shstr_names, nshstr, shstr_offsets);

  /* Symbol string table */
  size_t *sym_offsets = (size_t *)calloc(nsym + 1, sizeof(size_t));
  const char **sym_names = (const char **)calloc(nsym + 1, sizeof(const char *));
  sym_names[0] = "";
  size_t si = 1;
  for (k = kh_begin(u->symtab); k != kh_end(u->symtab); ++k) {
    if (kh_exist(u->symtab, k)) {
      sym_names[si] = kh_key(u->symtab, k);
      si++;
    }
  }
  size_t strtab_cap = 1;
  for (size_t i = 1; i <= nsym; i++) strtab_cap += strlen(sym_names[i]) + 1;
  char *strtab = (char *)calloc(strtab_cap, 1);
  size_t strtab_len = build_strtab(strtab, strtab_cap, sym_names, nsym + 1, sym_offsets);

  /* Count sections */
  size_t nsec = kv_size(u->sections);
  /* Each section with relocs gets a rela section */
  size_t nrela = 0;
  for (size_t i = 0; i < kv_size(u->relocs); i++) {
    ccw_reloc_t *r = &kv_A(u->relocs, i);
    if (r->section >= 0 && (size_t)r->section < nsec) nrela = (size_t)(r->section + 1);
  }
  /* If we have relocs but no section, use 0 */
  if (kv_size(u->relocs) > 0 && nrela == 0) nrela = 1;

  size_t shnum = 1 + nsec + nrela + 4; /* NULL + sections + rela + symtab + strtab + shstrtab + note */
  size_t off = sizeof(Elf64_Ehdr);

  /* Calculate section offsets */
  size_t *sec_offsets = (size_t *)calloc(nsec, sizeof(size_t));
  size_t *sec_sizes = (size_t *)calloc(nsec, sizeof(size_t));
  for (size_t i = 0; i < nsec; i++) {
    sec_offsets[i] = off;
    sec_sizes[i] = kv_A(u->sections, i).len;
    off = (off + kv_A(u->sections, i).len + 15) & ~15;
  }

  /* Rela sections */
  size_t *rela_offsets = (size_t *)calloc(nrela, sizeof(size_t));
  size_t *rela_counts = (size_t *)calloc(nrela, sizeof(size_t));
  for (size_t i = 0; i < kv_size(u->relocs); i++) {
    ccw_reloc_t *r = &kv_A(u->relocs, i);
    size_t sidx = (r->section >= 0 && (size_t)r->section < nrela) ? (size_t)r->section : 0;
    rela_counts[sidx]++;
  }
  for (size_t i = 0; i < nrela; i++) {
    rela_offsets[i] = off;
    off += rela_counts[i] * sizeof(Elf64_Rela);
    off = (off + 7) & ~7;
  }

  size_t symoff = off;
  size_t symn = nsym + 1; /* +1 for NULL symbol */
  off += symn * sizeof(Elf64_Sym);
  size_t stroff = off;
  off += strtab_len;
  size_t shstroff = off;
  off += shstr_len;
  off = (off + 7) & ~7;

  /* Note section */
  size_t noteoff = off;
  off += 32;
  off = (off + 7) & ~7;

  size_t shoff = off;
  size_t total = shoff + shnum * sizeof(Elf64_Shdr);

  /* Allocate and write */
  unsigned char *buf = (unsigned char *)calloc(1, total);
  if (!buf) {
    free(sec_offsets); free(sec_sizes); free(rela_offsets); free(rela_counts);
    free(sym_offsets); free(sym_names); free(strtab);
    if (error) *error = strdup("out of memory");
    return 0;
  }

  /* ELF header */
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
  ehdr->e_ident[0] = ELFMAG0; ehdr->e_ident[1] = ELFMAG1;
  ehdr->e_ident[2] = ELFMAG2; ehdr->e_ident[3] = ELFMAG3;
  ehdr->e_ident[4] = ELFCLASS64; ehdr->e_ident[5] = ELFDATA2LSB;
  ehdr->e_ident[6] = EV_CURRENT;
  ehdr->e_type = ET_REL;
  ehdr->e_machine = arch_to_emachine(u->arch);
  ehdr->e_version = EV_CURRENT;
  ehdr->e_ehsize = sizeof(Elf64_Ehdr);
  ehdr->e_shoff = shoff;
  ehdr->e_shentsize = sizeof(Elf64_Shdr);
  ehdr->e_shnum = (Elf64_Half)shnum;
  ehdr->e_shstrndx = (Elf64_Half)(1 + nsec + nrela + 2);

  /* Copy section data */
  for (size_t i = 0; i < nsec; i++) {
    if (kv_A(u->sections, i).data && kv_A(u->sections, i).len > 0) {
      memcpy(buf + sec_offsets[i], kv_A(u->sections, i).data, kv_A(u->sections, i).len);
    }
  }

  /* Symbol table */
  Elf64_Sym *syms = (Elf64_Sym *)(buf + symoff);
  syms[0].st_name = 0; syms[0].st_info = 0; syms[0].st_shndx = 0;
  syms[0].st_value = 0; syms[0].st_size = 0;
  si = 1;
  for (k = kh_begin(u->symtab); k != kh_end(u->symtab); ++k) {
    if (kh_exist(u->symtab, k)) {
      ccw_symbol_t *sym = &kh_value(u->symtab, k);
      syms[si].st_name = (Elf64_Word)sym_offsets[si];
      syms[si].st_info = (unsigned char)((sym->binding << 4) | (sym->section < 0 ? STT_NOTYPE : STT_FUNC));
      syms[si].st_other = 0;
      syms[si].st_shndx = (Elf64_Half)(sym->section >= 0 ? sym->section + 1 : 0);
      syms[si].st_value = sym->value;
      syms[si].st_size = sym->size;
      si++;
    }
  }

  /* Relocations */
  for (size_t i = 0; i < nrela; i++) {
    Elf64_Rela *rela = (Elf64_Rela *)(buf + rela_offsets[i]);
    size_t ri = 0;
    for (size_t j = 0; j < kv_size(u->relocs); j++) {
      ccw_reloc_t *r = &kv_A(u->relocs, j);
      size_t sidx = (r->section >= 0 && (size_t)r->section < nrela) ? (size_t)r->section : 0;
      if (sidx != i) continue;
      rela[ri].r_offset = r->offset;
      /* Find symbol index */
      Elf64_Xword sym_idx = 0;
      if (r->symbol) {
        for (size_t s = 1; s <= nsym; s++) {
          if (!strcmp(sym_names[s], r->symbol)) { sym_idx = s; break; }
        }
      }
      uint32_t rtype = reloc_to_elf_type(r->type, u->arch);
      rela[ri].r_info = ELF64_R_INFO(sym_idx, rtype);
      rela[ri].r_addend = r->addend;
      ri++;
    }
  }

  /* STRING TABLES */
  memcpy(buf + stroff, strtab, strtab_len);
  memcpy(buf + shstroff, shstrtab, shstr_len);

  /* NOTE: .note.ccw */
  {
    uint32_t *note = (uint32_t *)(buf + noteoff);
    note[0] = 4; /* namesz */
    note[1] = 8; /* descsz */
    note[2] = 1; /* type: version */
    memcpy(buf + noteoff + 12, "CCW\0", 4);
    memcpy(buf + noteoff + 16, "ccwas\0\0\0", 8);
  }

  /* Section headers */
  Elf64_Shdr *shdr = (Elf64_Shdr *)(buf + shoff);
  /* NULL section */
  shdr[0].sh_name = 0;
  shdr[0].sh_type = SHT_NULL;

  /* Content sections */
  for (size_t i = 0; i < nsec; i++) {
    ccw_section_t *sec = &kv_A(u->sections, i);
    shdr[1 + i].sh_name = (Elf64_Word)shstr_offsets[1 + (i < 3 ? i : 3)];
    shdr[1 + i].sh_type = (sec->type == 8) ? SHT_NOBITS : SHT_PROGBITS;
    shdr[1 + i].sh_flags = sec->flags;
    shdr[1 + i].sh_offset = sec_offsets[i];
    shdr[1 + i].sh_size = sec_sizes[i];
    shdr[1 + i].sh_addralign = sec->align;
    shdr[1 + i].sh_entsize = 0;
  }

  /* Rela sections */
  for (size_t i = 0; i < nrela; i++) {
    size_t hi = 1 + nsec + i;
    shdr[hi].sh_name = (Elf64_Word)shstr_offsets[8 + (i == 0 ? 0 : 1)];
    shdr[hi].sh_type = SHT_RELA;
    shdr[hi].sh_flags = 0;
    shdr[hi].sh_offset = rela_offsets[i];
    shdr[hi].sh_size = rela_counts[i] * sizeof(Elf64_Rela);
    shdr[hi].sh_link = (Elf64_Word)(1 + nsec + nrela); /* symtab */
    shdr[hi].sh_info = (Elf64_Word)(1 + i); /* target section */
    shdr[hi].sh_addralign = 8;
    shdr[hi].sh_entsize = sizeof(Elf64_Rela);
  }

  /* Symtab, strtab, shstrtab, note */
  size_t symtab_idx = 1 + nsec + nrela;
  shdr[symtab_idx].sh_name = (Elf64_Word)shstr_offsets[4];
  shdr[symtab_idx].sh_type = SHT_SYMTAB;
  shdr[symtab_idx].sh_offset = symoff;
  shdr[symtab_idx].sh_size = symn * sizeof(Elf64_Sym);
  shdr[symtab_idx].sh_link = (Elf64_Word)(symtab_idx + 1);
  shdr[symtab_idx].sh_info = (Elf64_Word)1;
  shdr[symtab_idx].sh_addralign = 8;
  shdr[symtab_idx].sh_entsize = sizeof(Elf64_Sym);

  shdr[symtab_idx + 1].sh_name = (Elf64_Word)shstr_offsets[5];
  shdr[symtab_idx + 1].sh_type = SHT_STRTAB;
  shdr[symtab_idx + 1].sh_offset = stroff;
  shdr[symtab_idx + 1].sh_size = strtab_len;

  shdr[symtab_idx + 2].sh_name = (Elf64_Word)shstr_offsets[6];
  shdr[symtab_idx + 2].sh_type = SHT_STRTAB;
  shdr[symtab_idx + 2].sh_offset = shstroff;
  shdr[symtab_idx + 2].sh_size = shstr_len;

  shdr[symtab_idx + 3].sh_name = (Elf64_Word)shstr_offsets[7];
  shdr[symtab_idx + 3].sh_type = SHT_NOTE;
  shdr[symtab_idx + 3].sh_offset = noteoff;
  shdr[symtab_idx + 3].sh_size = 32;

  /* Write file */
  FILE *f = fopen(path, "wb");
  if (!f) {
    free(buf);
    free(sec_offsets); free(sec_sizes); free(rela_offsets); free(rela_counts);
    free(sym_offsets); free(sym_names); free(strtab);
    if (error) *error = strdup("cannot open output file");
    return 0;
  }
  fwrite(buf, 1, total, f);
  fclose(f);

  free(buf);
  free(sec_offsets); free(sec_sizes); free(rela_offsets); free(rela_counts);
  free(sym_offsets); free(sym_names); free(strtab);
  return 1;
}

/* PE/COFF writer (stub for now) */
static int write_pecoff(const ccw_unit_t *u, const char *path, char **error) {
  (void)u;
  (void)path;
  if (error) *error = strdup("PE/COFF output not yet implemented");
  return 0;
}

/* Mach-O writer (stub for now) */
static int write_macho(const ccw_unit_t *u, const char *path, char **error) {
  (void)u;
  (void)path;
  if (error) *error = strdup("Mach-O output not yet implemented");
  return 0;
}

/* Main dispatch */
int ccw_obj_write(const ccw_unit_t *u, const char *path, ccw_obj_format_t fmt,
                  char **error) {
  switch (fmt) {
    case CCW_FMT_ELF:  return write_elf64(u, path, error);
    case CCW_FMT_PE:   return write_pecoff(u, path, error);
    case CCW_FMT_MACHO: return write_macho(u, path, error);
    default:
      if (error) *error = strdup("unsupported output format");
      return 0;
  }
}

ccw_obj_format_t ccw_obj_default_format(void) {
#if defined(__APPLE__)
  return CCW_FMT_MACHO;
#elif defined(_WIN32)
  return CCW_FMT_PE;
#else
  return CCW_FMT_ELF;
#endif
}
