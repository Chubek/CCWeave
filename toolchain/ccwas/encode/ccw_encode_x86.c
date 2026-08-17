/* §8: x86-64 encoder — Intel syntax, REX prefixes, ModRM/SIB, immediate operands */
#include "ccw_encode.h"
#include "ccw_symtab.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

/* --- x86-64 register table --- */
typedef struct {
  const char *name;
  int         regno;
  int         rex_b;  /* requires REX.B */
  int         is_8bit; /* 1 if 8-bit low register */
  int         is_8bit_high; /* 1 if 8-bit high (AH/CH/DH/BH) */
} x86_reg_t;

static const x86_reg_t x86_regs[] = {
  {"rax", 0, 0, 0, 0}, {"rcx", 1, 0, 0, 0}, {"rdx", 2, 0, 0, 0}, {"rbx", 3, 0, 0, 0},
  {"rsp", 4, 0, 0, 0}, {"rbp", 5, 0, 0, 0}, {"rsi", 6, 0, 0, 0}, {"rdi", 7, 0, 0, 0},
  {"r8",  8, 1, 0, 0}, {"r9",  9, 1, 0, 0}, {"r10", 10, 1, 0, 0}, {"r11", 11, 1, 0, 0},
  {"r12", 12, 1, 0, 0}, {"r13", 13, 1, 0, 0}, {"r14", 14, 1, 0, 0}, {"r15", 15, 1, 0, 0},
  {"eax", 0, 0, 0, 0}, {"ecx", 1, 0, 0, 0}, {"edx", 2, 0, 0, 0}, {"ebx", 3, 0, 0, 0},
  {"esp", 4, 0, 0, 0}, {"ebp", 5, 0, 0, 0}, {"esi", 6, 0, 0, 0}, {"edi", 7, 0, 0, 0},
  {"r8d", 8, 1, 0, 0}, {"r9d", 9, 1, 0, 0}, {"r10d",10, 1, 0, 0}, {"r11d",11, 1, 0, 0},
  {"r12d",12, 1, 0, 0}, {"r13d",13, 1, 0, 0}, {"r14d",14, 1, 0, 0}, {"r15d",15, 1, 0, 0},
  {"ax",  0, 0, 0, 0}, {"cx", 1, 0, 0, 0}, {"dx", 2, 0, 0, 0}, {"bx", 3, 0, 0, 0},
  {"sp",  4, 0, 0, 0}, {"bp", 5, 0, 0, 0}, {"si", 6, 0, 0, 0}, {"di", 7, 0, 0, 0},
  {"r8w", 8, 1, 0, 0}, {"r9w", 9, 1, 0, 0}, {"r10w",10, 1, 0, 0}, {"r11w",11, 1, 0, 0},
  {"r12w",12, 1, 0, 0}, {"r13w",13, 1, 0, 0}, {"r14w",14, 1, 0, 0}, {"r15w",15, 1, 0, 0},
  {"al",  0, 0, 1, 0}, {"cl", 1, 0, 1, 0}, {"dl", 2, 0, 1, 0}, {"bl", 3, 0, 1, 0},
  {"ah",  4, 0, 0, 1}, {"ch", 5, 0, 0, 1}, {"dh", 6, 0, 0, 1}, {"bh", 7, 0, 0, 1},
  {"spl", 4, 0, 1, 0}, {"bpl", 5, 0, 1, 0}, {"sil", 6, 0, 1, 0}, {"dil", 7, 0, 1, 0},
  {"r8b", 8, 1, 1, 0}, {"r9b", 9, 1, 1, 0}, {"r10b",10, 1, 1, 0}, {"r11b",11, 1, 1, 0},
  {"r12b",12, 1, 1, 0}, {"r13b",13, 1, 1, 0}, {"r14b",14, 1, 1, 0}, {"r15b",15, 1, 1, 0},
  {NULL, 0, 0, 0, 0}
};

int ccw_encode_regno(ccw_arch_t arch, const char *name) {
  if (arch != CCW_ARCH_X86_64) return -1;
  if (!name) return -1;
  for (const x86_reg_t *r = x86_regs; r->name; r++) {
    if (!strcasecmp(r->name, name)) return r->regno;
  }
  return -1;
}

