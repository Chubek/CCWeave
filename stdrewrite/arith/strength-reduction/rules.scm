;;; stdrewrite/arith/strength-reduction/rules.scm
;;; Multiplies and divides by powers of two into shifts;
;;; small-constant multiply into shift-add. Side conditions check
;;; that the constant is a power of two or small integer.

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

(rule mul-32-shl-5
      (imul ?x (iconst 32)) (shl ?x (iconst 5))
      :bidirectional #t)

(rule mul-64-shl-6
      (imul ?x (iconst 64)) (shl ?x (iconst 6))
      :bidirectional #t)

(rule add-self-shl-1
      (iadd ?x ?x) (shl ?x (iconst 1))
      :bidirectional #t)

(rule mul-3-shift-add
      (imul ?x (iconst 3))
      (iadd (shl ?x (iconst 1)) ?x)
      :bidirectional #t)

(rule mul-5-shift-add
      (imul ?x (iconst 5))
      (iadd (shl ?x (iconst 2)) ?x)
      :bidirectional #t)

(rule mul-6-shift-add
      (imul ?x (iconst 6))
      (iadd (shl (shl ?x (iconst 1)) (iconst 1))
            (shl ?x (iconst 1)))
      :bidirectional #t)

(rule mul-9-shift-add
      (imul ?x (iconst 9))
      (iadd (shl ?x (iconst 3)) ?x)
      :bidirectional #t)

(rule mul-10-shift-add
      (imul ?x (iconst 10))
      (iadd (shl ?x (iconst 3)) (shl ?x (iconst 1)))
      :bidirectional #t)

(rule mul-12-shift-add
      (imul ?x (iconst 12))
      (iadd (shl ?x (iconst 3)) (shl ?x (iconst 2)))
      :bidirectional #t)

(rule mul-7-shift-sub
      (imul ?x (iconst 7))
      (isub (shl ?x (iconst 3)) ?x)
      :bidirectional #t)
