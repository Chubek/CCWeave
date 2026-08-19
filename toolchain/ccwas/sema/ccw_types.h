#ifndef CCWAS_TYPES_H
#define CCWAS_TYPES_H

/* §4: ccwas shared types — operand IR, symbol table, expression AST */
#include "khash.h"
#include "kvec.h"
#include <stddef.h>
#include <stdint.h>

/* --- Target architecture --- */
typedef enum
{
  CCW_ARCH_X86_64,
  CCW_ARCH_AARCH64,
  CCW_ARCH_RISCV64,
  CCW_ARCH_WASM32
} ccw_arch_t;

/* --- Operand kind --- */
typedef enum
{
  CCW_OP_REG,     /* register: name string */
  CCW_OP_IMM,     /* immediate integer */
  CCW_OP_MEM,     /* memory reference: base+index*scale+disp */
  CCW_OP_LABEL,   /* unresolved label reference */
  CCW_OP_EXPR,    /* expression node (deferred) */
  CCW_OP_REG_LIST /* register list (aarch64 push/pop) */
} ccw_op_kind_t;

/* --- Memory operand --- */
typedef struct
{
  char *base;        /* base register name or NULL */
  char *index;       /* index register name or NULL */
  int scale;         /* 1, 2, 4, 8; 0 = no index */
  int64_t disp;      /* displacement */
  int disp_is_label; /* true if disp is a label offset */
  char *seg;         /* segment override or NULL */
} ccw_mem_t;

/* --- Operand --- */
typedef struct
{
  ccw_op_kind_t kind;
  union
  {
    char *reg;     /* CCW_OP_REG */
    int64_t imm;   /* CCW_OP_IMM */
    ccw_mem_t mem; /* CCW_OP_MEM */
    char *label;   /* CCW_OP_LABEL */
    void *expr;    /* CCW_OP_EXPR: pointer to ccw_expr_t */
    struct
    { /* CCW_OP_REG_LIST */
      char **regs;
      size_t count;
    } reg_list;
  };
} ccw_operand_t;

/* --- Expression AST node kind --- */
typedef enum
{
  CCW_EXPR_INT,     /* integer literal */
  CCW_EXPR_SYM,     /* symbol reference */
  CCW_EXPR_UNARY,   /* unary op: -, ~, ! */
  CCW_EXPR_BINARY,  /* binary op: + - * / % << >> & | ^ */
  CCW_EXPR_CURRENT, /* current address counter ($) */
  CCW_EXPR_CHAR     /* character literal */
} ccw_expr_kind_t;

/* --- Binary/unary operators --- */
typedef enum
{
  CCW_EXPR_OP_NEG,
  CCW_EXPR_OP_NOT,
  CCW_EXPR_OP_COM,
  CCW_EXPR_OP_ADD,
  CCW_EXPR_OP_SUB,
  CCW_EXPR_OP_MUL,
  CCW_EXPR_OP_DIV,
  CCW_EXPR_OP_MOD,
  CCW_EXPR_OP_SHL,
  CCW_EXPR_OP_SHR,
  CCW_EXPR_OP_AND,
  CCW_EXPR_OP_OR,
  CCW_EXPR_OP_XOR
} ccw_expr_op_t;

/* --- Expression AST node --- */
typedef struct ccw_expr_s
{
  ccw_expr_kind_t kind;
  union
  {
    int64_t ival;
    char *sval;
    struct
    {
      ccw_expr_op_t op;
      struct ccw_expr_s *child;
    } unary;
    struct
    {
      ccw_expr_op_t op;
      struct ccw_expr_s *left, *right;
    } binary;
  };
} ccw_expr_t;

/* --- Directive kind --- */
typedef enum
{
  CCW_DIR_SECTION,
  CCW_DIR_TEXT,
  CCW_DIR_DATA,
  CCW_DIR_BSS,
  CCW_DIR_GLOBAL,
  CCW_DIR_LOCAL,
  CCW_DIR_ALIGN,
  CCW_DIR_BYTE,
  CCW_DIR_2BYTE,
  CCW_DIR_4BYTE,
  CCW_DIR_8BYTE,
  CCW_DIR_ASCII,
  CCW_DIR_ASCIZ,
  CCW_DIR_ZERO,
  CCW_DIR_SPACE,
  CCW_DIR_EQU,
  CCW_DIR_SET,
  CCW_DIR_ARCH,
  CCW_DIR_MACRO,
  CCW_DIR_ENDM,
  CCW_DIR_INCLUDE,
  CCW_DIR_ERROR,
  CCW_DIR_WARNING,
  CCW_DIR_IFDEF,
  CCW_DIR_IFNDEF,
  CCW_DIR_ELSE,
  CCW_DIR_ENDIF
} ccw_dir_t;

