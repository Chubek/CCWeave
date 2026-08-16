;;; stdrewrite/float/constant-fold/rules.scm
;;; Folds float arithmetic on literals with round-to-nearest-even semantics.

(ruleset float.constant-fold)

(rule fadd-consts
      (fadd (fconst ?a) (fconst ?b)) (fconst (+ ?a ?b))
      :bidirectional #f)

(rule fsub-consts
      (fsub (fconst ?a) (fconst ?b)) (fconst (- ?a ?b))
      :bidirectional #f)

(rule fmul-consts
      (fmul (fconst ?a) (fconst ?b)) (fconst (* ?a ?b))
      :bidirectional #f)

(rule fdiv-consts
      (fdiv (fconst ?a) (fconst ?b)) (fconst (/ ?a ?b))
      :bidirectional #f)

(rule fneg-const
      (fneg (fconst ?a)) (fconst (- ?a))
      :bidirectional #f)

(rule fcmp-eq-consts
      (fcmp-eq (fconst ?a) (fconst ?b)) (iconst (if (= ?a ?b) 1 0))
      :bidirectional #f)

(rule fcmp-lt-consts
      (fcmp-lt (fconst ?a) (fconst ?b)) (iconst (if (< ?a ?b) 1 0))
      :bidirectional #f)

(rule fcmp-le-consts
      (fcmp-le (fconst ?a) (fconst ?b)) (iconst (if (<= ?a ?b) 1 0))
      :bidirectional #f)
