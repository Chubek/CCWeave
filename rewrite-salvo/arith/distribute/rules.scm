;;; rewrite-salvo/arith/distribute/rules.scm
;;; Distribution and factoring of multiplication over addition.

(ruleset arith.distribute)

(rule mul-dist-add
      (imul ?x (iadd ?y ?z)) (iadd (imul ?x ?y) (imul ?x ?z))
      :bidirectional #t)

(rule mul-dist-add-left
      (imul (iadd ?y ?z) ?x) (iadd (imul ?y ?x) (imul ?z ?x))
      :bidirectional #t)

(rule mul-dist-sub
      (imul ?x (isub ?y ?z)) (isub (imul ?x ?y) (imul ?x ?z))
      :bidirectional #t)

(rule mul-dist-sub-left
      (imul (isub ?y ?z) ?x) (isub (imul ?y ?x) (imul ?z ?x))
      :bidirectional #t)

(rule factor-add
      (iadd (imul ?x ?y) (imul ?x ?z)) (imul ?x (iadd ?y ?z))
      :bidirectional #t)

(rule factor-add-left
      (iadd (imul ?y ?x) (imul ?z ?x)) (imul (iadd ?y ?z) ?x)
      :bidirectional #t)

(rule factor-sub
      (isub (imul ?x ?y) (imul ?x ?z)) (imul ?x (isub ?y ?z))
      :bidirectional #t)

(rule factor-sub-left
      (isub (imul ?y ?x) (imul ?z ?x)) (imul (isub ?y ?z) ?x)
      :bidirectional #t)

(rule neg-dist
      (ineg (imul ?x ?y)) (imul (ineg ?x) ?y)
      :bidirectional #t)

(rule neg-dist-alt
      (imul (ineg ?x) ?y) (ineg (imul ?x ?y))
      :bidirectional #t)
