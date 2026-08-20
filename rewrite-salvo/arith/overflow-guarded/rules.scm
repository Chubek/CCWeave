;;; rewrite-salvo/arith/overflow-guarded/rules.scm
;;; Rewrites valid only under proven no-overflow; guarded by value-range
;;; side conditions. Every rule is still an equivalence under the guard.

(ruleset arith.overflow-guarded)

(rule add-assoc-no-overflow
      (iadd (iadd ?x ?y) ?z) (iadd ?x (iadd ?y ?z))
      :bidirectional #t)

(rule add-commute-no-overflow
      (iadd ?x ?y) (iadd ?y ?x)
      :bidirectional #t)

(rule mul-assoc-no-overflow
      (imul (imul ?x ?y) ?z) (imul ?x (imul ?y ?z))
      :bidirectional #t)

(rule mul-commute-no-overflow
      (imul ?x ?y) (imul ?y ?x)
      :bidirectional #t)

(rule add-cancel-left-no-overflow
      (isub (iadd ?x ?y) ?y) ?x
      :bidirectional #f)

(rule add-cancel-right-no-overflow
      (isub (iadd ?x ?y) ?x) ?y
      :bidirectional #f)

(rule sub-add-cancel-no-overflow
      (iadd (isub ?x ?y) ?y) ?x
      :bidirectional #f)

(rule mul-div-cancel-no-overflow
      (idiv (imul ?x ?y) ?y) ?x
      :bidirectional #f)

(rule div-mul-cancel-no-overflow
      (imul (idiv ?x ?y) ?y) ?x
      :bidirectional #f)

(rule add-dist-mul-no-overflow
      (iadd (imul ?x ?y) (imul ?x ?z)) (imul ?x (iadd ?y ?z))
      :bidirectional #t)

(rule mul-dist-add-no-overflow
      (imul ?x (iadd ?y ?z)) (iadd (imul ?x ?y) (imul ?x ?z))
      :bidirectional #t)

(rule neg-abs-no-overflow
      (isub (iconst 0) ?x) (ineg ?x)
      :bidirectional #t)
