/* §5: Symbol table, expression evaluation, ISA validation, unit management */
#include "ccw_symtab.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Symbol table (khash-backed)
 * ================================================================ */

ccw_symbol_t *
ccw_symtab_lookup (khash_t (ccw_sym) * tab, const char *name)
{
  khiter_t k = kh_get (ccw_sym, tab, name);
  if (k == kh_end (tab))
    return NULL;
  return &kh_value (tab, k);
}

ccw_symbol_t *
ccw_symtab_define (khash_t (ccw_sym) * tab, const char *name, uint64_t value,
                   int section, int binding)
{
  int ret;
  khiter_t k = kh_put (ccw_sym, tab, name, &ret);
  ccw_symbol_t *sym = &kh_value (tab, k);
  if (ret)
    {
      /* new key: copy name */
      size_t n = strlen (name) + 1;
      sym->name = (char *)malloc (n);
      memcpy (sym->name, name, n);
      kh_key (tab, k) = sym->name;
    }
  sym->value = value;
  sym->section = section;
  sym->binding = binding;
  sym->defined = 1;
  sym->size = 0;
  return sym;
}

void
ccw_symtab_destroy (khash_t (ccw_sym) * tab)
{
  khiter_t k;
  for (k = kh_begin (tab); k != kh_end (tab); ++k)
    {
      if (kh_exist (tab, k))
        free ((char *)kh_key (tab, k));
    }
  kh_destroy (ccw_sym, tab);
}

/* ================================================================
 * Expression parser (recursive descent)
 * ================================================================ */

static const char *expr_pos;
static const char *expr_end;
static char *expr_err;
static char expr_err_buf[256];

static ccw_expr_t *expr_primary (void);
static ccw_expr_t *expr_unary (void);
static ccw_expr_t *expr_mul (void);
static ccw_expr_t *expr_add (void);
static ccw_expr_t *expr_shift (void);
static ccw_expr_t *expr_bitand (void);
static ccw_expr_t *expr_bitxor (void);
static ccw_expr_t *expr_bitor (void);
static ccw_expr_t *expr_parse_inner (void);

static void
skip_ws (void)
{
  while (expr_pos < expr_end && isspace ((unsigned char)*expr_pos))
    expr_pos++;
}

static ccw_expr_t *
expr_new (ccw_expr_kind_t kind)
{
  ccw_expr_t *e = (ccw_expr_t *)calloc (1, sizeof (*e));
  e->kind = kind;
  return e;
}

