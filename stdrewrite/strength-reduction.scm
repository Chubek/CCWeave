;;; stdrewrite/strength-reduction.scm
;;; Multiply/divide by powers of two as shifts. Equivalences only:
;;; each pair denotes the same value, so extraction (not rule order)
;;; decides which form survives.

(ruleset arith.strength-reduction)

(rule mul-2-shl-1
      (imul ?x (iconst 2)) (shl ?x (iconst 1))
      :bidirectional #t)

(rule mul-4-shl-2
      (imul ?x (iconst 4)) (shl ?x (iconst 2))
      :bidirectional #t)

(rule mul-8-shl-3
      (imul ?x (iconst 8)) (shl ?x (iconst 3))
      :bidirectional #t)

(rule mul-16-shl-4
      (imul ?x (iconst 16)) (shl ?x (iconst 4))
      :bidirectional #t)

(rule add-self-shl-1
      (iadd ?x ?x) (shl ?x (iconst 1))
      :bidirectional #t)
