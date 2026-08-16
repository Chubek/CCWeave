;;; stdrewrite/arith/constant-fold/rules.scm
;;; Folds integer add/sub/mul/neg on literal operands.

(ruleset arith.constant-fold)

(rule add-consts
      (iadd (iconst ?a) (iconst ?b)) (iconst (+ ?a ?b))
      :bidirectional #f)

(rule sub-consts
      (isub (iconst ?a) (iconst ?b)) (iconst (- ?a ?b))
      :bidirectional #f)

(rule mul-consts
      (imul (iconst ?a) (iconst ?b)) (iconst (* ?a ?b))
      :bidirectional #f)

(rule neg-const
      (ineg (iconst ?a)) (iconst (- ?a))
      :bidirectional #f)

(rule add-const-left
      (iadd (iconst ?a) ?x) (iadd ?x (iconst ?a))
      :bidirectional #t)

(rule mul-const-left
      (imul (iconst ?a) ?x) (imul ?x (iconst ?a))
      :bidirectional #t)

(rule add-assoc-consts
      (iadd (iadd ?x (iconst ?a)) (iconst ?b))
      (iadd ?x (iconst (+ ?a ?b)))
      :bidirectional #f)

(rule add-assoc-consts-left
      (iadd (iconst ?a) (iadd ?x (iconst ?b)))
      (iadd ?x (iconst (+ ?a ?b)))
      :bidirectional #f)

(rule mul-assoc-consts
      (imul (imul ?x (iconst ?a)) (iconst ?b))
      (imul ?x (iconst (* ?a ?b)))
      :bidirectional #f)

(rule mul-assoc-consts-left
      (imul (iconst ?a) (imul ?x (iconst ?b)))
      (imul ?x (iconst (* ?a ?b)))
      :bidirectional #f)

(rule sub-const-left
      (isub (iconst ?a) ?x) (isub (iconst ?a) ?x)
      :bidirectional #t)

(rule add-const-right-prop
      (iadd (iadd ?x (iconst ?a)) ?y)
      (iadd (iadd ?x ?y) (iconst ?a))
      :bidirectional #t)

(rule mul-const-right-prop
      (imul (imul ?x (iconst ?a)) ?y)
      (imul (imul ?x ?y) (iconst ?a))
      :bidirectional #t)

(rule neg-neg-const
      (ineg (ineg (iconst ?a))) (iconst ?a)
      :bidirectional #f)

(rule div-consts
      (idiv (iconst ?a) (iconst ?b)) (iconst (quotient ?a ?b))
      :bidirectional #f)

(rule rem-consts
      (irem (iconst ?a) (iconst ?b)) (iconst (remainder ?a ?b))
      :bidirectional #f)