static ccw_expr_t *
expr_primary (void)
{
  skip_ws ();
  if (expr_pos >= expr_end)
    {
      snprintf (expr_err_buf, sizeof (expr_err_buf),
                "unexpected end of expression");
      expr_err = expr_err_buf;
      return NULL;
    }
  /* character literal */
  if (*expr_pos == '\'')
    {
      expr_pos++;
      if (expr_pos >= expr_end)
        {
          expr_err = "unterminated character literal";
          return NULL;
        }
      char c;
      if (*expr_pos == '\\')
        {
          expr_pos++;
          if (expr_pos >= expr_end)
            {
              expr_err = "unterminated character literal";
              return NULL;
            }
          switch (*expr_pos)
            {
            case 'n':
              c = '\n';
              break;
            case 't':
              c = '\t';
              break;
            case 'r':
              c = '\r';
              break;
            case '0':
              c = '\0';
              break;
            case '\\':
              c = '\\';
              break;
            case '\'':
              c = '\'';
              break;
            default:
              c = *expr_pos;
              break;
            }
        }
      else
        {
          c = *expr_pos;
        }
      expr_pos++;
      if (expr_pos >= expr_end || *expr_pos != '\'')
        {
          expr_err = "unterminated character literal";
          return NULL;
        }
      expr_pos++;
      ccw_expr_t *e = expr_new (CCW_EXPR_CHAR);
      e->ival = (int64_t)(unsigned char)c;
      return e;
    }
  /* integer literal */
  if (isdigit ((unsigned char)*expr_pos))
    {
      char *endp;
      int64_t v = (int64_t)strtoll (expr_pos, &endp, 0);
      expr_pos = endp;
      ccw_expr_t *e = expr_new (CCW_EXPR_INT);
      e->ival = v;
      return e;
    }
  /* '.' (current address counter) */
  if (*expr_pos == '.'
      && (expr_pos + 1 >= expr_end || !isalnum ((unsigned char)expr_pos[1])))
    {
      expr_pos++;
      ccw_expr_t *e = expr_new (CCW_EXPR_CURRENT);
      return e;
    }
  /* identifier */
  if (isalpha ((unsigned char)*expr_pos) || *expr_pos == '_'
      || *expr_pos == '.')
    {
      const char *start = expr_pos;
      while (expr_pos < expr_end
             && (isalnum ((unsigned char)*expr_pos) || *expr_pos == '_'
                 || *expr_pos == '.'))
        expr_pos++;
      size_t n = (size_t)(expr_pos - start);
      ccw_expr_t *e = expr_new (CCW_EXPR_SYM);
      e->sval = (char *)malloc (n + 1);
      memcpy (e->sval, start, n);
      e->sval[n] = '\0';
      return e;
    }
  /* parenthesized expression */
  if (*expr_pos == '(')
    {
      expr_pos++;
      ccw_expr_t *e = expr_parse_inner ();
      if (!e)
        return NULL;
      skip_ws ();
      if (expr_pos >= expr_end || *expr_pos != ')')
        {
          expr_err = "missing closing ')'";
          ccw_expr_free (e);
          return NULL;
        }
      expr_pos++;
      return e;
    }
  /* '%' prefix for binary */
  if (*expr_pos == '%')
    {
      expr_pos++;
      if (expr_pos < expr_end && isdigit ((unsigned char)*expr_pos))
        {
          char *endp;
          int64_t v = (int64_t)strtoll (expr_pos, &endp, 2);
          expr_pos = endp;
          ccw_expr_t *e = expr_new (CCW_EXPR_INT);
          e->ival = v;
          return e;
        }
    }
  snprintf (expr_err_buf, sizeof (expr_err_buf),
            "unexpected '%c' in expression", *expr_pos);
  expr_err = expr_err_buf;
  return NULL;
}

static ccw_expr_t *
expr_unary (void)
{
  skip_ws ();
  if (expr_pos < expr_end
      && (*expr_pos == '-' || *expr_pos == '~' || *expr_pos == '!'))
    {
      char op = *expr_pos;
      expr_pos++;
      ccw_expr_t *child = expr_unary ();
      if (!child)
        return NULL;
      ccw_expr_t *e = expr_new (CCW_EXPR_UNARY);
      e->unary.op = (op == '-')   ? CCW_EXPR_OP_NEG
                    : (op == '~') ? CCW_EXPR_OP_COM
                                  : CCW_EXPR_OP_NOT;
      e->unary.child = child;
      return e;
    }
  return expr_primary ();
}

static ccw_expr_t *
expr_mul (void)
{
  ccw_expr_t *left = expr_unary ();
  if (!left)
    return NULL;
  while (1)
    {
      skip_ws ();
      if (expr_pos >= expr_end)
        break;
      ccw_expr_op_t op;
      switch (*expr_pos)
        {
        case '*':
          op = CCW_EXPR_OP_MUL;
          break;
        case '/':
          op = CCW_EXPR_OP_DIV;
          break;
        case '%':
          op = CCW_EXPR_OP_MOD;
          break;
        default:
          return left;
        }
      expr_pos++;
      ccw_expr_t *right = expr_unary ();
      if (!right)
        {
          ccw_expr_free (left);
          return NULL;
        }
      ccw_expr_t *e = expr_new (CCW_EXPR_BINARY);
      e->binary.op = op;
      e->binary.left = left;
      e->binary.right = right;
      left = e;
    }
  return left;
}

