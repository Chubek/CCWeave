;;; stdrewrite/cmp/constant-fold/rules.scm
;;; Folds comparisons of literal operands to boolean constants.

(ruleset cmp.constant-fold)

(rule icmp-eq-consts
      (icmp-eq (iconst ?a) (iconst ?b)) (iconst (if (= ?a ?b) 1 0))
      :bidirectional #f)

(rule icmp-ne-consts
      (icmp-ne (iconst ?a) (iconst ?b)) (iconst (if (not (= ?a ?b)) 1 0))
      :bidirectional #f)

(rule icmp-lt-consts
      (icmp-lt (iconst ?a) (iconst ?b)) (iconst (if (< ?a ?b) 1 0))
      :bidirectional #f)

(rule icmp-le-consts
      (icmp-le (iconst ?a) (iconst ?b)) (iconst (if (<= ?a ?b) 1 0))
      :bidirectional #f)

(rule icmp-gt-consts
      (icmp-gt (iconst ?a) (iconst ?b)) (iconst (if (> ?a ?b) 1 0))
      :bidirectional #f)

(rule icmp-ge-consts
      (icmp-ge (iconst ?a) (iconst ?b)) (iconst (if (>= ?a ?b) 1 0))
      :bidirectional #f)

(rule icmp-eq-same
      (icmp-eq ?x ?x) (iconst 1)
      :bidirectional #f)

(rule icmp-ne-same
      (icmp-ne ?x ?x) (iconst 0)
      :bidirectional #f)

(rule icmp-lt-same
      (icmp-lt ?x ?x) (iconst 0)
      :bidirectional #f)

(rule icmp-le-same
      (icmp-le ?x ?x) (iconst 1)
      :bidirectional #f)