const char *ccw_encode_regname(ccw_arch_t arch, int regno) {
  if (arch != CCW_ARCH_X86_64) return NULL;
  /* Return canonical 64-bit name */
  static const char *names[] = {
    "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
    "r8","r9","r10","r11","r12","r13","r14","r15"
  };
  if (regno >= 0 && regno < 16) return names[regno];
  return NULL;
}

/* --- Internal helpers --- */

static const x86_reg_t *lookup_reg(const char *name) {
  if (!name) return NULL;
  for (const x86_reg_t *r = x86_regs; r->name; r++) {
    if (!strcasecmp(r->name, name)) return r;
  }
  return NULL;
}

static int reg_requires_rex_b(int regno) { return regno >= 8; }
static int reg_requires_rex_r(int regno) { return regno >= 8; }

/* Emit a byte into the current section */
static void emit8(ccw_unit_t *u, uint8_t b) {
  ccw_unit_emit_byte(u, u->cur_section, b);
}

/* Emit up to 8 bytes (little-endian) */
static void emit_imm(ccw_unit_t *u, uint64_t val, int bytes) {
  for (int i = 0; i < bytes; i++) {
    emit8(u, (uint8_t)(val & 0xFF));
    val >>= 8;
  }
}

/* Emit ModRM byte */
static void emit_modrm(ccw_unit_t *u, int mod, int reg, int rm) {
  emit8(u, (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7)));
}

/* Emit SIB byte */
static void emit_sib(ccw_unit_t *u, int scale, int index, int base) {
  emit8(u, (uint8_t)(((scale & 3) << 6) | ((index & 7) << 3) | (base & 7)));
}

/* Check if REX prefix is needed and emit it */
static void emit_rex(ccw_unit_t *u, int w, int r, int x, int b) {
  uint8_t rex = 0x40;
  if (w) rex |= 0x08;
  if (r) rex |= 0x04;
  if (x) rex |= 0x02;
  if (b) rex |= 0x01;
  emit8(u, rex);
}

/* --- Main encoder dispatcher --- */

/* Encode mov r64, r64: REX.W + 89 /r */
static int encode_mov_rr(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register in mov");
    return 0;
  }
  int w = 1, r = reg_requires_rex_r(src->regno), b = reg_requires_rex_b(dst->regno);
  emit_rex(u, w, r, 0, b);
  emit8(u, 0x89);
  emit_modrm(u, 3, src->regno, dst->regno);
  return 3;
}

/* Encode generic ALU R,R: REX.W + opcode /r */
static int encode_alu_rr(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                          uint8_t opcode, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, r = reg_requires_rex_r(src->regno), b = reg_requires_rex_b(dst->regno);
  emit_rex(u, w, r, 0, b);
  emit8(u, opcode);
  emit_modrm(u, 3, src->regno, dst->regno);
  return 3;
}

/* Encode push/pop r64 */
static int encode_pushpop_r(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                             int is_push, char **error) {
  const x86_reg_t *r = lookup_reg(insn->operands[0].reg);
  if (!r) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  if (reg_requires_rex_b(r->regno)) emit8(u, 0x41);
  emit8(u, (uint8_t)((is_push ? 0x50 : 0x58) + (r->regno & 7)));
  return reg_requires_rex_b(r->regno) ? 2 : 1;
}

/* Encode simple no-operand instructions */
static int encode_simple(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  (void)insn; (void)error;
  if (!strcmp(insn->mnemonic, "nop")) { emit8(u, 0x90); return 1; }
  if (!strcmp(insn->mnemonic, "ret")) { emit8(u, 0xC3); return 1; }
  if (!strcmp(insn->mnemonic, "int3")) { emit8(u, 0xCC); return 1; }
  if (!strcmp(insn->mnemonic, "syscall")) { emit8(u, 0x0F); emit8(u, 0x05); return 2; }
  return 0;
}

/* Encode branch (call/jmp rel32) */
static int encode_branch(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                          uint8_t opcode, char **error) {
  if (insn->operands[0].kind == CCW_OP_LABEL) {
    emit8(u, opcode);
    /* emit placeholder rel32; will be fixed up via relocation */
    uint64_t off = (u->cur_section >= 0) ? kv_A(u->sections, u->cur_section).len : 0;
    ccw_unit_emit_reloc(u, CCW_RELOC_PC32, off + 1, -4, insn->operands[0].label, -1);
    emit_imm(u, 0, 4);
    return 5;
  }
  if (error) *error = strdup("branch target must be a label");
  return 0;
}