static ccw_expr_t *
expr_add (void)
{
  ccw_expr_t *left = expr_mul ();
  if (!left)
    return NULL;
  while (1)
    {
      skip_ws ();
      if (expr_pos >= expr_end)
        break;
      ccw_expr_op_t op;
      switch (*expr_pos)
        {
        case '+':
          op = CCW_EXPR_OP_ADD;
          break;
        case '-':
          op = CCW_EXPR_OP_SUB;
          break;
        default:
          return left;
        }
      expr_pos++;
      ccw_expr_t *right = expr_mul ();
      if (!right)
        {
          ccw_expr_free (left);
          return NULL;
        }
      ccw_expr_t *e = expr_new (CCW_EXPR_BINARY);
      e->binary.op = op;
      e->binary.left = left;
      e->binary.right = right;
      left = e;
    }
  return left;
}

static ccw_expr_t *
expr_shift (void)
{
  ccw_expr_t *left = expr_add ();
  if (!left)
    return NULL;
  while (1)
    {
      skip_ws ();
      if (expr_pos + 1 >= expr_end)
        break;
      ccw_expr_op_t op;
      if (*expr_pos == '<' && expr_pos[1] == '<')
        {
          op = CCW_EXPR_OP_SHL;
        }
      else if (*expr_pos == '>' && expr_pos[1] == '>')
        {
          op = CCW_EXPR_OP_SHR;
        }
      else
        return left;
      expr_pos += 2;
      ccw_expr_t *right = expr_add ();
      if (!right)
        {
          ccw_expr_free (left);
          return NULL;
        }
      ccw_expr_t *e = expr_new (CCW_EXPR_BINARY);
      e->binary.op = op;
      e->binary.left = left;
      e->binary.right = right;
      left = e;
    }
  return left;
}

static ccw_expr_t *
expr_bitand (void)
{
  ccw_expr_t *left = expr_shift ();
  if (!left)
    return NULL;
  while (1)
    {
      skip_ws ();
      if (expr_pos >= expr_end || *expr_pos != '&')
        return left;
      expr_pos++;
      ccw_expr_t *right = expr_shift ();
      if (!right)
        {
          ccw_expr_free (left);
          return NULL;
        }
      ccw_expr_t *e = expr_new (CCW_EXPR_BINARY);
      e->binary.op = CCW_EXPR_OP_AND;
      e->binary.left = left;
      e->binary.right = right;
      left = e;
    }
}

static ccw_expr_t *
expr_bitxor (void)
{
  ccw_expr_t *left = expr_bitand ();
  if (!left)
    return NULL;
  while (1)
    {
      skip_ws ();
      if (expr_pos >= expr_end || *expr_pos != '^')
        return left;
      expr_pos++;
      ccw_expr_t *right = expr_bitand ();
      if (!right)
        {
          ccw_expr_free (left);
          return NULL;
        }
      ccw_expr_t *e = expr_new (CCW_EXPR_BINARY);
      e->binary.op = CCW_EXPR_OP_XOR;
      e->binary.left = left;
      e->binary.right = right;
      left = e;
    }
}

static ccw_expr_t *
expr_bitor (void)
{
  ccw_expr_t *left = expr_bitxor ();
  if (!left)
    return NULL;
  while (1)
    {
      skip_ws ();
      if (expr_pos >= expr_end || *expr_pos != '|')
        return left;
      expr_pos++;
      ccw_expr_t *right = expr_bitxor ();
      if (!right)
        {
          ccw_expr_free (left);
          return NULL;
        }
      ccw_expr_t *e = expr_new (CCW_EXPR_BINARY);
      e->binary.op = CCW_EXPR_OP_OR;
      e->binary.left = left;
      e->binary.right = right;
      left = e;
    }
}

static ccw_expr_t *
expr_parse_inner (void)
{
  return expr_bitor ();
}

int
ccw_expr_parse (const char *s, ccw_expr_t **out, char **error)
{
  expr_pos = s;
  expr_end = s + strlen (s);
  expr_err = NULL;
  ccw_expr_t *e = expr_parse_inner ();
  if (!e)
    {
      if (error)
        {
          *error = expr_err ? strdup (expr_err) : strdup ("parse error");
        }
      return 0;
    }
  skip_ws ();
  if (expr_pos < expr_end)
    {
      /* trailing garbage */
      if (error)
        {
          snprintf (expr_err_buf, sizeof (expr_err_buf),
                    "trailing characters: '%s'", expr_pos);
          *error = strdup (expr_err_buf);
        }
      ccw_expr_free (e);
      return 0;
    }
  *out = e;
  return 1;
}

