;;; stdrewrite/norm/constant-right/rules.scm
;;; Moves literal operands to the right of commutative operations.

(ruleset norm.constant-right)

(rule add-const-right
      (iadd (iconst ?a) ?x) (iadd ?x (iconst ?a))
      :bidirectional #f)

(rule mul-const-right
      (imul (iconst ?a) ?x) (imul ?x (iconst ?a))
      :bidirectional #f)

(rule and-const-right
      (and (iconst ?a) ?x) (and ?x (iconst ?a))
      :bidirectional #f)

(rule or-const-right
      (or (iconst ?a) ?x) (or ?x (iconst ?a))
      :bidirectional #f)
