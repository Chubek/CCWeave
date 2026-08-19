/* §4: Assembly parser — line-oriented, directive-aware, operand parsing */
#include "ccw_parse.h"
#include "ccw_encode.h"
#include "ccw_symtab.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ================================================================
 * Utility: trim whitespace, strip comments
 * ================================================================ */

static char *
trim_left (char *s)
{
  while (*s && isspace ((unsigned char)*s))
    s++;
  return s;
}

static char *
strip_comment (char *s, int is_wasm)
{
  char *c = is_wasm ? strstr (s, ";;") : strchr (s, ';');
  if (c)
    *c = '\0';
  return s;
}

/* ================================================================
 * Register lookup
 * ================================================================ */

static const char *x86_regs[]
    = { "rax",   "rcx",   "rdx",   "rbx",   "rsp",   "rbp",  "rsi",  "rdi",
        "r8",    "r9",    "r10",   "r11",   "r12",   "r13",  "r14",  "r15",
        "eax",   "ecx",   "edx",   "ebx",   "esp",   "ebp",  "esi",  "edi",
        "r8d",   "r9d",   "r10d",  "r11d",  "r12d",  "r13d", "r14d", "r15d",
        "ax",    "cx",    "dx",    "bx",    "sp",    "bp",   "si",   "di",
        "r8w",   "r9w",   "r10w",  "r11w",  "r12w",  "r13w", "r14w", "r15w",
        "al",    "cl",    "dl",    "bl",    "ah",    "ch",   "dh",   "bh",
        "spl",   "bpl",   "sil",   "dil",   "r8b",   "r9b",  "r10b", "r11b",
        "r12b",  "r13b",  "r14b",  "r15b",  "rip",   "xmm0", "xmm1", "xmm2",
        "xmm3",  "xmm4",  "xmm5",  "xmm6",  "xmm7",  "xmm8", "xmm9", "xmm10",
        "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", NULL };

const char *
ccw_parse_reg (ccw_arch_t arch, const char *name)
{
  if (!name)
    return NULL;
  if (arch == CCW_ARCH_X86_64)
    {
      for (const char **r = x86_regs; *r; r++)
        {
          if (!strcasecmp (*r, name))
            return *r;
        }
    }
  return NULL;
}

/* ================================================================
 * Memory operand parser: [base + index*scale + disp] or [base + disp]
 * ================================================================ */

int
ccw_parse_mem (ccw_arch_t arch, const char *s, ccw_mem_t *mem, char **error)
{
  (void)arch;
  memset (mem, 0, sizeof (*mem));
  const char *p = s;

  /* skip leading whitespace */
  while (*p && isspace ((unsigned char)*p))
    p++;
  if (*p != '[')
    {
      if (error)
        *error = strdup ("expected '[' in memory operand");
      return 0;
    }
  p++;

  /* Parse inside brackets */
  char tok[128];
  int tok_idx = 0;
  int state
      = 0; /* 0 = base, 1 = after +/-, 2 = index, 3 = after *, 4 = scale */
  char saved_base[128] = "";
  char saved_index[128] = "";
  int64_t saved_disp = 0;
  char sign = '+';

  while (*p && *p != ']')
    {
      if (isspace ((unsigned char)*p))
        {
          p++;
          continue;
        }
      if (*p == '+' || *p == '-')
        {
          if (tok_idx > 0)
            {
              tok[tok_idx] = '\0';
              if (state == 0)
                {
                  strcpy (saved_base, tok);
                }
              else if (state == 2)
                {
                  strcpy (saved_index, tok);
                }
              else if (state == 4)
                {
                  saved_disp = (sign == '-') ? -strtoll (tok, NULL, 0)
                                             : strtoll (tok, NULL, 0);
                }
              tok_idx = 0;
            }
          sign = *p;
          state = (state == 0) ? 1 : 3;
          p++;
          continue;
        }
      if (*p == '*')
        {
          if (tok_idx > 0)
            {
              tok[tok_idx] = '\0';
              strcpy (saved_index, tok);
              tok_idx = 0;
            }
          state = 3;
          p++;
          continue;
        }
      if (tok_idx < 127)
        {
          tok[tok_idx++] = *p;
        }
      p++;
    }

  /* Handle last token */
  if (tok_idx > 0)
    {
      tok[tok_idx] = '\0';
      if (state == 0)
        {
          strcpy (saved_base, tok);
        }
      else if (state == 2)
        {
          strcpy (saved_index, tok);
        }
      else if (state == 4)
        {
          saved_disp = (sign == '-') ? -strtoll (tok, NULL, 0)
                                     : strtoll (tok, NULL, 0);
        }
      else if (state == 1)
        {
          /* after +/-: could be disp or index */
          if (isdigit ((unsigned char)tok[0]) || tok[0] == '-'
              || tok[0] == '.')
            {
              saved_disp = (sign == '-') ? -strtoll (tok, NULL, 0)
                                         : strtoll (tok, NULL, 0);
            }
          else
            {
              strcpy (saved_index, tok);
            }
        }
    }

  if (*p != ']')
    {
      if (error)
        *error = strdup ("unterminated memory operand: expected ']'");
      return 0;
    }

  /* Fill in mem struct */
  if (saved_base[0])
    mem->base = strdup (saved_base);
  if (saved_index[0])
    mem->index = strdup (saved_index);
  mem->disp = saved_disp;
  mem->scale = 1; /* default scale */
  return 1;
}