/* --- Instruction operand form --- */
typedef enum
{
  CCW_FORM_R,       /* op r, r */
  CCW_FORM_R_R_R,   /* op r, r, r */
  CCW_FORM_R_R,     /* op r, r */
  CCW_FORM_R_M,     /* op r, m */
  CCW_FORM_M_R,     /* op m, r */
  CCW_FORM_R_I,     /* op r, imm */
  CCW_FORM_M_I,     /* op m, imm */
  CCW_FORM_I,       /* op imm */
  CCW_FORM_M,       /* op m */
  CCW_FORM_NONE,    /* no operands */
  CCW_FORM_R_R_I,   /* op r, r, imm */
  CCW_FORM_R_M_I,   /* op r, m, imm */
  CCW_FORM_R_R_R_I, /* op r, r, r, imm */
  CCW_FORM_RI,      /* op r+imm */
  CCW_FORM_MR,      /* op m+r */
  CCW_FORM_RRI,     /* op r+r+imm */
  CCW_FORM_RRI2,    /* op r+r+imm (2nd variant) */
  CCW_FORM_R_M_R,   /* op r, m, r (aarch64 ldst) */
  CCW_FORM_LABEL,   /* op label (branch target) */
  CCW_FORM_CUSTOM   /* target-specific form */
} ccw_form_t;

/* --- Instruction descriptor --- */
typedef struct
{
  const char *mnemonic;  /* canonical mnemonic (lowercase) */
  ccw_form_t form;       /* operand form */
  const char *extension; /* required extension or NULL for baseline */
  uint32_t encoding;     /* primary encoding value */
  uint32_t alt_encoding; /* alternate encoding (branches, compressed) */
  uint8_t opcode_bytes;  /* number of opcode bytes */
  uint8_t has_rex_w;     /* 1 if REX.W prefix needed */
  uint8_t is_branch;     /* 1 if control flow */
  uint8_t is_privileged; /* 1 if privileged instruction */
} ccw_instr_desc_t;

/* --- Symbol --- */
typedef struct
{
  char *name;
  uint64_t value; /* address or constant value */
  int section;    /* section index; -1 = absolute */
  int binding;    /* 0=local, 1=global, 2=weak */
  int defined;    /* 1 = defined, 0 = external */
  size_t size;    /* size of symbol (0=unknown) */
} ccw_symbol_t;

/* --- Relocation --- */
typedef enum
{
  CCW_RELOC_ABS64,    /* S + A */
  CCW_RELOC_PC32,     /* S + A - P */
  CCW_RELOC_PLT32,    /* L + A - P */
  CCW_RELOC_GOTPCREL, /* G + GOT + A - P */
  CCW_RELOC_REL32,    /* same as PC32 */
  CCW_RELOC_REL8,     /* 8-bit relative */
  CCW_RELOC_REL16,    /* 16-bit relative */
  CCW_RELOC_PC16,     /* 16-bit PC-relative */
  CCW_RELOC_PC64,     /* 64-bit PC-relative */
  CCW_RELOC_ABS32,    /* S + A (32-bit) */
  CCW_RELOC_ABS16,    /* S + A (16-bit) */
  CCW_RELOC_GOT32,    /* GOT entry */
  CCW_RELOC_PLT32_PC, /* PLT entry PC-relative */
  CCW_RELOC_HI20,     /* RISC-V: %hi(symbol) */
  CCW_RELOC_LO12_I,   /* RISC-V: %lo(symbol) imm */
  CCW_RELOC_LO12_S,   /* RISC-V: %lo(symbol) store */
  CCW_RELOC_CALL,     /* aarch64: call target */
  CCW_RELOC_PAGE,     /* aarch64: adrp page */
  CCW_RELOC_PAGEOFF   /* aarch64: page offset */
} ccw_reloc_type_t;

