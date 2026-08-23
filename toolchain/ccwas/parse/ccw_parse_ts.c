/* §4: Tree-sitter assembly grammar reader for the instruction layer.
 *
 * The vendored tree-sitter-asm grammar (third_party/tree-sitter-asm) is
 * the primary reader for instruction statements in builds with
 * Tree-sitter enabled. It is deliberately not a dialect authority:
 * labels, directives, macros, line structure, and ISA validation stay in
 * the hand-rolled parse pass (ccw_parse.c), which also remains the
 * reader for any instruction form the generic grammar cannot map
 * exactly. Every unmappable node makes this reader decline (return 0),
 * so the hand-rolled reader decides; builds without Tree-sitter use it
 * exclusively. */
#include "ccw_parse.h"
#include "ccw_symtab.h"

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
/* This grammar ships its C binding header directly in bindings/c/, not
 * in a tree_sitter/ subdirectory like the Swaff grammars. */
#include <tree-sitter-asm.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CCW_TS_MAX_TOKEN 128

/* ================================================================
 * Small helpers
 * ================================================================ */

/* Copy source[start, end) into a bounded NUL-terminated buffer,
 * lowercased when requested. Returns 0 on truncation. */
static int
node_text_copy (const char *src, uint32_t start, uint32_t end, int lower,
                char *out, size_t cap)
{
  size_t len;
  size_t i;
  if (end < start)
    return 0;
  len = (size_t)(end - start);
  if (len + 1 > cap)
    return 0;
  for (i = 0; i < len; i++)
    out[i] = lower ? (char)tolower ((unsigned char)src[start + i])
                   : src[start + i];
  out[len] = '\0';
  return 1;
}

/* Strict ccwas integer literal (§4.1): a form strtoll(…, 0) consumes
 * completely. The grammar's extra literal forms ('#'/'$' prefixes, '_'
 * digit separators) are rejected so accepted instruction syntax never
 * widens silently; those inputs fall back to the hand-rolled reader and
 * keep their current diagnostics. */
static int
parse_int_strict (const char *s, uint32_t len, int64_t *out)
{
  char buf[CCW_TS_MAX_TOKEN];
  char *end = NULL;
  long long v;
  const char *c;
  if (len == 0 || len >= sizeof (buf))
    return 0;
  memcpy (buf, s, len);
  buf[len] = '\0';
  if (buf[0] == '#' || buf[0] == '$' || buf[0] == '_')
    return 0;
  for (c = buf; *c; c++)
    if (*c == '_')
      return 0;
  v = strtoll (buf, &end, 0);
  if (end == buf || *end != '\0')
    return 0;
  *out = (int64_t)v;
  return 1;
}

/* Free whatever a partially built operand owns and zero it again. */
static void
operand_clear (ccw_operand_t *op)
{
  if (op->kind == CCW_OP_REG)
    free (op->reg);
  else if (op->kind == CCW_OP_LABEL)
    free (op->label);
  else if (op->kind == CCW_OP_MEM)
    {
      free (op->mem.base);
      free (op->mem.index);
      free (op->mem.seg);
    }
  memset (op, 0, sizeof (*op));
}

/* ================================================================
 * Operand readers
 * ================================================================ */

static int
read_ident (TSNode node, const char *src, ccw_arch_t arch,
            ccw_operand_t *op)
{
  char tok[CCW_TS_MAX_TOKEN];
  if (!node_text_copy (src, ts_node_start_byte (node),
                       ts_node_end_byte (node), 0, tok, sizeof (tok)))
    return 0;
  /* Same classification order as the hand-rolled reader: known register
   * name, else label reference. */
  if (ccw_parse_reg (arch, tok))
    {
      op->kind = CCW_OP_REG;
      op->reg = strdup (tok);
      return op->reg ? 1 : 0;
    }
  op->kind = CCW_OP_LABEL;
  op->label = strdup (tok);
  return op->label ? 1 : 0;
}