/* Encode conditional branch (jcc rel8/rel32) */
static int encode_jcc(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                       uint8_t opcode, char **error) {
  if (insn->operands[0].kind == CCW_OP_LABEL) {
    emit8(u, 0x0F);
    emit8(u, opcode);
    uint64_t off = (u->cur_section >= 0) ? kv_A(u->sections, u->cur_section).len : 0;
    ccw_unit_emit_reloc(u, CCW_RELOC_PC32, off, -4, insn->operands[0].label, -1);
    emit_imm(u, 0, 4);
    return 6;
  }
  if (error) *error = strdup("branch target must be a label");
  return 0;
}

/* Encode mov r64, imm64 */
static int encode_mov_ri(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  if (!dst) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, b = reg_requires_rex_b(dst->regno);
  emit_rex(u, w, 0, 0, b);
  emit8(u, (uint8_t)(0xB8 + (dst->regno & 7)));
  emit_imm(u, (uint64_t)insn->operands[1].imm, 8);
  return 10;
}

/* Encode ALU r64, imm8 */
static int encode_alu_ri(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                          uint8_t opcode, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  if (!dst) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int64_t imm = insn->operands[1].imm;
  int w = 1, b = reg_requires_rex_b(dst->regno);
  emit_rex(u, w, 0, 0, b);
  if (imm >= -128 && imm <= 127) {
    emit8(u, 0x83);
    emit_modrm(u, 3, opcode, dst->regno);
    emit_imm(u, (uint64_t)imm, 1);
    return 4;
  } else {
    emit8(u, 0x81);
    emit_modrm(u, 3, opcode, dst->regno);
    emit_imm(u, (uint64_t)imm, 4);
    return 7;
  }
}

/* Encode shl/shr/sar r64, cl */
static int encode_shift_rr(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                            uint8_t opcode, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  /* shift by cl only */
  if (src->regno != 1) {
    if (error) *error = strdup("shift count must be in cl");
    return 0;
  }
  int w = 1, b = reg_requires_rex_b(dst->regno);
  emit_rex(u, w, 0, 0, b);
  emit8(u, 0xD3);
  emit_modrm(u, 3, opcode, dst->regno);
  return 3;
}

/* Encode shl/shr/sar r64, imm8 */
static int encode_shift_ri(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                            uint8_t opcode, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  if (!dst) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int64_t imm = insn->operands[1].imm;
  int w = 1, b = reg_requires_rex_b(dst->regno);
  emit_rex(u, w, 0, 0, b);
  if (imm == 1) {
    emit8(u, 0xD1);
    emit_modrm(u, 3, opcode, dst->regno);
    return 3;
  } else {
    emit8(u, 0xC1);
    emit_modrm(u, 3, opcode, dst->regno);
    emit_imm(u, (uint64_t)imm, 1);
    return 4;
  }
}

/* Encode inc/dec r64 */
static int encode_incdec_r(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                            int is_inc, char **error) {
  const x86_reg_t *r = lookup_reg(insn->operands[0].reg);
  if (!r) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, b = reg_requires_rex_b(r->regno);
  emit_rex(u, w, 0, 0, b);
  emit8(u, 0xFF);
  emit_modrm(u, 3, is_inc ? 0 : 1, r->regno);
  return 3;
}

/* Encode not/neg r64 */
static int encode_notneg_r(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                            uint8_t regop, char **error) {
  const x86_reg_t *r = lookup_reg(insn->operands[0].reg);
  if (!r) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, b = reg_requires_rex_b(r->regno);
  emit_rex(u, w, 0, 0, b);
  emit8(u, 0xF7);
  emit_modrm(u, 3, regop, r->regno);
  return 3;
}

/* Encode test r64, r64 */
static int encode_test_rr(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, r = reg_requires_rex_r(src->regno), b = reg_requires_rex_b(dst->regno);
  emit_rex(u, w, r, 0, b);
  emit8(u, 0x85);
  emit_modrm(u, 3, src->regno, dst->regno);
  return 3;
}

