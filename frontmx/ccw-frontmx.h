#ifndef CCW_FRONTMX_H
#define CCW_FRONTMX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FMX FMX;
typedef struct FMXNode FMXNode;
typedef struct FMXAttr FMXAttr;
typedef struct FMXCursor FMXCursor;

typedef enum {
  FMX_OK = 0,
  FMX_INVALID_ARGUMENT = 1,
  FMX_PARSE_ERROR = 2,
  FMX_SCHEMA_ERROR = 3,
  FMX_REGEX_ERROR = 4,
  FMX_IO_ERROR = 5,
  FMX_GENERATE_ERROR = 6
} fmx_status;

typedef struct {
  const char *message; /* owned by caller; release with frontmx_error_free */
  size_t offset;
} fmx_error;

void frontmx_error_free(fmx_error *error);
FMX *frontmx_parse(const char *source, size_t length, fmx_error *error);
FMX *frontmx_parse_file(const char *path, fmx_error *error);
void frontmx_free(FMX *fmx);

const char *frontmx_language(const FMX *fmx);
int frontmx_grammar_version(const FMX *fmx);
const char *frontmx_entry(const FMX *fmx);
size_t frontmx_terminal_count(const FMX *fmx);
size_t frontmx_production_count(const FMX *fmx);
size_t frontmx_limitation_count(const FMX *fmx);
size_t frontmx_semantic_count(const FMX *fmx);
size_t frontmx_rewrite_count(const FMX *fmx);

const FMXNode *frontmx_terminal(const FMX *fmx, size_t index);
const FMXNode *frontmx_production(const FMX *fmx, size_t index);
const FMXNode *frontmx_limitation(const FMX *fmx, size_t index);
const FMXNode *frontmx_semantic(const FMX *fmx, size_t index);
const FMXNode *frontmx_rewrite(const FMX *fmx, size_t index);

const char *frontmx_node_name(const FMXNode *node);
const char *frontmx_node_lhs(const FMXNode *node);
const char *frontmx_node_regex(const FMXNode *node);
const char *frontmx_node_rhs(const FMXNode *node);
const char *frontmx_node_attr(const FMXNode *node);
const char *frontmx_node_pattern(const FMXNode *node);
const char *frontmx_node_reference(const FMXNode *node);
const char *frontmx_node_violation(const FMXNode *node);
int frontmx_terminal_matches(const FMXNode *terminal, const char *text,
                             size_t length);

/* Emit parser/AST/attribute/walker interface artifacts into output_dir. */
fmx_status frontmx_generate(const FMX *fmx, const char *output_dir,
                            fmx_error *error);
const char *frontmx_last_error(const FMX *fmx);

#ifdef __cplusplus
}
#endif
#endif