/* Memory operand: only the bracket forms of the grammar's ptr rule map
 * onto ccw_mem_t. Size-prefixed Intel operands (dword ptr […]), the
 * parenthesized absolute form, and *rel[…] decline: ccw_mem_t has no
 * size field, and silently dropping a size would change the encoded
 * instruction. */
static int
read_mem (TSNode node, const char *src, ccw_arch_t arch, ccw_operand_t *op)
{
  char base[CCW_TS_MAX_TOKEN] = "";
  char index[CCW_TS_MAX_TOKEN] = "";
  int64_t disp = 0;
  int sign = 1;
  uint32_t count = ts_node_child_count (node);
  uint32_t i;

  if (src[ts_node_start_byte (node)] != '[')
    return 0;

  for (i = 0; i < count; i++)
    {
      TSNode ch = ts_node_child (node, i);
      const char *ty = ts_node_type (ch);
      uint32_t s = ts_node_start_byte (ch);
      uint32_t e = ts_node_end_byte (ch);
      if (!strcmp (ty, "+"))
        {
          sign = 1;
          continue;
        }
      if (!strcmp (ty, "-"))
        {
          sign = -1;
          continue;
        }
      if (!ts_node_is_named (ch))
        continue; /* '[', ']', ',', '!' */
      if (!strcmp (ty, "reg"))
        {
          if (!base[0])
            {
              if (!node_text_copy (src, s, e, 0, base, sizeof (base)))
                return 0;
            }
          else if (!index[0])
            {
              if (!node_text_copy (src, s, e, 0, index, sizeof (index)))
                return 0;
            }
          else
            return 0;
        }
      else if (!strcmp (ty, "int"))
        {
          int64_t v;
          if (!parse_int_strict (src + s, e - s, &v))
            return 0;
          disp += sign * v;
          sign = 1;
        }
      else if (!strcmp (ty, "ident"))
        {
          /* [base + sym]: the hand-rolled reader also files the symbol
           * under the index slot, so match it exactly. */
          if (index[0]
              || !node_text_copy (src, s, e, 0, index, sizeof (index)))
            return 0;
        }
      else
        return 0;
    }

  /* Same register check as register operands: a bracket operand whose
   * base is not a target register defers to the hand-rolled reader. */
  if (!base[0] || !ccw_parse_reg (arch, base))
    return 0;

  op->kind = CCW_OP_MEM;
  op->mem.base = strdup (base);
  op->mem.index = index[0] ? strdup (index) : NULL;
  op->mem.scale = 1;
  op->mem.disp = disp;
  if (!op->mem.base || (index[0] && !op->mem.index))
    {
      operand_clear (op);
      return 0;
    }
  return 1;
}

/* Character literal 'x' / escape — the same value rules as the
 * hand-rolled reader. Double-quoted strings are not an operand form
 * ccwas defines and decline. */
static int
read_char_literal (TSNode node, const char *src, ccw_operand_t *op)
{
  uint32_t s = ts_node_start_byte (node);
  uint32_t e = ts_node_end_byte (node);
  size_t len = (size_t)(e - s);
  const char *body;
  if (len < 3 || len > 4 || src[s] != '\'' || src[e - 1] != '\'')
    return 0;
  body = src + s + 1;
  op->kind = CCW_OP_IMM;
  if (len == 4)
    {
      if (body[0] != '\\')
        return 0;
      switch (body[1])
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
          op->imm = (int64_t)(unsigned char)body[1];
          break;
        }
      return 1;
    }
  op->imm = (int64_t)(unsigned char)body[0];
  return 1;
}

