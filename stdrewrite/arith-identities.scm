;;; stdrewrite/arith-identities.scm
;;; Integer arithmetic identities. Every rule is a semantics-preserving
;;; equivalence (§7.2): no rule repairs incorrect code.

(ruleset arith.identities)

(rule add-zero
      (iadd ?x (iconst 0)) ?x
      :bidirectional #f)

(rule sub-zero
      (isub ?x (iconst 0)) ?x
      :bidirectional #f)

(rule mul-one
      (imul ?x (iconst 1)) ?x
      :bidirectional #f)

(rule mul-zero
      (imul ?x (iconst 0)) (iconst 0)
      :bidirectional #f)

(rule add-commutes
      (iadd ?x ?y) (iadd ?y ?x)
      :bidirectional #t)

(rule mul-commutes
      (imul ?x ?y) (imul ?y ?x)
      :bidirectional #t)