int64_t
ccw_expr_eval (const ccw_expr_t *e, const khash_t (ccw_sym) * symtab,
               uint64_t pc, int *ok)
{
  if (!e)
    {
      *ok = 0;
      return 0;
    }
  switch (e->kind)
    {
    case CCW_EXPR_INT:
      *ok = 1;
      return e->ival;
    case CCW_EXPR_CHAR:
      *ok = 1;
      return e->ival;
    case CCW_EXPR_CURRENT:
      *ok = 1;
      return (int64_t)pc;
    case CCW_EXPR_SYM:
      {
        if (!symtab)
          {
            *ok = 0;
            return 0;
          }
        ccw_symbol_t *sym
            = ccw_symtab_lookup ((khash_t (ccw_sym) *)symtab, e->sval);
        if (!sym || !sym->defined)
          {
            *ok = 0;
            return 0;
          }
        *ok = 1;
        return (int64_t)sym->value;
      }
    case CCW_EXPR_UNARY:
      {
        int64_t v = ccw_expr_eval (e->unary.child, symtab, pc, ok);
        if (!*ok)
          return 0;
        switch (e->unary.op)
          {
          case CCW_EXPR_OP_NEG:
            return -v;
          case CCW_EXPR_OP_NOT:
            return !v ? 1 : 0;
          case CCW_EXPR_OP_COM:
            return ~v;
          default:
            *ok = 0;
            return 0;
          }
      }
    case CCW_EXPR_BINARY:
      {
        int64_t l = ccw_expr_eval (e->binary.left, symtab, pc, ok);
        if (!*ok)
          return 0;
        int64_t r = ccw_expr_eval (e->binary.right, symtab, pc, ok);
        if (!*ok)
          return 0;
        switch (e->binary.op)
          {
          case CCW_EXPR_OP_ADD:
            return l + r;
          case CCW_EXPR_OP_SUB:
            return l - r;
          case CCW_EXPR_OP_MUL:
            return l * r;
          case CCW_EXPR_OP_DIV:
            if (r == 0)
              {
                *ok = 0;
                return 0;
              }
            return l / r;
          case CCW_EXPR_OP_MOD:
            if (r == 0)
              {
                *ok = 0;
                return 0;
              }
            return l % r;
          case CCW_EXPR_OP_SHL:
            return l << (r & 63);
          case CCW_EXPR_OP_SHR:
            return l >> (r & 63);
          case CCW_EXPR_OP_AND:
            return l & r;
          case CCW_EXPR_OP_OR:
            return l | r;
          case CCW_EXPR_OP_XOR:
            return l ^ r;
          default:
            *ok = 0;
            return 0;
          }
      }
    }
  *ok = 0;
  return 0;
}

void
ccw_expr_free (ccw_expr_t *e)
{
  if (!e)
    return;
  switch (e->kind)
    {
    case CCW_EXPR_SYM:
      free (e->sval);
      break;
    case CCW_EXPR_UNARY:
      ccw_expr_free (e->unary.child);
      break;
    case CCW_EXPR_BINARY:
      ccw_expr_free (e->binary.left);
      ccw_expr_free (e->binary.right);
      break;
    default:
      break;
    }
  free (e);
}

/* ================================================================
 * Operand helpers
 * ================================================================ */

void
ccw_operand_free (ccw_operand_t *op)
{
  if (!op)
    return;
  switch (op->kind)
    {
    case CCW_OP_REG:
      free (op->reg);
      break;
    case CCW_OP_MEM:
      free (op->mem.base);
      free (op->mem.index);
      free (op->mem.seg);
      break;
    case CCW_OP_LABEL:
      free (op->label);
      break;
    case CCW_OP_EXPR:
      ccw_expr_free ((ccw_expr_t *)op->expr);
      break;
    case CCW_OP_REG_LIST:
      for (size_t i = 0; i < op->reg_list.count; i++)
        free (op->reg_list.regs[i]);
      free (op->reg_list.regs);
      break;
    default:
      break;
    }
}

