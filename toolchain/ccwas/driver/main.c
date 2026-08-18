/* §3: ccwas driver — CLI, phase sequencing, pipeline orchestration */
#include "lccwas.h"
#include "ccw_symtab.h"
#include "ccw_parse.h"
#include "ccw_obj.h"
#include "ketopt.h"
#include "kstring.h"
#include "kvec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* --- Options --- */
typedef struct {
  const char *target;
  const char *syntax;
  const char *input;
  const char *output;
  const char *keep_expanded;
  int         template;
  int         unsafe_lua;
  int         werror;
  char      **defs;
  size_t      ndefs;
  const char *format;
} ccw_opts_t;

static void die(const char *msg) {
  fprintf(stderr, "ccwas: %s\n", msg);
  exit(2);
}

static char *read_file(const char *path, char **error) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    if (error) {
      kstring_t message = {0, 0, NULL};
      if (ksprintf(&message, "%s: %s", path, strerror(errno)) < 0)
        return NULL;
      *error = ks_release(&message);
    }
    return NULL;
  }
  kstring_t source = {0, 0, NULL};
  char buffer[4096];
  while (!feof(f)) {
    size_t n = fread(buffer, 1, sizeof(buffer), f);
    if (n != 0 && kputsn(buffer, (int)n, &source) == EOF) {
      fclose(f);
      free(source.s);
      if (error) {
        kstring_t message = {0, 0, NULL};
        if (kputs("out of memory", &message) != EOF)
          *error = ks_release(&message);
      }
      return NULL;
    }
    if (ferror(f)) {
      fclose(f);
      free(source.s);
      if (error) {
        kstring_t message = {0, 0, NULL};
        if (ksprintf(&message, "%s: %s", path, strerror(errno)) >= 0)
          *error = ks_release(&message);
      }
      return NULL;
    }
  }
  fclose(f);
  return ks_release(&source);
}

static ccw_arch_t parse_target(const char *t) {
  if (!strcmp(t, "x86-64"))  return CCW_ARCH_X86_64;
  if (!strcmp(t, "aarch64")) return CCW_ARCH_AARCH64;
  if (!strcmp(t, "riscv64")) return CCW_ARCH_RISCV64;
  if (!strcmp(t, "wasm32"))  return CCW_ARCH_WASM32;
  die("unsupported target (use x86-64, aarch64, riscv64, or wasm32)");
  return CCW_ARCH_X86_64;
}

static ccw_obj_format_t parse_format(const char *f) {
  if (!f) return ccw_obj_default_format();
  if (!strcmp(f, "elf"))   return CCW_FMT_ELF;
  if (!strcmp(f, "pe"))    return CCW_FMT_PE;
  if (!strcmp(f, "macho")) return CCW_FMT_MACHO;
  die("unsupported format (use elf, pe, or macho)");
  return CCW_FMT_ELF;
}