/* Encode lea r64, [mem] */
static int encode_lea_rm(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  if (!dst) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  ccw_mem_t *mem = &insn->operands[1].mem;
  const x86_reg_t *base_r = lookup_reg(mem->base);
  const x86_reg_t *idx_r = mem->index ? lookup_reg(mem->index) : NULL;
  int w = 1, r = reg_requires_rex_r(dst->regno);
  int x = idx_r && reg_requires_rex_b(idx_r->regno);
  int b = base_r && reg_requires_rex_b(base_r->regno);
  int has_disp = (mem->disp != 0 || mem->disp_is_label);
  int disp_size = (mem->disp >= -128 && mem->disp <= 127 && !mem->disp_is_label) ? 1 : 4;

  if (!base_r && !idx_r) {
    /* absolute address */
    emit_rex(u, w, r, 0, 0);
    emit8(u, 0x8D);
    emit_modrm(u, 0, dst->regno, 4);
    emit_sib(u, 0, 4, 5);
    emit_imm(u, (uint64_t)mem->disp, 4);
    return 7;
  }

  if (base_r && !idx_r) {
    int mod = has_disp ? (disp_size == 1 ? 1 : 2) : 0;
    if (base_r->regno == 4) {
      /* RSP/R12 needs SIB */
      emit_rex(u, w, r, 0, b);
      emit8(u, 0x8D);
      emit_modrm(u, mod, dst->regno, 4);
      emit_sib(u, 0, 4, 4);
    } else {
      emit_rex(u, w, r, 0, b);
      emit8(u, 0x8D);
      emit_modrm(u, mod, dst->regno, base_r->regno);
    }
    if (has_disp) emit_imm(u, (uint64_t)mem->disp, disp_size);
    return 3 + (has_disp ? disp_size : 0);
  }

  /* base + index*scale */
  int scale = mem->scale;
  if (scale == 0) scale = 1;
  int scale_enc = (scale == 1) ? 0 : (scale == 2) ? 1 : (scale == 4) ? 2 : 3;
  int mod = has_disp ? (disp_size == 1 ? 1 : 2) : 0;
  emit_rex(u, w, r, x, b);
  emit8(u, 0x8D);
  emit_modrm(u, mod, dst->regno, 4);
  int base_enc = base_r ? base_r->regno : 5;
  emit_sib(u, scale_enc, idx_r ? idx_r->regno : 4, base_enc);
  if (has_disp) emit_imm(u, (uint64_t)mem->disp, disp_size);
  return 4 + (has_disp ? disp_size : 0);
}

/* Encode mov r, [mem] */
static int encode_mov_rm(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  if (!dst) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  ccw_mem_t *mem = &insn->operands[1].mem;
  const x86_reg_t *base_r = lookup_reg(mem->base);
  const x86_reg_t *idx_r = mem->index ? lookup_reg(mem->index) : NULL;
  int w = 1, r = reg_requires_rex_r(dst->regno);
  int x = idx_r && reg_requires_rex_b(idx_r->regno);
  int b = base_r && reg_requires_rex_b(base_r->regno);
  int has_disp = (mem->disp != 0 || mem->disp_is_label);
  int disp_size = (mem->disp >= -128 && mem->disp <= 127 && !mem->disp_is_label) ? 1 : 4;

  /* For [label] with no base/index */
  if (!base_r && !idx_r) {
    emit_rex(u, w, r, 0, 0);
    emit8(u, 0x8B);
    emit_modrm(u, 0, dst->regno, 4);
    emit_sib(u, 0, 4, 5);
    if (mem->disp_is_label) {
      uint64_t off = kv_A(u->sections, u->cur_section).len;
      (void)off;
      emit_imm(u, 0, 4);
      /* FIXME: add proper label relocation */
      return 7;
    }
    emit_imm(u, (uint64_t)mem->disp, 4);
    return 7;
  }

  if (base_r && !idx_r) {
    int mod = has_disp ? (disp_size == 1 ? 1 : 2) : 0;
    if (base_r->regno == 4) {
      emit_rex(u, w, r, 0, b);
      emit8(u, 0x8B);
      emit_modrm(u, mod, dst->regno, 4);
      emit_sib(u, 0, 4, 4);
    } else {
      emit_rex(u, w, r, 0, b);
      emit8(u, 0x8B);
      emit_modrm(u, mod, dst->regno, base_r->regno);
    }
    if (has_disp) emit_imm(u, (uint64_t)mem->disp, disp_size);
    return 3 + (has_disp ? disp_size : 0);
  }

  /* base + index*scale */
  int scale = mem->scale;
  if (scale == 0) scale = 1;
  int scale_enc = (scale == 1) ? 0 : (scale == 2) ? 1 : (scale == 4) ? 2 : 3;
  int mod = has_disp ? (disp_size == 1 ? 1 : 2) : 0;
  emit_rex(u, w, r, x, b);
  emit8(u, 0x8B);
  emit_modrm(u, mod, dst->regno, 4);
  int base_enc = base_r ? base_r->regno : 5;
  emit_sib(u, scale_enc, idx_r ? idx_r->regno : 4, base_enc);
  if (has_disp) emit_imm(u, (uint64_t)mem->disp, disp_size);
  return 4 + (has_disp ? disp_size : 0);
}