ccw_operand_t
ccw_operand_copy (const ccw_operand_t *src)
{
  ccw_operand_t dst = *src;
  switch (src->kind)
    {
    case CCW_OP_REG:
      dst.reg = src->reg ? strdup (src->reg) : NULL;
      break;
    case CCW_OP_LABEL:
      dst.label = src->label ? strdup (src->label) : NULL;
      break;
    case CCW_OP_MEM:
      dst.mem.base = src->mem.base ? strdup (src->mem.base) : NULL;
      dst.mem.index = src->mem.index ? strdup (src->mem.index) : NULL;
      dst.mem.seg = src->mem.seg ? strdup (src->mem.seg) : NULL;
      break;
    default:
      break;
    }
  return dst;
}

/* ================================================================
 * Statement helpers
 * ================================================================ */

void
ccw_stmt_free (ccw_stmt_t *s)
{
  if (!s)
    return;
  switch (s->kind)
    {
    case CCW_STMT_INSN:
      for (size_t i = 0; i < s->insn.op_count; i++)
        ccw_operand_free (&s->insn.operands[i]);
      free (s->insn.operands);
      free ((char *)s->insn.mnemonic);
      free (s->insn.suffix);
      break;
    case CCW_STMT_DIRECTIVE:
      switch (s->dir.kind)
        {
        case CCW_DIR_SECTION:
        case CCW_DIR_INCLUDE:
          free (s->dir.str_val);
          break;
        case CCW_DIR_BYTE:
        case CCW_DIR_2BYTE:
        case CCW_DIR_4BYTE:
        case CCW_DIR_8BYTE:
        case CCW_DIR_ASCII:
        case CCW_DIR_ASCIZ:
        case CCW_DIR_ZERO:
        case CCW_DIR_SPACE:
          for (size_t i = 0; i < kv_size (s->dir.data); i++)
            free (kv_A (s->dir.data, i).label);
          kv_destroy (s->dir.data);
          break;
        default:
          break;
        }
      break;
    case CCW_STMT_LABEL:
      free (s->label);
      break;
    default:
      break;
    }
}

/* ================================================================
 * Unit helpers
 * ================================================================ */

void
ccw_unit_init (ccw_unit_t *u, ccw_arch_t arch, const char *syntax)
{
  memset (u, 0, sizeof (*u));
  u->arch = arch;
  u->syntax = syntax;
  u->symtab = kh_init (ccw_sym);
  kv_init (u->sections);
  kv_init (u->relocs);
  kv_init (u->stmts);
  u->cur_section = -1;
  u->text_section = u->data_section = u->bss_section = -1;
  /* create default sections */
  u->text_section
      = ccw_unit_add_section (u, ".text", 1 /* SHT_PROGBITS */,
                              2 /* SHF_ALLOC */ | 4 /* SHF_EXECINSTR */);
  u->data_section = ccw_unit_add_section (
      u, ".data", 1, 2 /* SHF_ALLOC */ | 1 /* SHF_WRITE */);
  u->bss_section = ccw_unit_add_section (u, ".bss", 8 /* SHT_NOBITS */, 2 | 1);
  u->cur_section = u->text_section;
}

void
ccw_unit_destroy (ccw_unit_t *u)
{
  ccw_symtab_destroy (u->symtab);
  for (size_t i = 0; i < kv_size (u->sections); i++)
    {
      free (kv_A (u->sections, i).name);
      free (kv_A (u->sections, i).data);
    }
  kv_destroy (u->sections);
  for (size_t i = 0; i < kv_size (u->relocs); i++)
    {
      free (kv_A (u->relocs, i).symbol);
    }
  kv_destroy (u->relocs);
  for (size_t i = 0; i < kv_size (u->stmts); i++)
    {
      ccw_stmt_free (&kv_A (u->stmts, i));
    }
  kv_destroy (u->stmts);
  for (int i = 0; i < u->enabled_extensions.count; i++)
    {
      free (u->enabled_extensions.names[i]);
    }
  free (u->enabled_extensions.names);
}