/* --- Relocation entry --- */
typedef struct
{
  ccw_reloc_type_t type;
  uint64_t offset; /* offset within section */
  uint64_t addend; /* addend */
  char *symbol;    /* target symbol name */
  int section;     /* target section index or -1 */
} ccw_reloc_t;

/* --- Section --- */
typedef struct
{
  char *name;
  uint8_t *data;
  size_t len;
  size_t cap;
  int type;       /* SHT_PROGBITS, SHT_NOBITS, etc. */
  uint64_t flags; /* SHF_ALLOC, SHF_EXECINSTR, SHF_WRITE */
  uint64_t addr;  /* section address */
  uint64_t align; /* alignment */
  int index;      /* section index for symbol table */
} ccw_section_t;

/* --- Data directive value --- */
typedef struct
{
  int width;     /* 1, 2, 4, 8 */
  int64_t value; /* numeric value */
  char *label;   /* label reference (for relocations) */
} ccw_data_val_t;

/* --- Line statement (parsed) --- */
typedef enum
{
  CCW_STMT_INSN,      /* instruction */
  CCW_STMT_DIRECTIVE, /* .directive */
  CCW_STMT_LABEL,     /* label: */
  CCW_STMT_EMPTY,     /* blank/comment */
  CCW_STMT_DATA       /* data emission (.byte, .word, etc.) */
} ccw_stmt_kind_t;

/* --- Instruction statement --- */
typedef struct
{
  const char *mnemonic;
  ccw_operand_t *operands;
  size_t op_count;
  char *suffix; /* optional size suffix (b/w/l/q) */
} ccw_insn_stmt_t;

/* --- Data values (kvec of ccw_data_val_t) --- */
typedef kvec_t (ccw_data_val_t) ccw_data_vec_t;

/* --- Directive statement --- */
typedef struct
{
  ccw_dir_t kind;
  union
  {
    char *str_val;       /* section name, include path, etc. */
    int64_t int_val;     /* alignment, etc. */
    ccw_data_vec_t data; /* data values */
    struct
    {
      char *name;
      char **args;
      size_t arg_count;
    } macro_def; /* macro definition */
    struct
    {
      char *name;
      char **args;
      size_t arg_count;
    } macro_call; /* macro invocation */
    struct
    {
      char *arch;
      char *action; /* enable/disable */
    } arch_dir;     /* .arch directive */
  };
} ccw_dir_stmt_t;

/* --- Statement --- */
typedef struct
{
  ccw_stmt_kind_t kind;
  union
  {
    ccw_insn_stmt_t insn;
    ccw_dir_stmt_t dir;
    char *label;
    ccw_data_vec_t data;
  };
  int line;         /* source line number */
  int col;          /* source column */
  const char *file; /* source file name */
} ccw_stmt_t;

/* --- KHASH declarations --- */
KHASH_MAP_INIT_STR (ccw_sym, ccw_symbol_t)

/* --- kvec types --- */
typedef kvec_t (ccw_stmt_t) ccw_stmt_vec_t;
typedef kvec_t (ccw_operand_t) ccw_op_vec_t;
typedef kvec_t (ccw_section_t) ccw_section_vec_t;
typedef kvec_t (ccw_reloc_t) ccw_reloc_vec_t;

/* --- Assembly unit (output of parse + semantic passes) --- */
typedef struct
{
  ccw_arch_t arch;
  const char *syntax;         /* "intel" or "gas" for x86-64 */
  khash_t (ccw_sym) * symtab; /* symbol table: name -> ccw_symbol_t */
  ccw_section_vec_t sections; /* all sections */
  ccw_reloc_vec_t relocs;     /* all relocations */
  ccw_stmt_vec_t stmts;       /* parsed statements */
  int cur_section;            /* current section index */
  int text_section;           /* .text section index */
  int data_section;           /* .data section index */
  int bss_section;            /* .bss section index */
  uint64_t cur_offset;        /* current offset in section */
  int error_count;
  int warning_count;
  /* --- Extension state --- */
  struct
  {
    char **names;
    int count;
    int cap;
  } enabled_extensions;
} ccw_unit_t;

/* --- Module info --- */
typedef struct
{
  const char *name;
  const char *arch_str;
  ccw_arch_t arch;
  int ptr_size;
  int bits;
  const char *endian;
} ccw_module_info_t;

#endif /* CCWAS_TYPES_H */
