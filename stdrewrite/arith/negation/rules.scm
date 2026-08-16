;;; stdrewrite/arith/negation/rules.scm
;;; Double negation, negation of subtraction, and sign-flip normalization.

(ruleset arith.negation)

(rule double-negate
      (ineg (ineg ?x)) ?x
      :bidirectional #f)

(rule neg-of-add
      (ineg (iadd ?x ?y)) (iadd (ineg ?x) (ineg ?y))
      :bidirectional #t)

(rule neg-of-sub
      (ineg (isub ?x ?y)) (isub ?y ?x)
      :bidirectional #t)

(rule neg-of-mul-left
      (ineg (imul ?x ?y)) (imul (ineg ?x) ?y)
      :bidirectional #t)

(rule neg-of-mul-right
      (ineg (imul ?x ?y)) (imul ?x (ineg ?y))
      :bidirectional #t)

(rule sub-to-add-neg
      (isub ?x ?y) (iadd ?x (ineg ?y))
      :bidirectional #t)

(rule add-neg-to-sub
      (iadd ?x (ineg ?y)) (isub ?x ?y)
      :bidirectional #t)

(rule neg-of-shl
      (ineg (shl ?x (iconst ?k))) (shl (ineg ?x) (iconst ?k))
      :bidirectional #t)
