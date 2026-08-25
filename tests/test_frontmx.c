#include "ccw-frontmx.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  const char *s =
      "(frontmx (language Moonix) (grammar-version 1) "
      "(terminals (int (regex \"[0-9]+\") (attr (type i64))) "
      "(plus (regex \"\\\\+\"))) "
      "(productions (rule (name expr.int) (lhs expr) (rhs (int $v)) "
      "(attr (type i64)))) "
      "(semantics (action (use sema.range.int) (on (int $v)))) "
      "(rewrites (use rewrite.add-zero)) (entry (node expr)))";
  fmx_error e;
  FMX *f = frontmx_parse(s, strlen(s), &e);
  if (!f) {
    fprintf(stderr, "%s\n", e.message ? e.message : "parse failed");
    frontmx_error_free(&e);
    return 1;
  }
  int ok = strcmp(frontmx_language(f), "Moonix") == 0 &&
           frontmx_terminal_count(f) == 2 &&
           frontmx_production_count(f) == 1 &&
           frontmx_semantic_count(f) == 1 &&
           frontmx_rewrite_count(f) == 1 &&
           strcmp(frontmx_entry(f), "expr") == 0 &&
           frontmx_terminal_matches(frontmx_terminal(f, 0), "123", 3);
  frontmx_free(f);
  frontmx_error_free(&e);
  return ok ? 0 : 2;
}