int main(int argc, char *argv[]) {
  ccw_opts_t opts = {0};
  opts.syntax = "intel";

  /* Parse options using ketopt */
  ketopt_t s = KETOPT_INIT;
  ko_longopt_t longopts[] = {
    {"target",          ko_required_argument, 't'},
    {"syntax",          ko_required_argument, 's'},
    {"keep-expanded",   ko_required_argument, 'k'},
    {"template",        ko_no_argument,       'T'},
    {"unsafe-lua",      ko_no_argument,       'U'},
    {"format",          ko_required_argument, 'f'},
    {"help",            ko_no_argument,       'h'},
    {NULL, 0, 0}
  };

  int c;
  while ((c = ketopt(&s, argc, argv, 1, "D:o:W:I:f:", longopts)) != -1) {
    switch (c) {
      case 't': opts.target = s.arg; break;
      case 's': opts.syntax = s.arg; break;
      case 'k': opts.keep_expanded = s.arg; break;
      case 'T': opts.template = 1; break;
      case 'U': opts.unsafe_lua = 1; break;
      case 'D': {
        kvec_t(char *) defs = {opts.ndefs, opts.ndefs, opts.defs};
        if (kv_resize(char *, defs, defs.n + 1u) == NULL)
          die("out of memory");
        opts.defs = defs.a;
        opts.defs[opts.ndefs++] = s.arg;
        break;
      }
      case 'o': opts.output = s.arg; break;
      case 'W': opts.werror = (s.arg && !strcmp(s.arg, "error")) ? 1 : 0; break;
      case 'f': opts.format = s.arg; break;
      case 'h':
        printf("ccwas — CCWeave cross-assembler\n");
        printf("usage: ccwas --target=<arch> [options] input.s -o output.o\n");
        printf("  --target=<arch>  Required. x86-64, aarch64, riscv64, wasm32\n");
        printf("  --syntax=<s>     x86-64 only: intel (default) or gas\n");
        printf("  -D key=value     Define a preprocessor symbol\n");
        printf("  --template       Force template pass\n");
        printf("  --keep-expanded=<path>  Dump post-template buffer\n");
        printf("  --unsafe-lua     Enable unsafe Lua (for local dev only)\n");
        printf("  -W error         Promote warnings to errors\n");
        printf("  -o output.o      Output file\n");
        printf("  --format=elf|pe|macho  Output format\n");
        return 0;
      default: break;
    }
  }

  /* Positional arguments */
  for (int i = s.ind; i < argc; i++) {
    if (!opts.input) opts.input = argv[i];
    else die("exactly one input file is required");
  }

  if (!opts.target) die("--target is required (x86-64, aarch64, riscv64, wasm32)");
  if (!opts.input) die("no input file specified");
  if (!opts.output) die("-o output.o is required");

  /* Validate target+syntax */
  ccw_arch_t arch = parse_target(opts.target);
  if (arch != CCW_ARCH_X86_64 && opts.syntax && strcmp(opts.syntax, "intel")) {
    die("--syntax is only valid for x86-64");
  }

  /* Read input file */
  char *io_error = NULL;
  char *source = read_file(opts.input, &io_error);
  if (!source) {
    fprintf(stderr, "ccwas: %s\n", io_error);
    free(io_error);
    return 2;
  }

  /* ================================================================
   * Phase 1: Template pass (lccwas)
   * ================================================================ */
  ccw_lccwas lc;
  ccw_lccwas_init(&lc, opts.target, opts.syntax, opts.input, opts.unsafe_lua);

  /* Apply -D definitions */
  for (size_t i = 0; i < opts.ndefs; i++) {
    char *eq = strchr(opts.defs[i], '=');
    if (eq) {
      *eq = '\0';
      ccw_lccwas_define(&lc, opts.defs[i], eq + 1);
      *eq = '=';
    } else {
      ccw_lccwas_define(&lc, opts.defs[i], "1");
    }
  }

  char *expanded = NULL;
  if (opts.template || strstr(source, "<?lua")) {
    char *err = NULL;
    if (!ccw_lccwas_expand_buffer(&lc, source, opts.input, &err)) {
      fprintf(stderr, "ccwas: template error: %s\n", err ? err : "unknown");
      free(err); free(source);
      ccw_lccwas_destroy(&lc);
      return 1;
    }
    expanded = ccw_lccwas_take_buffer(&lc);
  } else {
    kstring_t copy = {0, 0, NULL};
    if (kputs(source, &copy) == EOF)
      expanded = NULL;
    else
      expanded = ks_release(&copy);
  }
  ccw_lccwas_seal(&lc);
  if (expanded == NULL) {
    fprintf(stderr, "ccwas: out of memory\n");
    free(source);
    ccw_lccwas_destroy(&lc);
    return 1;
  }

  /* Dump expanded output if requested */
  if (opts.keep_expanded) {
    FILE *kf = fopen(opts.keep_expanded, "wb");
    if (!kf) {
      fprintf(stderr, "ccwas: %s: %s\n", opts.keep_expanded, strerror(errno));
      free(expanded); free(source); ccw_lccwas_destroy(&lc);
      return 2;
    }
    fputs(expanded, kf);
    fclose(kf);
  }

  /* ================================================================
   * Phase 2-5: Parse, semantic, layout, encode
   * ================================================================ */
  ccw_unit_t unit;
  ccw_unit_init(&unit, arch, opts.syntax);

  char *parse_err = NULL;
  if (!ccw_parse_asm(&unit, expanded, opts.input, &parse_err)) {
    fprintf(stderr, "ccwas: %s\n", parse_err ? parse_err : "parse error");
    free(parse_err);
    free(expanded); free(source);
    ccw_unit_destroy(&unit); ccw_lccwas_destroy(&lc);
    return 1;
  }

  /* Check for errors/warnings */
  if (unit.error_count > 0) {
    fprintf(stderr, "ccwas: %d error(s) during assembly\n", unit.error_count);
    free(expanded); free(source);
    ccw_unit_destroy(&unit); ccw_lccwas_destroy(&lc);
    return 1;
  }

  if (opts.werror && unit.warning_count > 0) {
    fprintf(stderr, "ccwas: %d warning(s) treated as errors\n", unit.warning_count);
    free(expanded); free(source);
    ccw_unit_destroy(&unit); ccw_lccwas_destroy(&lc);
    return 1;
  }

  /* ================================================================
   * Phase 6: Emission
   * ================================================================ */
  ccw_obj_format_t fmt = parse_format(opts.format);
  char *obj_err = NULL;
  if (!ccw_obj_write(&unit, opts.output, fmt, &obj_err)) {
    fprintf(stderr, "ccwas: object write error: %s\n", obj_err ? obj_err : "unknown");
    free(obj_err);
    free(expanded); free(source);
    ccw_unit_destroy(&unit); ccw_lccwas_destroy(&lc);
    return 1;
  }

  /* Cleanup */
  free(expanded);
  free(source);
  free(opts.defs);
  ccw_unit_destroy(&unit);
  ccw_lccwas_destroy(&lc);

  return 0;
}