int
ccw_unit_add_section (ccw_unit_t *u, const char *name, int type,
                      uint64_t flags)
{
  ccw_section_t sec = { 0 };
  sec.name = strdup (name);
  sec.type = type;
  sec.flags = flags;
  sec.align = 16;
  sec.index = (int)kv_size (u->sections);
  kv_push (ccw_section_t, u->sections, sec);
  return sec.index;
}

void
ccw_unit_emit_byte (ccw_unit_t *u, int sidx, uint8_t b)
{
  if (sidx < 0 || (size_t)sidx >= kv_size (u->sections))
    return;
  ccw_section_t *sec = &kv_A (u->sections, sidx);
  if (sec->len >= sec->cap)
    {
      sec->cap = sec->cap ? sec->cap * 2 : 256;
      sec->data = (uint8_t *)realloc (sec->data, sec->cap);
    }
  sec->data[sec->len++] = b;
}

void
ccw_unit_emit_bytes (ccw_unit_t *u, int sidx, const uint8_t *data, size_t len)
{
  if (sidx < 0 || (size_t)sidx >= kv_size (u->sections))
    return;
  ccw_section_t *sec = &kv_A (u->sections, sidx);
  while (sec->len + len > sec->cap)
    {
      sec->cap = sec->cap ? sec->cap * 2 : 256;
      sec->data = (uint8_t *)realloc (sec->data, sec->cap);
    }
  memcpy (sec->data + sec->len, data, len);
  sec->len += len;
}

void
ccw_unit_emit_reloc (ccw_unit_t *u, ccw_reloc_type_t type, uint64_t offset,
                     uint64_t addend, const char *symbol, int section)
{
  ccw_reloc_t r
      = { type, offset, addend, symbol ? strdup (symbol) : NULL, section };
  kv_push (ccw_reloc_t, u->relocs, r);
}

/* ================================================================
 * ISA validation (stub — driven by manifests in production)
 * ================================================================ */

/* Built-in instruction tables for x86-64 baseline */
typedef struct
{
  const char *name;
  ccw_form_t form;
  uint32_t encoding;
  uint8_t opcode_bytes;
  uint8_t has_rex_w;
  uint8_t is_branch;
  const char *extension;
} ccw_isa_entry_t;

