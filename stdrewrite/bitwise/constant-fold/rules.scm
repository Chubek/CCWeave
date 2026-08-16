;;; stdrewrite/bitwise/constant-fold/rules.scm
;;; Folds and/or/xor/not on literal operands.

(ruleset bitwise.constant-fold)

(rule and-consts
      (and (iconst ?a) (iconst ?b)) (iconst (logand ?a ?b))
      :bidirectional #f)

(rule or-consts
      (or (iconst ?a) (iconst ?b)) (iconst (logior ?a ?b))
      :bidirectional #f)

(rule xor-consts
      (xor (iconst ?a) (iconst ?b)) (iconst (logxor ?a ?b))
      :bidirectional #f)

(rule not-const
      (not (iconst ?a)) (iconst (lognot ?a))
      :bidirectional #f)

(rule and-const-commute
      (and (iconst ?a) ?x) (and ?x (iconst ?a))
      :bidirectional #t)

(rule or-const-commute
      (or (iconst ?a) ?x) (or ?x (iconst ?a))
      :bidirectional #t)

(rule xor-const-commute
      (xor (iconst ?a) ?x) (xor ?x (iconst ?a))
      :bidirectional #t)

(rule and-assoc-consts
      (and (and ?x (iconst ?a)) (iconst ?b))
      (and ?x (iconst (logand ?a ?b)))
      :bidirectional #f)

(rule or-assoc-consts
      (or (or ?x (iconst ?a)) (iconst ?b))
      (or ?x (iconst (logior ?a ?b)))
      :bidirectional #f)

(rule xor-assoc-consts
      (xor (xor ?x (iconst ?a)) (iconst ?b))
      (xor ?x (iconst (logxor ?a ?b)))
      :bidirectional #f)

(rule and-const-self
      (and (iconst ?a) (iconst ?a)) (iconst ?a)
      :bidirectional #f)

(rule or-const-self
      (or (iconst ?a) (iconst ?a)) (iconst ?a)
      :bidirectional #f)