/* ================================================================
 * Operand parser
 * ================================================================ */

static int
parse_one_operand (ccw_arch_t arch, const char *s, ccw_operand_t *op,
                   char **error)
{
  memset (op, 0, sizeof (*op));
  const char *p = s;
  while (*p && isspace ((unsigned char)*p))
    p++;
  if (!*p)
    return 0;

  /* Memory operand: [...] */
  if (*p == '[')
    {
      op->kind = CCW_OP_MEM;
      return ccw_parse_mem (arch, p, &op->mem, error);
    }

  /* Register: check against known registers */
  char tok[128];
  int i = 0;
  while (*p && !isspace ((unsigned char)*p) && *p != ',')
    {
      if (i < 127)
        tok[i++] = *p;
      p++;
    }
  tok[i] = '\0';

  if (ccw_parse_reg (arch, tok))
    {
      op->kind = CCW_OP_REG;
      op->reg = strdup (tok);
      return 1;
    }

  /* Immediate: starts with digit, +, -, or '.' */
  if (isdigit ((unsigned char)tok[0]) || tok[0] == '-' || tok[0] == '+'
      || tok[0] == '.' || tok[0] == '\'' || tok[0] == '0')
    {
      op->kind = CCW_OP_IMM;
      if (tok[0] == '\'')
        {
          /* character literal */
          if (tok[1] == '\\' && tok[2] && tok[3] == '\'')
            {
              switch (tok[2])
                {
                case 'n':
                  op->imm = '\n';
                  break;
                case 't':
                  op->imm = '\t';
                  break;
                case '0':
                  op->imm = '\0';
                  break;
                default:
                  op->imm = (int64_t)tok[2];
                  break;
                }
            }
          else if (tok[1] && tok[2] == '\'')
            {
              op->imm = (int64_t)(unsigned char)tok[1];
            }
        }
      else
        {
          char *endp;
          op->imm = (int64_t)strtoll (tok, &endp, 0);
        }
      return 1;
    }

  /* Label reference */
  op->kind = CCW_OP_LABEL;
  op->label = strdup (tok);
  return 1;
}

int
ccw_parse_operands (ccw_arch_t arch, const char *s, ccw_operand_t **out,
                    size_t *count, char **error)
{
  *out = NULL;
  *count = 0;
  if (!s || !*s)
    return 1;

  /* Count operands */
  const char *p = s;
  size_t n = 1;
  int in_bracket = 0;
  while (*p)
    {
      if (*p == '[')
        in_bracket = 1;
      if (*p == ']')
        in_bracket = 0;
      if (*p == ',' && !in_bracket)
        n++;
      p++;
    }

  ccw_operand_t *ops = (ccw_operand_t *)calloc (n, sizeof (ccw_operand_t));
  if (!ops)
    {
      if (error)
        *error = strdup ("out of memory");
      return 0;
    }

  /* Split and parse */
  char *buf = strdup (s);
  char *saveptr;
  char *token = strtok_r (buf, ",", &saveptr);
  size_t idx = 0;
  while (token && idx < n)
    {
      /* trim */
      char *t = token;
      while (*t && isspace ((unsigned char)*t))
        t++;
      char *e = t + strlen (t) - 1;
      while (e > t && isspace ((unsigned char)*e))
        *e-- = '\0';

      if (!parse_one_operand (arch, t, &ops[idx], error))
        {
          for (size_t j = 0; j < idx; j++)
            ccw_operand_free (&ops[j]);
          free (ops);
          free (buf);
          return 0;
        }
      idx++;
      token = strtok_r (NULL, ",", &saveptr);
    }
  free (buf);

  *out = ops;
  *count = idx;
  return 1;
}