static int
read_operand (TSNode node, const char *src, ccw_arch_t arch,
              ccw_operand_t *op)
{
  const char *ty = ts_node_type (node);
  memset (op, 0, sizeof (*op));
  if (!strcmp (ty, "ident") || !strcmp (ty, "word"))
    return read_ident (node, src, arch, op);
  if (!strcmp (ty, "int"))
    {
      int64_t v;
      if (!parse_int_strict (src + ts_node_start_byte (node),
                             ts_node_end_byte (node)
                                 - ts_node_start_byte (node),
                             &v))
        return 0;
      op->kind = CCW_OP_IMM;
      op->imm = v;
      return 1;
    }
  if (!strcmp (ty, "ptr"))
    return read_mem (node, src, arch, op);
  if (!strcmp (ty, "string"))
    return read_char_literal (node, src, op);
  /* float, list, tc_infix, and anything else the generic grammar models
   * have no exact ccw_stmt_t mapping; defer to the hand-rolled reader. */
  return 0;
}

/* ================================================================
 * Instruction reader
 * ================================================================ */

static int
read_instruction (TSNode inst, const char *src, ccw_arch_t arch,
                  ccw_stmt_t *stmt)
{
  TSNode kind = ts_node_child_by_field_name (inst, "kind", 4);
  char *mnemonic;
  ccw_operand_t *ops = NULL;
  size_t count = 0;
  uint32_t n = ts_node_named_child_count (inst);
  uint32_t i;

  if (ts_node_is_null (kind))
    return 0;
  if (n > 1)
    {
      ops = (ccw_operand_t *)calloc (n - 1, sizeof (ccw_operand_t));
      if (!ops)
        return 0;
    }
  for (i = 0; i < n; i++)
    {
      TSNode ch = ts_node_named_child (inst, i);
      if (ts_node_eq (ch, kind))
        continue;
      if (!read_operand (ch, src, arch, &ops[count]))
        {
          size_t j;
          for (j = 0; j < count; j++)
            ccw_operand_free (&ops[j]);
          free (ops);
          return 0;
        }
      count++;
    }

  mnemonic = (char *)malloc (CCW_TS_MAX_TOKEN);
  if (!mnemonic
      || !node_text_copy (src, ts_node_start_byte (kind),
                          ts_node_end_byte (kind), 1, mnemonic,
                          CCW_TS_MAX_TOKEN))
    {
      size_t j;
      free (mnemonic);
      for (j = 0; j < count; j++)
        ccw_operand_free (&ops[j]);
      free (ops);
      return 0;
    }

  memset (stmt, 0, sizeof (*stmt));
  stmt->kind = CCW_STMT_INSN;
  stmt->insn.mnemonic = mnemonic;
  stmt->insn.suffix = NULL;
  stmt->insn.operands = ops;
  stmt->insn.op_count = count;
  return 1;
}

int
ccw_parse_insn_ts (const char *line, ccw_stmt_t *stmt, ccw_arch_t arch)
{
  TSParser *parser;
  TSTree *tree;
  int ok = 0;

  if (!line || !*line)
    return 0;

  parser = ts_parser_new ();
  if (!parser || !ts_parser_set_language (parser, tree_sitter_asm ()))
    {
      if (parser)
        ts_parser_delete (parser);
      return 0;
    }
  tree = ts_parser_parse_string (parser, NULL, line, (uint32_t)strlen (line));
  ts_parser_delete (parser);
  if (!tree)
    return 0;

  {
    TSNode root = ts_tree_root_node (tree);
    if (!ts_node_has_error (root) && ts_node_named_child_count (root) == 1)
      {
        TSNode item = ts_node_named_child (root, 0);
        if (!strcmp (ts_node_type (item), "instruction"))
          ok = read_instruction (item, line, arch, stmt);
      }
  }
  ts_tree_delete (tree);
  return ok;
}

#else /* !CCWEAVE_WITH_TREESITTER */

/* Without Tree-sitter the instruction layer always uses the hand-rolled
 * reader in ccw_parse.c. */
int
ccw_parse_insn_ts (const char *line, ccw_stmt_t *stmt, ccw_arch_t arch)
{
  (void)line;
  (void)stmt;
  (void)arch;
  return 0;
}

#endif /* CCWEAVE_WITH_TREESITTER */
