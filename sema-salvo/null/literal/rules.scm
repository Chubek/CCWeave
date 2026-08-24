;;; sema-salvo/null/literal/rules.scm
;;; Semantic rules for sema.null.literal.

(sema-ruleset sema.null.literal)

(sema-rule literal
  :description "Null literal is definitely null"
  :trigger (null-lit)
  :target (assert (nullability (null-lit) null))
  :gating #t)

(sema-rule string-lit
  :description "String literal address is non-null"
  :trigger (str-lit $s)
  :target (assert (nullability (str-lit $s) non-null))
  :gating #t)