static const ccw_isa_entry_t x86_64_isa[]
    = { { "mov", CCW_FORM_R_R, 0x89, 1, 1, 0, NULL },
        { "mov", CCW_FORM_R_M, 0x8B, 1, 1, 0, NULL },
        { "mov", CCW_FORM_M_R, 0x89, 1, 1, 0, NULL },
        { "mov", CCW_FORM_R_I, 0xB8, 1, 1, 0, NULL },
        { "add", CCW_FORM_R_R, 0x01, 1, 1, 0, NULL },
        { "add", CCW_FORM_R_M, 0x03, 1, 1, 0, NULL },
        { "add", CCW_FORM_M_R, 0x01, 1, 1, 0, NULL },
        { "add", CCW_FORM_R_I, 0x81, 2, 1, 0, NULL },
        { "sub", CCW_FORM_R_R, 0x29, 1, 1, 0, NULL },
        { "sub", CCW_FORM_R_M, 0x2B, 1, 1, 0, NULL },
        { "sub", CCW_FORM_M_R, 0x29, 1, 1, 0, NULL },
        { "sub", CCW_FORM_R_I, 0x81, 2, 1, 0, NULL },
        { "and", CCW_FORM_R_R, 0x21, 1, 1, 0, NULL },
        { "and", CCW_FORM_R_M, 0x23, 1, 1, 0, NULL },
        { "and", CCW_FORM_M_R, 0x21, 1, 1, 0, NULL },
        { "and", CCW_FORM_R_I, 0x81, 2, 1, 0, NULL },
        { "or", CCW_FORM_R_R, 0x09, 1, 1, 0, NULL },
        { "or", CCW_FORM_R_M, 0x0B, 1, 1, 0, NULL },
        { "or", CCW_FORM_M_R, 0x09, 1, 1, 0, NULL },
        { "or", CCW_FORM_R_I, 0x81, 2, 1, 0, NULL },
        { "xor", CCW_FORM_R_R, 0x31, 1, 1, 0, NULL },
        { "xor", CCW_FORM_R_M, 0x33, 1, 1, 0, NULL },
        { "xor", CCW_FORM_M_R, 0x31, 1, 1, 0, NULL },
        { "xor", CCW_FORM_R_I, 0x81, 2, 1, 0, NULL },
        { "cmp", CCW_FORM_R_R, 0x39, 1, 1, 0, NULL },
        { "cmp", CCW_FORM_R_M, 0x3B, 1, 1, 0, NULL },
        { "cmp", CCW_FORM_M_R, 0x39, 1, 1, 0, NULL },
        { "cmp", CCW_FORM_R_I, 0x81, 2, 1, 0, NULL },
        { "imul", CCW_FORM_R_R, 0xAF, 2, 1, 0, NULL },
        { "imul", CCW_FORM_R_M, 0xAF, 2, 1, 0, NULL },
        { "imul", CCW_FORM_R_R_I, 0x6B, 1, 1, 0, NULL },
        { "idiv", CCW_FORM_R, 0xF7, 2, 1, 0, NULL },
        { "div", CCW_FORM_R, 0xF7, 2, 1, 0, NULL },
        { "inc", CCW_FORM_R, 0xFF, 2, 1, 0, NULL },
        { "dec", CCW_FORM_R, 0xFF, 2, 1, 0, NULL },
        { "neg", CCW_FORM_R, 0xF7, 2, 1, 0, NULL },
        { "not", CCW_FORM_R, 0xF7, 2, 1, 0, NULL },
        { "shl", CCW_FORM_R_I, 0xC1, 2, 1, 0, NULL },
        { "shr", CCW_FORM_R_I, 0xC1, 2, 1, 0, NULL },
        { "sar", CCW_FORM_R_I, 0xC1, 2, 1, 0, NULL },
        { "shl", CCW_FORM_R_R, 0xD3, 1, 1, 0, NULL },
        { "shr", CCW_FORM_R_R, 0xD3, 1, 1, 0, NULL },
        { "sar", CCW_FORM_R_R, 0xD3, 1, 1, 0, NULL },
        { "push", CCW_FORM_R, 0x50, 1, 0, 0, NULL },
        { "pop", CCW_FORM_R, 0x58, 1, 0, 0, NULL },
        { "nop", CCW_FORM_NONE, 0x90, 1, 0, 0, NULL },
        { "ret", CCW_FORM_NONE, 0xC3, 1, 0, 0, NULL },
        { "int3", CCW_FORM_NONE, 0xCC, 1, 0, 0, NULL },
        { "syscall", CCW_FORM_NONE, 0x050F, 2, 0, 0, NULL },
        { "call", CCW_FORM_LABEL, 0xE8, 1, 0, 1, NULL },
        { "jmp", CCW_FORM_LABEL, 0xE9, 1, 0, 1, NULL },
        { "je", CCW_FORM_LABEL, 0x84, 2, 0, 1, NULL },
        { "jne", CCW_FORM_LABEL, 0x85, 2, 0, 1, NULL },
        { "jg", CCW_FORM_LABEL, 0x8F, 2, 0, 1, NULL },
        { "jge", CCW_FORM_LABEL, 0x8D, 2, 0, 1, NULL },
        { "jl", CCW_FORM_LABEL, 0x8C, 2, 0, 1, NULL },
        { "jle", CCW_FORM_LABEL, 0x8E, 2, 0, 1, NULL },
        { "ja", CCW_FORM_LABEL, 0x87, 2, 0, 1, NULL },
        { "jae", CCW_FORM_LABEL, 0x83, 2, 0, 1, NULL },
        { "jb", CCW_FORM_LABEL, 0x82, 2, 0, 1, NULL },
        { "jbe", CCW_FORM_LABEL, 0x86, 2, 0, 1, NULL },
        { "lea", CCW_FORM_R_M, 0x8D, 1, 1, 0, NULL },
        { "test", CCW_FORM_R_R, 0x85, 1, 1, 0, NULL },
        { "test", CCW_FORM_R_I, 0xF7, 2, 1, 0, NULL },
        { "movsx", CCW_FORM_R_R, 0xBE, 2, 1, 0, NULL },
        { "movzx", CCW_FORM_R_R, 0xB6, 2, 1, 0, NULL },
        { "sete", CCW_FORM_R, 0x94, 2, 0, 0, NULL },
        { "setne", CCW_FORM_R, 0x95, 2, 0, 0, NULL },
        { "setg", CCW_FORM_R, 0x9F, 2, 0, 0, NULL },
        { "setge", CCW_FORM_R, 0x9D, 2, 0, 0, NULL },
        { "setl", CCW_FORM_R, 0x9C, 2, 0, 0, NULL },
        { "setle", CCW_FORM_R, 0x9E, 2, 0, 0, NULL },
        { "cmovne", CCW_FORM_R_R, 0x45, 2, 1, 0, NULL },
        { "cmove", CCW_FORM_R_R, 0x44, 2, 1, 0, NULL },
        { NULL, 0, 0, 0, 0, 0, NULL } };

