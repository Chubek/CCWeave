/* §3: the phase pipeline's shared state.
 *
 * load ▸ [merge inputs] ▸ RESOLVE ▸ [LTO recompile] ▸ GC ▸ LAYOUT ▸
 * [relocate] ▸ EMIT.  Each phase mutates this state; the sealed plan is
 * read-only apart from the layout-output fields on its section/symbol
 * records and hook-mediated mutations through ccwld_link.
 *
 * Everything here iterates in deterministic order: objects in link
 * order, sections/symbols in first-occurrence order (§7). */
#ifndef CCWLD_PHASES_H
#define CCWLD_PHASES_H

#include "../ccwld.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* --- input section (one per ELF/loaded member) --- */
  typedef struct ccwld_isec
  {
    char *name;           /* input section name                      */
    unsigned char *data;  /* writable copy of the contents           */
    size_t size;
    uint64_t align;
    uint64_t flags;       /* SHF_*                                   */
    uint32_t type;        /* SHT_*                                   */
    int obj;              /* owning object index                     */
    int shndx;            /* section index within the object         */
    /* --- pipeline state --- */
    int live;             /* survived GC                              */
    int placed;           /* matched by an output-section selector    */
    int out_sec;          /* output section index or -1              */
    uint64_t out_off;     /* offset within the output section        */
    int is_root;          /* GC root (keep/entry/dynamic-referenced) */
  } ccwld_isec;

  /* --- input symbol --- */
  typedef struct ccwld_isym
  {
    char *name;
    int binding;      /* 0 local, 1 global, 2 weak               */
    int visibility;   /* STV_* numeric                           */
    int shndx;        /* defining section, 0 = undef, ABS = -1   */
    uint64_t value;
    uint64_t size;
    int obj;
  } ccwld_isym;

  /* --- input relocation --- */
  typedef struct ccwld_ireloc
  {
    int obj;
    int sec;          /* isec it applies to (obj-local shndx)    */
    uint64_t offset;
    uint32_t type;    /* target relocation type                  */
    char *sym;        /* symbol name (copied)                    */
    int64_t addend;
  } ccwld_ireloc;

  /* --- archive member --- */
  typedef struct ccwld_ar_member
  {
    char *name;
    unsigned char *data;
    size_t size;
    long hdr_off;     /* byte offset of the member header        */
    int extracted;
  } ccwld_ar_member;

  /* --- loaded input (object, archive, or DSO) --- */
  typedef struct ccwld_obj
  {
    char *path;
    char *kind;   /* "object" | "archive" | "dso" | "lto-module" */
    char *format; /* "elf64" | "archive"                          */
    int machine;  /* EM_* of the object (0 unknown)              */
    unsigned char *raw; /* owning copy of the input file bytes   */
    size_t raw_len;
    ccwld_isec *secs;
    size_t nsecs;
    ccwld_isym *syms;
    size_t nsyms;
    ccwld_ireloc *relocs;
    size_t nrelocs;
    /* archive support */
    ccwld_ar_member *members;
    size_t nmembers;
    char **ar_syms;     /* symbol names in the archive index     */
    int *ar_sym_member; /* parallel member indexes               */
    size_t nar_syms;
    /* flags */
    int used;       /* archive: member extracted; dso: referenced */
    int as_needed;
    int is_dso;
    int is_lto;     /* carries a .ccw.lto IR member              */
    int from_group; /* extracted or listed inside a GROUP        */
  } ccwld_obj;

  /* --- resolved symbol (first-occurrence ordered) --- */
  typedef struct ccwld_rsym
  {
    char *name;
    int defined;      /* 0 = still undefined                      */
    int obj;          /* defining object index or -1              */
    int isym;         /* index into objs[obj].syms, or -1         */
    int weak;
    int from_script;  /* assigned/provided by the plan            */
    int provided;     /* PROVIDE semantics                        */
    int referenced;   /* referenced by a reloc or another symbol  */
    int script_idx;   /* plan->syms index when from_script        */
    uint64_t value;
    int value_known;
    uint64_t size;
    char *binding;    /* "global"|"weak"|"local" (view string)    */
    char *visibility; /* view string                              */
  } ccwld_rsym;

  /* --- relocation stat bucket --- */
  typedef struct ccwld_stat
  {
    char name[48];
    size_t count;
  } ccwld_stat;

  /* --- note key/value added at the emit phase --- */
  typedef struct ccwld_note
  {
    char *key;
    char *value;
  } ccwld_note;

  /* --- mutation record (plugin/hook conflict authority, §5) --- */
  typedef struct ccwld_mut
  {
    char key[160];
    int src; /* CCWLD_MUT_PLUGIN or CCWLD_MUT_HOOK */
    int phase;
  } ccwld_mut;

  /* --- diagnostic (one stream, one shape, §9) --- */
  typedef struct ccwld_diag
  {
    int code;                  /* CCWLD_EXIT_* severity class       */
    int is_error;
    char message[448];
    char node[96];             /* plan node (e.g. section '.text')  */
    char site[96];             /* frontend site (file:line)         */
    char include_stack[192];   /* include/-T stack                  */
  } ccwld_diag;

  /* --- the pipeline state --- */
  typedef struct ccwld_state
  {
    ccwld_plan *plan;
    ccwld_obj *objs;
    size_t nobjs;
    size_t cobjs;
    ccwld_rsym *rsyms;
    size_t nrsyms;
    size_t crsyms;
    ccwld_stat *stats;
    size_t nstats;
    ccwld_note *notes;
    size_t nnotes;
    ccwld_mut *muts;
    size_t nmuts;
    ccwld_diag *diags;
    size_t ndiags;
    ccwld_diag *pending;   /* diagnostics raised during a hook/plugin callback */
    size_t npending;
    int in_callback;       /* a hook/plugin callback is currently running */
    uint64_t dot;
    uint64_t entry_value;
    int entry_known;
    int undefined_strong;  /* count of strong undefined symbols      */
    int machine;           /* EM_* consensus                          */
    int hook_depth;        /* lccwld §4.9 depth cap                   */
    int reloc_errors;
    /* LMA cursors for AT> regions (parallel to plan->mems order of use) */
    char **lma_regions;
    uint64_t *lma_dots;
    size_t nlma;
    char *lto_backend_used;  /* backend identity recorded for the note */
    unsigned lto_abi_used;
    int *sec_laid;           /* per output section: layout completed     */
  } ccwld_state;

  /* --- lifecycle --- */
  ccwld_state *ccwld_state_new (ccwld_plan *p);
  void ccwld_state_free (ccwld_state *st);

  /* --- diagnostics: one stream, one shape (§9) --- */
  void ccwld_diag_error (ccwld_state *st, int code, const char *node,
                         const char *site, const char *fmt, ...);
  void ccwld_diag_warn (ccwld_state *st, const char *node, const char *site,
                        const char *fmt, ...);
  void ccwld_diag_print (const ccwld_state *st, FILE *out);
  /* Flush diagnostics recorded while a hook/plugin callback ran. */
  void ccwld_diag_flush_pending (ccwld_state *st);

  /* --- phase runners (§3) --- */
  int ccwld_phase_load (ccwld_state *st, ccwld_error *e);
  int ccwld_phase_resolve (ccwld_state *st, ccwld_error *e);
  int ccwld_phase_lto (ccwld_state *st, ccwld_error *e);
  int ccwld_phase_gc (ccwld_state *st, ccwld_error *e);
  int ccwld_phase_layout (ccwld_state *st, ccwld_error *e);
  int ccwld_phase_relocate (ccwld_state *st, ccwld_error *e);

  /* --- helpers shared across phases --- */
  ccwld_rsym *ccwld_state_rsym (ccwld_state *st, const char *name);
  ccwld_isec *ccwld_state_isec (ccwld_state *st, int obj, int shndx);
  void ccwld_state_record_stat (ccwld_state *st, const char *name);
  int ccwld_state_record_mut (ccwld_state *st, const char *key, int src,
                              int phase);
  void ccwld_state_conflict_scan (ccwld_state *st, int phase);
  const char *ccwld_reloc_name (int machine, uint32_t type);
  /* file lookup honoring the plan's search paths (-l style optional) */
  char *ccwld_find_input (ccwld_state *st, const char *path);
  /* object construction (used by load and by archive/LTO ingestion) */
  ccwld_obj *ccwld_state_add_obj (ccwld_state *st, const char *path,
                                  const char *kind, const char *format);
  int ccwld_load_elf_mem (ccwld_state *st, const char *path,
                          const unsigned char *buf, size_t len,
                          ccwld_error *e);
  /* reset resolved-symbol state for a re-resolve after LTO (§3) */
  void ccwld_state_reset_resolution (ccwld_state *st);
  /* selector/glob matching (shared by gc and layout) */
  int ccwld_glob_match (const char *pat, const char *str);
  int ccwld_sec_matches (const ccwld_plan *p, const ccwld_sec *out,
                         const char *objpath, const char *secname,
                         int *is_keep);
  /* layout-progress query used by deferred ADDR/SIZEOF evaluation */
  int ccwld_state_sec_laid (const ccwld_plan *p, size_t index);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_PHASES_H */
