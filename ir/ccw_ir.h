/* Weave IR core — §5.2 shared core, §5.4 canonical in-memory form.
 * The in-memory module is canonical; text is a serialization of it. */

#ifndef CCW_IR_H
#define CCW_IR_H

#include "../glue/GlueSTD.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- profiles (§5.3) ---------- */

typedef enum {
    CCW_PROFILE_TILLY = 0,
    CCW_PROFILE_ON1X  = 1
} ccw_profile;

const char *ccw_profile_name(ccw_profile p);
bool        ccw_profile_parse(const char *name, ccw_profile *out);

/* ---------- type system (§5.2) ---------- */

typedef enum {
    CCW_TY_VOID = 0,
    CCW_TY_I1,
    CCW_TY_I8,
    CCW_TY_I16,
    CCW_TY_I32,
    CCW_TY_I64,
    CCW_TY_F32,
    CCW_TY_F64,
    CCW_TY_PTR
} ccw_ir_type;

const char *ccw_ir_type_name(ccw_ir_type t);
bool        ccw_ir_type_parse(const char *name, ccw_ir_type *out);

/* ---------- node kinds ---------- */

typedef enum {
    CCW_NODE_DEAD = 0,   /* deleted node; id never reused */
    CCW_NODE_FUNCTION,
    CCW_NODE_BLOCK,
    CCW_NODE_INSTR,
    CCW_NODE_OPERAND
} ccw_node_kind;

typedef enum {
    CCW_OPND_REG = 0,    /* %name       */
    CCW_OPND_CONST_INT,  /* (iconst T n) */
    CCW_OPND_CONST_FLOAT,/* (fconst T x) */
    CCW_OPND_FUNC,       /* @name       */
    CCW_OPND_BLOCK       /* ^name       */
} ccw_operand_kind;

/* ---------- module lifecycle ---------- */

ccw_ir     *ccw_ir_module_create(const char *name, ccw_profile profile);
void        ccw_ir_module_destroy(ccw_ir *ir);
const char *ccw_ir_module_name(const ccw_ir *ir);
ccw_profile ccw_ir_module_profile(const ccw_ir *ir);

/* ---------- attributes (key/value, ordered) ---------- */

/* owner == 0 addresses the module itself. */
ccw_status  ccw_ir_attr_set(ccw_ir *ir, ccw_node owner,
                            const char *key, const char *value);
int         ccw_ir_attr_count(const ccw_ir *ir, ccw_node owner);
const char *ccw_ir_attr_key(const ccw_ir *ir, ccw_node owner, int idx);
const char *ccw_ir_attr_value(const ccw_ir *ir, ccw_node owner, int idx);
const char *ccw_ir_attr_lookup(const ccw_ir *ir, ccw_node owner, const char *key);

/* ---------- construction ---------- */

ccw_node ccw_ir_function_add(ccw_ir *ir, const char *name, ccw_ir_type result);
ccw_status ccw_ir_function_add_param(ccw_ir *ir, ccw_node fn,
                                     ccw_ir_type type, const char *name);
ccw_node ccw_ir_block_add(ccw_ir *ir, ccw_node fn, const char *name);

/* Detached instruction; splice with the mutation calls below. */
ccw_node ccw_ir_instr_build(ccw_ir *ir, const char *opcode, ccw_ir_type type);
ccw_status ccw_ir_instr_set_dest(ccw_ir *ir, ccw_node ins, const char *dest);
ccw_status ccw_ir_instr_add_operand(ccw_ir *ir, ccw_node ins, ccw_node operand);
ccw_status ccw_ir_block_append_instr(ccw_ir *ir, ccw_node blk, ccw_node ins);

ccw_node ccw_ir_operand_reg(ccw_ir *ir, const char *name);
ccw_node ccw_ir_operand_func(ccw_ir *ir, const char *name);
ccw_node ccw_ir_operand_block(ccw_ir *ir, const char *name);
ccw_node ccw_ir_operand_const_int(ccw_ir *ir, ccw_ir_type type, int64_t value);
ccw_node ccw_ir_operand_const_float(ccw_ir *ir, ccw_ir_type type, double value);