/* Encode mov [mem], r */
static int encode_mov_mr(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  ccw_mem_t *mem = &insn->operands[0].mem;
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  const x86_reg_t *base_r = lookup_reg(mem->base);
  int w = 1, r = reg_requires_rex_r(src->regno);
  int b = base_r && reg_requires_rex_b(base_r->regno);
  int has_disp = (mem->disp != 0);
  int disp_size = (mem->disp >= -128 && mem->disp <= 127) ? 1 : 4;

  if (!base_r) {
    emit_rex(u, w, r, 0, 0);
    emit8(u, 0x89);
    emit_modrm(u, 0, src->regno, 4);
    emit_sib(u, 0, 4, 5);
    emit_imm(u, (uint64_t)mem->disp, 4);
    return 7;
  }

  int mod = has_disp ? (disp_size == 1 ? 1 : 2) : 0;
  if (base_r->regno == 4) {
    emit_rex(u, w, r, 0, b);
    emit8(u, 0x89);
    emit_modrm(u, mod, src->regno, 4);
    emit_sib(u, 0, 4, 4);
  } else {
    emit_rex(u, w, r, 0, b);
    emit8(u, 0x89);
    emit_modrm(u, mod, src->regno, base_r->regno);
  }
  if (has_disp) emit_imm(u, (uint64_t)mem->disp, disp_size);
  return 3 + (has_disp ? disp_size : 0);
}

/* Encode imul r, r */
static int encode_imul_rr(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, r = reg_requires_rex_r(dst->regno), b = reg_requires_rex_b(src->regno);
  emit_rex(u, w, r, 0, b);
  emit8(u, 0x0F);
  emit8(u, 0xAF);
  emit_modrm(u, 3, dst->regno, src->regno);
  return 4;
}

/* Encode imul r, r, imm */
static int encode_imul_rri(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int64_t imm = insn->operands[2].imm;
  int w = 1, r = reg_requires_rex_r(dst->regno), b = reg_requires_rex_b(src->regno);
  emit_rex(u, w, r, 0, b);
  emit8(u, 0x6B);
  emit_modrm(u, 3, dst->regno, src->regno);
  emit_imm(u, (uint64_t)imm, 1);
  return 4;
}

/* Encode movsx/movzx r64, r32/16/8 */
static int encode_movext(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                          int is_sx, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, r = reg_requires_rex_r(dst->regno), b = reg_requires_rex_b(src->regno);
  emit_rex(u, w, r, 0, b);
  emit8(u, 0x0F);
  emit8(u, (uint8_t)(is_sx ? 0xBF : 0xB7));
  emit_modrm(u, 3, dst->regno, src->regno);
  return 4;
}

/* Encode setcc r8 */
static int encode_setcc(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                         uint8_t opcode, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  if (!dst) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int r = reg_requires_rex_r(dst->regno), b = reg_requires_rex_b(dst->regno);
  if (r || b) emit_rex(u, 0, r, 0, b);
  emit8(u, 0x0F);
  emit8(u, opcode);
  emit_modrm(u, 3, 0, dst->regno);
  return r || b ? 4 : 3;
}

