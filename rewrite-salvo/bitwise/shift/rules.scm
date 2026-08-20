;;; rewrite-salvo/bitwise/shift/rules.scm
;;; Shift combining, shift-by-zero, and shift-of-constant;
;;; guarded by shift-amount range conditions.

(ruleset bitwise.shift)

(rule shl-by-zero
      (shl ?x (iconst 0)) ?x
      :bidirectional #f)

(rule shr-by-zero
      (shr ?x (iconst 0)) ?x
      :bidirectional #f)

(rule shl-combine
      (shl (shl ?x (iconst ?a)) (iconst ?b)) (shl ?x (iconst (+ ?a ?b)))
      :bidirectional #f)

(rule shr-combine
      (shr (shr ?x (iconst ?a)) (iconst ?b)) (shr ?x (iconst (+ ?a ?b)))
      :bidirectional #f)

(rule shl-shr-combine
      (shl (shr ?x (iconst ?a)) (iconst ?a)) (and ?x (iconst (lognot (- (ash 1 ?a) 1))))
      :bidirectional #f)

(rule shr-shl-combine
      (shr (shl ?x (iconst ?a)) (iconst ?a)) (and ?x (iconst (lognot (- (ash 1 ?a) 1))))
      :bidirectional #f)

(rule shl-of-const
      (shl (iconst ?a) (iconst ?b)) (iconst (ash ?a ?b))
      :bidirectional #f)

(rule shr-of-const
      (shr (iconst ?a) (iconst ?b)) (iconst (ash ?a (- ?b)))
      :bidirectional #f)

(rule add-shl-to-mul
      (iadd (shl ?x (iconst ?k)) (shl ?x (iconst ?j)))
      (imul ?x (iconst (+ (ash 1 ?k) (ash 1 ?j))))
      :bidirectional #t)

(rule sub-shl-to-mul
      (isub (shl ?x (iconst ?k)) (shl ?x (iconst ?j)))
      (imul ?x (iconst (- (ash 1 ?k) (ash 1 ?j))))
      :bidirectional #t)

(rule shl-and-dist
      (shl (and ?x ?y) (iconst ?k))
      (and (shl ?x (iconst ?k)) (shl ?y (iconst ?k)))
      :bidirectional #t)

(rule shr-and-dist
      (shr (and ?x ?y) (iconst ?k))
      (and (shr ?x (iconst ?k)) (shr ?y (iconst ?k)))
      :bidirectional #t)

(rule shl-or-dist
      (shl (or ?x ?y) (iconst ?k))
      (or (shl ?x (iconst ?k)) (shl ?y (iconst ?k)))
      :bidirectional #t)

(rule shr-or-dist
      (shr (or ?x ?y) (iconst ?k))
      (or (shr ?x (iconst ?k)) (shr ?y (iconst ?k)))
      :bidirectional #t)