/* ---------- navigation / inspection (mirrors the Core Accessor Set) ---------- */

int      ccw_ir_function_count(const ccw_ir *ir);
ccw_node ccw_ir_function_ref(const ccw_ir *ir, int idx);
const char *ccw_ir_function_name(const ccw_ir *ir, ccw_node fn);
ccw_ir_type ccw_ir_function_result(const ccw_ir *ir, ccw_node fn);
int      ccw_ir_function_param_count(const ccw_ir *ir, ccw_node fn);
const char *ccw_ir_function_param_name(const ccw_ir *ir, ccw_node fn, int idx);
ccw_ir_type ccw_ir_function_param_type(const ccw_ir *ir, ccw_node fn, int idx);
int      ccw_ir_function_block_count(const ccw_ir *ir, ccw_node fn);
ccw_node ccw_ir_function_block_ref(const ccw_ir *ir, ccw_node fn, int idx);

const char *ccw_ir_block_name(const ccw_ir *ir, ccw_node blk);
int      ccw_ir_block_instr_count(const ccw_ir *ir, ccw_node blk);
ccw_node ccw_ir_block_instr_ref(const ccw_ir *ir, ccw_node blk, int idx);

const char *ccw_ir_instr_opcode(const ccw_ir *ir, ccw_node ins);
const char *ccw_ir_instr_dest(const ccw_ir *ir, ccw_node ins);  /* NULL if none */
ccw_ir_type ccw_ir_instr_type(const ccw_ir *ir, ccw_node ins);
int      ccw_ir_instr_operand_count(const ccw_ir *ir, ccw_node ins);
ccw_node ccw_ir_instr_operand(const ccw_ir *ir, ccw_node ins, int idx);

ccw_node_kind    ccw_ir_node_kind(const ccw_ir *ir, ccw_node n);
ccw_operand_kind ccw_ir_operand_kind(const ccw_ir *ir, ccw_node n);
bool     ccw_ir_operand_is_const(const ccw_ir *ir, ccw_node n);
ccw_status ccw_ir_const_int_value(const ccw_ir *ir, ccw_node n, int64_t *out);
ccw_status ccw_ir_const_float_value(const ccw_ir *ir, ccw_node n, double *out);
const char *ccw_ir_operand_name(const ccw_ir *ir, ccw_node n);
ccw_ir_type ccw_ir_operand_type(const ccw_ir *ir, ccw_node n);

/* ---------- mutation (the three structural edits, §GlueSTD mutation) ---------- */

ccw_status ccw_ir_instr_replace(ccw_ir *ir, ccw_node old_ins, ccw_node new_ins);
ccw_status ccw_ir_instr_insert_before(ccw_ir *ir, ccw_node anchor, ccw_node new_ins);
ccw_status ccw_ir_instr_delete(ccw_ir *ir, ccw_node ins);

/* ---------- text serialization (§5.4, round-trip REQUIRED) ---------- */

/* Returns malloc'd UTF-8 text; caller frees. NULL on failure. */
char      *ccw_ir_print(const ccw_ir *ir);
/* Parses text into a fresh module. On failure returns NULL and sets
 * *error_message (malloc'd; caller frees). */
ccw_ir    *ccw_ir_parse(const char *text, char **error_message);
ccw_ir    *ccw_ir_parse_file(const char *path, char **error_message);

/* Structural equality over the canonical form (ids excluded). */
bool       ccw_ir_equal(const ccw_ir *a, const ccw_ir *b);

/* ---------- validation (core + profile, §5.3) ---------- */

ccw_status ccw_ir_validate(const ccw_ir *ir, char **error_message);

#ifdef __cplusplus
}
#endif
#endif /* CCW_IR_H */