int
ccw_isa_validate (ccw_unit_t *u, const char *mnemonic, ccw_form_t *form_out,
                  char **error)
{
  if (u->arch != CCW_ARCH_X86_64)
    {
      /* other targets: stub; accept all for now */
      if (form_out)
        *form_out = CCW_FORM_CUSTOM;
      return 1;
    }
  for (const ccw_isa_entry_t *e = x86_64_isa; e->name; e++)
    {
      if (!strcmp (e->name, mnemonic))
        {
          if (e->extension && !ccw_ext_is_enabled (u, e->extension))
            {
              if (error)
                {
                  char buf[256];
                  snprintf (buf, sizeof (buf),
                            "instruction '%s' requires extension '%s'",
                            mnemonic, e->extension);
                  *error = strdup (buf);
                }
              return 0;
            }
          if (form_out)
            *form_out = e->form;
          return 1;
        }
    }
  if (error)
    {
      char buf[256];
      snprintf (buf, sizeof (buf), "unknown instruction '%s' for target",
                mnemonic);
      *error = strdup (buf);
    }
  return 0;
}

/* ================================================================
 * Extension management
 * ================================================================ */

int
ccw_ext_enable (ccw_unit_t *u, const char *ext)
{
  /* check if already enabled */
  for (int i = 0; i < u->enabled_extensions.count; i++)
    {
      if (!strcmp (u->enabled_extensions.names[i], ext))
        return 1;
    }
  if (u->enabled_extensions.count >= u->enabled_extensions.cap)
    {
      int newcap
          = u->enabled_extensions.cap ? u->enabled_extensions.cap * 2 : 4;
      char **tmp = (char **)realloc (u->enabled_extensions.names,
                                     (size_t)newcap * sizeof (char *));
      if (!tmp)
        return 0;
      u->enabled_extensions.names = tmp;
      u->enabled_extensions.cap = newcap;
    }
  u->enabled_extensions.names[u->enabled_extensions.count++] = strdup (ext);
  return 1;
}

int
ccw_ext_disable (ccw_unit_t *u, const char *ext)
{
  for (int i = 0; i < u->enabled_extensions.count; i++)
    {
      if (!strcmp (u->enabled_extensions.names[i], ext))
        {
          free (u->enabled_extensions.names[i]);
          memmove (&u->enabled_extensions.names[i],
                   &u->enabled_extensions.names[i + 1],
                   (size_t)(u->enabled_extensions.count - i - 1)
                       * sizeof (char *));
          u->enabled_extensions.count--;
          return 1;
        }
    }
  return 0;
}

int
ccw_ext_is_enabled (ccw_unit_t *u, const char *ext)
{
  for (int i = 0; i < u->enabled_extensions.count; i++)
    {
      if (!strcmp (u->enabled_extensions.names[i], ext))
        return 1;
    }
  return 0;
}
