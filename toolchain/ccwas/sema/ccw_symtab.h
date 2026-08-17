#ifndef CCWAS_SYMTAB_H
#define CCWAS_SYMTAB_H
/* §5: symbol table construction — khash-backed, first-occurrence ordering */

#include "ccw_types.h"
#include "khash.h"
#include "kvec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Symbol table --- */
ccw_symbol_t *ccw_symtab_lookup(khash_t(ccw_sym) *tab, const char *name);
ccw_symbol_t *ccw_symtab_define(khash_t(ccw_sym) *tab, const char *name,
                                 uint64_t value, int section, int binding);
void ccw_symtab_destroy(khash_t(ccw_sym) *tab);

/* --- Expression evaluation --- */
int ccw_expr_parse(const char *s, ccw_expr_t **out, char **error);
int64_t ccw_expr_eval(const ccw_expr_t *e, const khash_t(ccw_sym) *symtab,
                      uint64_t pc, int *ok);
void ccw_expr_free(ccw_expr_t *e);

/* --- Operand helpers --- */
void ccw_operand_free(ccw_operand_t *op);
ccw_operand_t ccw_operand_copy(const ccw_operand_t *src);

/* --- Statement helpers --- */
void ccw_stmt_free(ccw_stmt_t *s);

/* --- Unit helpers --- */
void ccw_unit_init(ccw_unit_t *u, ccw_arch_t arch, const char *syntax);
void ccw_unit_destroy(ccw_unit_t *u);
int ccw_unit_add_section(ccw_unit_t *u, const char *name, int type, uint64_t flags);
void ccw_unit_emit_byte(ccw_unit_t *u, int sidx, uint8_t b);
void ccw_unit_emit_bytes(ccw_unit_t *u, int sidx, const uint8_t *data, size_t len);
void ccw_unit_emit_reloc(ccw_unit_t *u, ccw_reloc_type_t type, uint64_t offset,
                          uint64_t addend, const char *symbol, int section);

/* --- ISA validation --- */
int ccw_isa_validate(ccw_unit_t *u, const char *mnemonic, ccw_form_t *form_out,
                     char **error);

/* --- Extension management --- */
int ccw_ext_enable(ccw_unit_t *u, const char *ext);
int ccw_ext_disable(ccw_unit_t *u, const char *ext);
int ccw_ext_is_enabled(ccw_unit_t *u, const char *ext);

#ifdef __cplusplus
}
#endif

#endif /* CCWAS_SYMTAB_H */
