;;; stdrewrite/arith/reassociate/rules.scm
;;; Reassociates commutative integer chains to group constants.

(ruleset arith.reassociate)

(rule add-reassoc-left
      (iadd (iadd ?x ?y) ?z) (iadd ?x (iadd ?y ?z))
      :bidirectional #t)

(rule add-reassoc-right
      (iadd ?x (iadd ?y ?z)) (iadd (iadd ?x ?y) ?z)
      :bidirectional #t)

(rule mul-reassoc-left
      (imul (imul ?x ?y) ?z) (imul ?x (imul ?y ?z))
      :bidirectional #t)

(rule mul-reassoc-right
      (imul ?x (imul ?y ?z)) (imul (imul ?x ?y) ?z)
      :bidirectional #t)

(rule add-const-group
      (iadd (iadd ?x (iconst ?a)) (iconst ?b))
      (iadd ?x (iconst (+ ?a ?b)))
      :bidirectional #f)

(rule mul-const-group
      (imul (imul ?x (iconst ?a)) (iconst ?b))
      (imul ?x (iconst (* ?a ?b)))
      :bidirectional #f)

(rule add-pull-const-left
      (iadd ?x (iadd (iconst ?a) ?y))
      (iadd (iconst ?a) (iadd ?x ?y))
      :bidirectional #t)

(rule add-pull-const-right
      (iadd (iadd ?x (iconst ?a)) ?y)
      (iadd (iadd ?x ?y) (iconst ?a))
      :bidirectional #t)

(rule mul-pull-const-left
      (imul ?x (imul (iconst ?a) ?y))
      (imul (iconst ?a) (imul ?x ?y))
      :bidirectional #t)

(rule mul-pull-const-right
      (imul (imul ?x (iconst ?a)) ?y)
      (imul (imul ?x ?y) (iconst ?a))
      :bidirectional #t)

(rule add-pull-const-left-2
      (iadd (iadd (iconst ?a) ?x) ?y)
      (iadd (iconst ?a) (iadd ?x ?y))
      :bidirectional #t)

(rule mul-pull-const-left-2
      (imul (imul (iconst ?a) ?x) ?y)
      (imul (iconst ?a) (imul ?x ?y))
      :bidirectional #t)