/* Encode cmovcc r64, r64 */
static int encode_cmovcc(ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                          uint8_t opcode, char **error) {
  const x86_reg_t *dst = lookup_reg(insn->operands[0].reg);
  const x86_reg_t *src = lookup_reg(insn->operands[1].reg);
  if (!dst || !src) {
    if (error) *error = strdup("invalid register");
    return 0;
  }
  int w = 1, r = reg_requires_rex_r(dst->regno), b = reg_requires_rex_b(src->regno);
  emit_rex(u, w, r, 0, b);
  emit8(u, 0x0F);
  emit8(u, opcode);
  emit_modrm(u, 3, dst->regno, src->regno);
  return 4;
}

/* --- Main dispatcher --- */

int ccw_encode_insn(ccw_unit_t *u, const ccw_insn_stmt_t *insn, char **error) {
  const char *m = insn->mnemonic;
  size_t n = insn->op_count;

  /* 0-operand */
  if (n == 0) {
    if (!strcmp(m, "nop")) return encode_simple(u, insn, error);
    if (!strcmp(m, "ret")) return encode_simple(u, insn, error);
    if (!strcmp(m, "int3")) return encode_simple(u, insn, error);
    if (!strcmp(m, "syscall")) return encode_simple(u, insn, error);
    if (error) *error = strdup("unknown instruction");
    return 0;
  }

  /* 1-operand */
  if (n == 1) {
    if (insn->operands[0].kind == CCW_OP_REG) {
      if (!strcmp(m, "push")) return encode_pushpop_r(u, insn, 1, error);
      if (!strcmp(m, "pop"))  return encode_pushpop_r(u, insn, 0, error);
      if (!strcmp(m, "inc"))  return encode_incdec_r(u, insn, 1, error);
      if (!strcmp(m, "dec"))  return encode_incdec_r(u, insn, 0, error);
      if (!strcmp(m, "not"))  return encode_notneg_r(u, insn, 2, error);
      if (!strcmp(m, "neg"))  return encode_notneg_r(u, insn, 3, error);
      if (!strcmp(m, "idiv"))  return encode_notneg_r(u, insn, 7, error);
      if (!strcmp(m, "div"))   return encode_notneg_r(u, insn, 6, error);
      if (!strcmp(m, "sete"))  return encode_setcc(u, insn, 0x94, error);
      if (!strcmp(m, "setne")) return encode_setcc(u, insn, 0x95, error);
      if (!strcmp(m, "setg"))  return encode_setcc(u, insn, 0x9F, error);
      if (!strcmp(m, "setge")) return encode_setcc(u, insn, 0x9D, error);
      if (!strcmp(m, "setl"))  return encode_setcc(u, insn, 0x9C, error);
      if (!strcmp(m, "setle")) return encode_setcc(u, insn, 0x9E, error);
    }
    if (insn->operands[0].kind == CCW_OP_LABEL) {
      if (!strcmp(m, "call")) return encode_branch(u, insn, 0xE8, error);
      if (!strcmp(m, "jmp"))  return encode_branch(u, insn, 0xE9, error);
      if (!strcmp(m, "je"))   return encode_jcc(u, insn, 0x84, error);
      if (!strcmp(m, "jne"))  return encode_jcc(u, insn, 0x85, error);
      if (!strcmp(m, "jg"))   return encode_jcc(u, insn, 0x8F, error);
      if (!strcmp(m, "jge"))  return encode_jcc(u, insn, 0x8D, error);
      if (!strcmp(m, "jl"))   return encode_jcc(u, insn, 0x8C, error);
      if (!strcmp(m, "jle"))  return encode_jcc(u, insn, 0x8E, error);
      if (!strcmp(m, "ja"))   return encode_jcc(u, insn, 0x87, error);
      if (!strcmp(m, "jae"))  return encode_jcc(u, insn, 0x83, error);
      if (!strcmp(m, "jb"))   return encode_jcc(u, insn, 0x82, error);
      if (!strcmp(m, "jbe"))  return encode_jcc(u, insn, 0x86, error);
    }
    if (error) *error = strdup("unsupported operand for 1-op instruction");
    return 0;
  }

  /* 2-operand */
  if (n == 2) {
    /* R, R */
    if (insn->operands[0].kind == CCW_OP_REG && insn->operands[1].kind == CCW_OP_REG) {
      if (!strcmp(m, "mov")) return encode_mov_rr(u, insn, error);
      if (!strcmp(m, "add")) return encode_alu_rr(u, insn, 0x01, error);
      if (!strcmp(m, "sub")) return encode_alu_rr(u, insn, 0x29, error);
      if (!strcmp(m, "and")) return encode_alu_rr(u, insn, 0x21, error);
      if (!strcmp(m, "or"))  return encode_alu_rr(u, insn, 0x09, error);
      if (!strcmp(m, "xor")) return encode_alu_rr(u, insn, 0x31, error);
      if (!strcmp(m, "cmp")) return encode_alu_rr(u, insn, 0x39, error);
      if (!strcmp(m, "test")) return encode_test_rr(u, insn, error);
      if (!strcmp(m, "imul")) return encode_imul_rr(u, insn, error);
      if (!strcmp(m, "movsx")) return encode_movext(u, insn, 1, error);
      if (!strcmp(m, "movzx")) return encode_movext(u, insn, 0, error);
      if (!strcmp(m, "cmovne")) return encode_cmovcc(u, insn, 0x45, error);
      if (!strcmp(m, "cmove"))  return encode_cmovcc(u, insn, 0x44, error);
      /* shift by cl */
      if (!strcmp(m, "shl")) return encode_shift_rr(u, insn, 4, error);
      if (!strcmp(m, "shr")) return encode_shift_rr(u, insn, 5, error);
      if (!strcmp(m, "sar")) return encode_shift_rr(u, insn, 7, error);
    }
    /* R, I */
    if (insn->operands[0].kind == CCW_OP_REG && insn->operands[1].kind == CCW_OP_IMM) {
      if (!strcmp(m, "mov")) return encode_mov_ri(u, insn, error);
      if (!strcmp(m, "add")) return encode_alu_ri(u, insn, 0, error);
      if (!strcmp(m, "sub")) return encode_alu_ri(u, insn, 5, error);
      if (!strcmp(m, "and")) return encode_alu_ri(u, insn, 4, error);
      if (!strcmp(m, "or"))  return encode_alu_ri(u, insn, 1, error);
      if (!strcmp(m, "xor")) return encode_alu_ri(u, insn, 6, error);
      if (!strcmp(m, "cmp")) return encode_alu_ri(u, insn, 7, error);
      if (!strcmp(m, "test")) return encode_alu_ri(u, insn, 0, error);
      if (!strcmp(m, "shl")) return encode_shift_ri(u, insn, 4, error);
      if (!strcmp(m, "shr")) return encode_shift_ri(u, insn, 5, error);
      if (!strcmp(m, "sar")) return encode_shift_ri(u, insn, 7, error);
    }
    /* R, M */
    if (insn->operands[0].kind == CCW_OP_REG && insn->operands[1].kind == CCW_OP_MEM) {
      if (!strcmp(m, "mov")) return encode_mov_rm(u, insn, error);
      if (!strcmp(m, "lea")) return encode_lea_rm(u, insn, error);
      if (!strcmp(m, "add")) return encode_mov_rm(u, insn, error); /* stub */
      if (!strcmp(m, "sub")) return encode_mov_rm(u, insn, error); /* stub */
      if (!strcmp(m, "cmp")) return encode_mov_rm(u, insn, error); /* stub */
      if (!strcmp(m, "imul")) return encode_mov_rm(u, insn, error); /* stub */
    }
    /* M, R */
    if (insn->operands[0].kind == CCW_OP_MEM && insn->operands[1].kind == CCW_OP_REG) {
      if (!strcmp(m, "mov")) return encode_mov_mr(u, insn, error);
      if (!strcmp(m, "add")) return encode_mov_mr(u, insn, error); /* stub */
      if (!strcmp(m, "sub")) return encode_mov_mr(u, insn, error); /* stub */
      if (!strcmp(m, "cmp")) return encode_mov_mr(u, insn, error); /* stub */
    }
    if (error) *error = strdup("unsupported operand combination");
    return 0;
  }

  /* 3-operand */
  if (n == 3) {
    if (insn->operands[0].kind == CCW_OP_REG &&
        insn->operands[1].kind == CCW_OP_REG &&
        insn->operands[2].kind == CCW_OP_IMM) {
      if (!strcmp(m, "imul")) return encode_imul_rri(u, insn, error);
    }
    if (error) *error = strdup("unsupported 3-operand form");
    return 0;
  }

  if (error) *error = strdup("unsupported operand count");
  return 0;
}
