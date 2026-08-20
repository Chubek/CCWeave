;;; rewrite-salvo/bitwise/mask/rules.scm
;;; Mask composition, redundant-mask removal, and mask-then-shift reordering.

(ruleset bitwise.mask)

(rule mask-compose
      (and (and ?x (iconst ?a)) (iconst ?b)) (and ?x (iconst (logand ?a ?b)))
      :bidirectional #f)

(rule mask-redundant
      (and (and ?x (iconst ?a)) (iconst ?a)) (and ?x (iconst ?a))
      :bidirectional #f)

(rule mask-subset
      (and (and ?x (iconst ?a)) (iconst ?b))
      (and ?x (iconst ?b))
      :bidirectional #f)

(rule shift-before-mask
      (and (shl ?x (iconst ?k)) (iconst ?m))
      (shl (and ?x (iconst (ash ?m (- ?k)))) (iconst ?k))
      :bidirectional #t)

(rule mask-before-shift
      (shl (and ?x (iconst ?a)) (iconst ?k))
      (and (shl ?x (iconst ?k)) (iconst (ash ?a ?k)))
      :bidirectional #t)

(rule mask-after-shift
      (and (shr ?x (iconst ?k)) (iconst ?m))
      (shr (and ?x (iconst (ash ?m ?k))) (iconst ?k))
      :bidirectional #t)

(rule sign-extend-mask
      (and (shl ?x (iconst ?k)) (iconst ?m))
      (shl (and ?x (iconst (ash ?m (- ?k)))) (iconst ?k))
      :bidirectional #t)

(rule mask-zero-ext
      (and ?x (iconst ?m)) (and ?x (iconst ?m))
      :bidirectional #t)

(rule or-mask-compose
      (or (and ?x (iconst ?a)) (and ?x (iconst ?b)))
      (and ?x (iconst (logior ?a ?b)))
      :bidirectional #t)

(rule and-mask-dist-const
      (and (and ?x (iconst ?a)) (and ?y (iconst ?b)))
      (and (and ?x ?y) (iconst (logand ?a ?b)))
      :bidirectional #t)