/* ================================================================
 * Data value parser
 * ================================================================ */

int
ccw_parse_data_values (const char *s, ccw_data_vec_t *vec, int width,
                       char **error)
{
  (void)error;
  kv_init (*vec);
  if (!s || !*s)
    return 1;

  char *buf = strdup (s);
  char *saveptr;
  char *token = strtok_r (buf, ",", &saveptr);
  while (token)
    {
      char *t = token;
      while (*t && isspace ((unsigned char)*t))
        t++;
      ccw_data_val_t v = { 0 };
      v.width = width;
      if (isdigit ((unsigned char)*t) || *t == '-' || *t == '+' || *t == '\'')
        {
          v.value = (int64_t)strtoll (t, NULL, 0);
        }
      else
        {
          /* label reference */
          v.label = strdup (t);
        }
      kv_push (ccw_data_val_t, *vec, v);
      token = strtok_r (NULL, ",", &saveptr);
    }
  free (buf);
  return 1;
}

/* ================================================================
 * Line parser: label, directive, or instruction
 * ================================================================ */

int
ccw_parse_line (const char *line, ccw_stmt_t *stmt, ccw_arch_t arch,
                const char *syntax, char **error)
{
  (void)syntax;
  memset (stmt, 0, sizeof (*stmt));

  /* Skip empty */
  if (!line || !*line)
    {
      stmt->kind = CCW_STMT_EMPTY;
      return 0;
    }

  /* Make a mutable copy */
  char *buf = strdup (line);
  char *p = trim_left (buf);

  /* Strip comments */
  p = strip_comment (p, arch == CCW_ARCH_WASM32);
  if (!*p)
    {
      free (buf);
      stmt->kind = CCW_STMT_EMPTY;
      return 0;
    }

  /* Label: name: */
  char *colon = strchr (p, ':');
  if (colon)
    {
      /* Check if it looks like a label (starts with letter/underscore/dot) */
      char *pre = p;
      while (pre < colon && isspace ((unsigned char)*pre))
        pre++;
      if (isalpha ((unsigned char)*pre) || *pre == '_' || *pre == '.')
        {
          /* It's a label */
          *colon = '\0';
          stmt->kind = CCW_STMT_LABEL;
          stmt->label = strdup (p);
          free (buf);
          return 1;
        }
    }

  /* Directive: starts with '.' */
  if (*p == '.')
    {
      stmt->kind = CCW_STMT_DIRECTIVE;
      char *dir = p + 1;
      char *arg = dir;
      while (*arg && !isspace ((unsigned char)*arg))
        arg++;
      if (*arg)
        {
          *arg = '\0';
          arg++;
        }
      while (*arg && isspace ((unsigned char)*arg))
        arg++;

      if (!strcmp (dir, "text"))
        {
          stmt->dir.kind = CCW_DIR_TEXT;
        }
      else if (!strcmp (dir, "data"))
        {
          stmt->dir.kind = CCW_DIR_DATA;
        }
      else if (!strcmp (dir, "bss"))
        {
          stmt->dir.kind = CCW_DIR_BSS;
        }
      else if (!strcmp (dir, "section"))
        {
          stmt->dir.kind = CCW_DIR_SECTION;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "global"))
        {
          stmt->dir.kind = CCW_DIR_GLOBAL;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "local"))
        {
          stmt->dir.kind = CCW_DIR_LOCAL;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "align"))
        {
          stmt->dir.kind = CCW_DIR_ALIGN;
          stmt->dir.int_val = strtoll (arg, NULL, 0);
        }
      else if (!strcmp (dir, "byte"))
        {
          stmt->dir.kind = CCW_DIR_BYTE;
          ccw_parse_data_values (arg, &stmt->dir.data, 1, error);
        }
      else if (!strcmp (dir, "2byte"))
        {
          stmt->dir.kind = CCW_DIR_2BYTE;
          ccw_parse_data_values (arg, &stmt->dir.data, 2, error);
        }
      else if (!strcmp (dir, "4byte"))
        {
          stmt->dir.kind = CCW_DIR_4BYTE;
          ccw_parse_data_values (arg, &stmt->dir.data, 4, error);
        }
      else if (!strcmp (dir, "8byte"))
        {
          stmt->dir.kind = CCW_DIR_8BYTE;
          ccw_parse_data_values (arg, &stmt->dir.data, 8, error);
        }
      else if (!strcmp (dir, "ascii"))
        {
          stmt->dir.kind = CCW_DIR_ASCII;
          stmt->dir.str_val = arg ? strdup (arg) : strdup ("");
        }
      else if (!strcmp (dir, "asciz"))
        {
          stmt->dir.kind = CCW_DIR_ASCIZ;
          stmt->dir.str_val = arg ? strdup (arg) : strdup ("");
        }
      else if (!strcmp (dir, "zero") || !strcmp (dir, "space"))
        {
          stmt->dir.kind = CCW_DIR_SPACE;
          stmt->dir.int_val = strtoll (arg, NULL, 0);
        }
      else if (!strcmp (dir, "equ"))
        {
          stmt->dir.kind = CCW_DIR_EQU;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "arch"))
        {
          stmt->dir.kind = CCW_DIR_ARCH;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "macro"))
        {
          stmt->dir.kind = CCW_DIR_MACRO;
        }
      else if (!strcmp (dir, "endm"))
        {
          stmt->dir.kind = CCW_DIR_ENDM;
        }
      else if (!strcmp (dir, "include"))
        {
          stmt->dir.kind = CCW_DIR_INCLUDE;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "error"))
        {
          stmt->dir.kind = CCW_DIR_ERROR;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "warning"))
        {
          stmt->dir.kind = CCW_DIR_WARNING;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "ifdef"))
        {
          stmt->dir.kind = CCW_DIR_IFDEF;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "ifndef"))
        {
          stmt->dir.kind = CCW_DIR_IFNDEF;
          stmt->dir.str_val = strdup (arg);
        }
      else if (!strcmp (dir, "else"))
        {
          stmt->dir.kind = CCW_DIR_ELSE;
        }
      else if (!strcmp (dir, "endif"))
        {
          stmt->dir.kind = CCW_DIR_ENDIF;
        }
      else if (!strcmp (dir, "globl") || !strcmp (dir, "short")
               || !strcmp (dir, "value") || !strcmp (dir, "long")
               || !strcmp (dir, "int") || !strcmp (dir, "quad")
               || !strcmp (dir, "set") || !strcmp (dir, "endmacro"))
        {
          if (error)
            {
              char ebuf[256];
              snprintf (ebuf, sizeof (ebuf),
                        "unsupported directive alias '.%s'; use the canonical "
                        "CCWAS name",
                        dir);
              *error = strdup (ebuf);
            }
          free (buf);
          return -1;
        }
      else
        {
          /* Unknown directive — treat as label-like */
          stmt->kind = CCW_STMT_LABEL;
          stmt->label = strdup (p);
        }
      free (buf);
      return 1;
    }

  /* Instruction: mnemonic [operands] */
  stmt->kind = CCW_STMT_INSN;
  char *mnemonic = p;
  char *args = p;
  while (*args && !isspace ((unsigned char)*args))
    args++;
  if (*args)
    {
      *args = '\0';
      args++;
    }
  while (*args && isspace ((unsigned char)*args))
    args++;

  /* Lowercase the mnemonic */
  for (char *c = mnemonic; *c; c++)
    *c = (char)tolower ((unsigned char)*c);

  stmt->insn.mnemonic = mnemonic;
  stmt->insn.suffix = NULL;

  /* Parse operands */
  if (*args)
    {
      ccw_parse_operands (arch, args, &stmt->insn.operands,
                          &stmt->insn.op_count, error);
    }
  else
    {
      stmt->insn.operands = NULL;
      stmt->insn.op_count = 0;
    }

  /* We need to copy the mnemonic since buf will be freed */
  stmt->insn.mnemonic = strdup (mnemonic);
  free (buf);
  return 1;
}

/* ================================================================
 * Full assembly parser
 * ================================================================ */

int
ccw_parse_asm (ccw_unit_t *u, const char *source, const char *filename,
               char **error)
{
  char *buf = strdup (source);
  char *saveptr;
  char *line = strtok_r (buf, "\n", &saveptr);
  int line_num = 0;
  int in_macro = 0;
  (void)line_num; /* used later */

  while (line)
    {
      line_num++;
      char *ln = strdup (line);
      char *p = trim_left (ln);

      /* Handle line continuation */
      while (p[strlen (p) - 1] == '\\')
        {
          p[strlen (p) - 1] = '\0';
          char *next = strtok_r (NULL, "\n", &saveptr);
          if (!next)
            break;
          line_num++;
          size_t nl = strlen (ln);
          size_t nn = strlen (next);
          ln = (char *)realloc (ln, nl + nn + 1);
          memcpy (ln + nl, next, nn + 1);
          p = trim_left (ln + nl);
        }

      /* Strip comments */
      p = strip_comment (p, u->arch == CCW_ARCH_WASM32);
      if (!*p)
        {
          free (ln);
          line = strtok_r (NULL, "\n", &saveptr);
          continue;
        }

      ccw_stmt_t stmt;
      memset (&stmt, 0, sizeof (stmt));
      stmt.line = line_num;
      stmt.file = filename;

      char *parse_err = NULL;
      int ret = ccw_parse_line (p, &stmt, u->arch, u->syntax, &parse_err);
      if (ret < 0)
        {
          if (error)
            {
              char ebuf[512];
              snprintf (ebuf, sizeof (ebuf), "%s:%d: %s", filename, line_num,
                        parse_err);
              *error = strdup (ebuf);
            }
          free (parse_err);
          free (ln);
          free (buf);
          return 0;
        }

      /* Process statement */
      if (ret > 0)
        {
          switch (stmt.kind)
            {
            case CCW_STMT_LABEL:
              {
                /* Define symbol at current offset */
                uint64_t off = 0;
                if (u->cur_section >= 0
                    && (size_t)u->cur_section < kv_size (u->sections))
                  {
                    off = kv_A (u->sections, u->cur_section).len;
                  }
                ccw_symbol_t *existing
                    = ccw_symtab_lookup (u->symtab, stmt.label);
                if (existing && existing->defined)
                  {
                    if (error)
                      {
                        char ebuf[512];
                        snprintf (ebuf, sizeof (ebuf),
                                  "%s:%d: symbol '%s' redefined", filename,
                                  line_num, stmt.label);
                        *error = strdup (ebuf);
                      }
                    free (stmt.label);
                    free (ln);
                    free (buf);
                    return 0;
                  }
                int binding = existing ? existing->binding : 0;
                ccw_symtab_define (u->symtab, stmt.label, off, u->cur_section,
                                   binding);
                free (stmt.label);
                break;
              }
            case CCW_STMT_DIRECTIVE:
              {
                switch (stmt.dir.kind)
                  {
                  case CCW_DIR_TEXT:
                    u->cur_section = u->text_section;
                    break;
                  case CCW_DIR_DATA:
                    u->cur_section = u->data_section;
                    break;
                  case CCW_DIR_BSS:
                    u->cur_section = u->bss_section;
                    break;
                  case CCW_DIR_SECTION:
                    {
                      /* Find or create section */
                      int found = -1;
                      for (size_t i = 0; i < kv_size (u->sections); i++)
                        {
                          if (!strcmp (kv_A (u->sections, i).name,
                                       stmt.dir.str_val))
                            {
                              found = (int)i;
                              break;
                            }
                        }
                      if (found < 0)
                        {
                          found = ccw_unit_add_section (
                              u, stmt.dir.str_val, 1,
                              2 /* SHF_ALLOC */ | 4 /* SHF_EXECINSTR */);
                        }
                      u->cur_section = found;
                      free (stmt.dir.str_val);
                      break;
                    }
                  case CCW_DIR_GLOBAL:
                    {
                      ccw_symbol_t *sym
                          = ccw_symtab_lookup (u->symtab, stmt.dir.str_val);
                      if (sym)
                        sym->binding = 1;
                      else
                        {
                          sym = ccw_symtab_define (u->symtab, stmt.dir.str_val,
                                                   0, -1, 1);
                          sym->defined = 0;
                        }
                      free (stmt.dir.str_val);
                      break;
                    }
                  case CCW_DIR_ALIGN:
                    {
                      if (u->cur_section >= 0)
                        {
                          ccw_section_t *sec
                              = &kv_A (u->sections, u->cur_section);
                          uint64_t align = (uint64_t)stmt.dir.int_val;
                          if (stmt.dir.int_val <= 0
                              || (align & (align - 1)) != 0)
                            {
                              if (error)
                                {
                                  char ebuf[512];
                                  snprintf (ebuf, sizeof (ebuf),
                                            "%s:%d: alignment must be a "
                                            "positive power of two",
                                            filename, line_num);
                                  *error = strdup (ebuf);
                                }
                              free (ln);
                              free (buf);
                              return 0;
                            }
                          sec->align = align;
                          uint64_t mask = align - 1;
                          uint8_t fill = (u->arch == CCW_ARCH_X86_64
                                          && (sec->flags & 4))
                                             ? 0x90
                                             : 0;
                          while (sec->len & mask)
                            ccw_unit_emit_byte (u, u->cur_section, fill);
                        }
                      break;
                    }
                  case CCW_DIR_BYTE:
                  case CCW_DIR_2BYTE:
                  case CCW_DIR_4BYTE:
                  case CCW_DIR_8BYTE:
                    {
                      int width = (stmt.dir.kind == CCW_DIR_BYTE)    ? 1
                                  : (stmt.dir.kind == CCW_DIR_2BYTE) ? 2
                                  : (stmt.dir.kind == CCW_DIR_4BYTE) ? 4
                                                                     : 8;
                      for (size_t i = 0; i < kv_size (stmt.dir.data); i++)
                        {
                          ccw_data_val_t *dv = &kv_A (stmt.dir.data, i);
                          if (dv->label)
                            {
                              /* Label reference — emit relocation */
                              uint64_t off
                                  = (u->cur_section >= 0)
                                        ? kv_A (u->sections, u->cur_section)
                                              .len
                                        : 0;
                              ccw_reloc_type_t rtype = (width == 8)
                                                           ? CCW_RELOC_ABS64
                                                           : CCW_RELOC_ABS32;
                              ccw_unit_emit_reloc (u, rtype, off, 0, dv->label,
                                                   -1);
                              for (int j = 0; j < width; j++)
                                ccw_unit_emit_byte (u, u->cur_section, 0);
                            }
                          else
                            {
                              for (int j = 0; j < width; j++)
                                {
                                  ccw_unit_emit_byte (
                                      u, u->cur_section,
                                      (uint8_t)(dv->value & 0xFF));
                                  dv->value >>= 8;
                                }
                            }
                          free (dv->label);
                        }
                      kv_destroy (stmt.dir.data);
                      break;
                    }
                  case CCW_DIR_ASCII:
                    {
                      if (stmt.dir.str_val)
                        {
                          size_t len = strlen (stmt.dir.str_val);
                          /* Unescape */
                          char *s = stmt.dir.str_val;
                          for (size_t i = 0; i < len; i++)
                            {
                              if (s[i] == '\\' && i + 1 < len)
                                {
                                  i++;
                                  switch (s[i])
                                    {
                                    case 'n':
                                      ccw_unit_emit_byte (u, u->cur_section,
                                                          '\n');
                                      break;
                                    case 't':
                                      ccw_unit_emit_byte (u, u->cur_section,
                                                          '\t');
                                      break;
                                    case 'r':
                                      ccw_unit_emit_byte (u, u->cur_section,
                                                          '\r');
                                      break;
                                    case '0':
                                      ccw_unit_emit_byte (u, u->cur_section,
                                                          '\0');
                                      break;
                                    case '\\':
                                      ccw_unit_emit_byte (u, u->cur_section,
                                                          '\\');
                                      break;
                                    default:
                                      ccw_unit_emit_byte (u, u->cur_section,
                                                          s[i]);
                                      break;
                                    }
                                }
                              else
                                {
                                  ccw_unit_emit_byte (u, u->cur_section,
                                                      (uint8_t)s[i]);
                                }
                            }
                          free (stmt.dir.str_val);
                        }
                      break;
                    }
                  case CCW_DIR_ASCIZ:
                    {
                      if (stmt.dir.str_val)
                        {
                          ccw_stmt_t tmp = stmt;
                          tmp.dir.kind = CCW_DIR_ASCII;
                          /* Process ASCII first */
                          {
                            char *s = tmp.dir.str_val;
                            size_t len = strlen (s);
                            for (size_t i = 0; i < len; i++)
                              {
                                if (s[i] == '\\' && i + 1 < len)
                                  {
                                    i++;
                                    switch (s[i])
                                      {
                                      case 'n':
                                        ccw_unit_emit_byte (u, u->cur_section,
                                                            '\n');
                                        break;
                                      case 't':
                                        ccw_unit_emit_byte (u, u->cur_section,
                                                            '\t');
                                        break;
                                      case 'r':
                                        ccw_unit_emit_byte (u, u->cur_section,
                                                            '\r');
                                        break;
                                      case '0':
                                        ccw_unit_emit_byte (u, u->cur_section,
                                                            '\0');
                                        break;
                                      case '\\':
                                        ccw_unit_emit_byte (u, u->cur_section,
                                                            '\\');
                                        break;
                                      default:
                                        ccw_unit_emit_byte (u, u->cur_section,
                                                            s[i]);
                                        break;
                                      }
                                  }
                                else
                                  {
                                    ccw_unit_emit_byte (u, u->cur_section,
                                                        (uint8_t)s[i]);
                                  }
                              }
                          }
                          ccw_unit_emit_byte (u, u->cur_section, 0);
                          free (stmt.dir.str_val);
                        }
                      break;
                    }
                  case CCW_DIR_SPACE:
                    for (int64_t i = 0; i < stmt.dir.int_val; i++)
                      ccw_unit_emit_byte (u, u->cur_section, 0);
                    break;
                  case CCW_DIR_EQU:
                    {
                      /* name, value */
                      char *eq = strchr (stmt.dir.str_val, ',');
                      if (eq)
                        {
                          *eq = '\0';
                          eq++;
                          while (*eq && isspace ((unsigned char)*eq))
                            eq++;
                          ccw_symbol_t *existing = ccw_symtab_lookup (
                              u->symtab, stmt.dir.str_val);
                          if (existing && existing->defined)
                            {
                              if (error)
                                {
                                  char ebuf[512];
                                  snprintf (ebuf, sizeof (ebuf),
                                            "%s:%d: symbol '%s' redefined",
                                            filename, line_num,
                                            stmt.dir.str_val);
                                  *error = strdup (ebuf);
                                }
                              free (stmt.dir.str_val);
                              free (ln);
                              free (buf);
                              return 0;
                            }
                          int64_t val = strtoll (eq, NULL, 0);
                          ccw_symtab_define (u->symtab, stmt.dir.str_val,
                                             (uint64_t)val, -1, 0);
                        }
                      free (stmt.dir.str_val);
                      break;
                    }
                  case CCW_DIR_ERROR:
                    fprintf (stderr, "ccwas: error: %s\n",
                             stmt.dir.str_val ? stmt.dir.str_val : "");
                    free (stmt.dir.str_val);
                    u->error_count++;
                    break;
                  case CCW_DIR_WARNING:
                    fprintf (stderr, "ccwas: warning: %s\n",
                             stmt.dir.str_val ? stmt.dir.str_val : "");
                    free (stmt.dir.str_val);
                    u->warning_count++;
                    break;
                  case CCW_DIR_MACRO:
                    in_macro = 1;
                    break;
                  case CCW_DIR_ENDM:
                    in_macro = 0;
                    break;
                  default:
                    break;
                  }
                break;
              }
            case CCW_STMT_INSN:
              {
                /* Encode instruction */
                if (!in_macro)
                  {
                    char *enc_err = NULL;
                    /* Validate ISA first */
                    ccw_form_t form;
                    if (!ccw_isa_validate (u, stmt.insn.mnemonic, &form,
                                           &enc_err))
                      {
                        if (error)
                          {
                            char ebuf[512];
                            snprintf (ebuf, sizeof (ebuf), "%s:%d: %s",
                                      filename, line_num, enc_err);
                            *error = strdup (ebuf);
                          }
                        free (enc_err);
                        ccw_stmt_free (&stmt);
                        free (ln);
                        free (buf);
                        return 0;
                      }
                    /* Encode */
                    int nbytes = ccw_encode_insn (u, &stmt.insn, &enc_err);
                    if (nbytes == 0)
                      {
                        if (error)
                          {
                            char ebuf[512];
                            snprintf (ebuf, sizeof (ebuf), "%s:%d: %s",
                                      filename, line_num,
                                      enc_err ? enc_err : "encoding failed");
                            *error = strdup (ebuf);
                          }
                        free (enc_err);
                        ccw_stmt_free (&stmt);
                        free (ln);
                        free (buf);
                        return 0;
                      }
                    free (enc_err);
                  }
                ccw_stmt_free (&stmt);
                break;
              }
            default:
              ccw_stmt_free (&stmt);
              break;
            }
        }

      free (ln);
      line = strtok_r (NULL, "\n", &saveptr);
    }

  free (buf);
  return 1;
}
