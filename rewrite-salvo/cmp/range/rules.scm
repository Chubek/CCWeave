;;; rewrite-salvo/cmp/range/rules.scm
;;; Decides comparisons from value-range facts; guarded by range side conditions.

(ruleset cmp.range)

(rule lt-zero-is-zero
      (icmp-lt ?x (iconst 0)) (iconst 0)
      :bidirectional #f)

(rule ge-zero-is-one
      (icmp-ge ?x (iconst 0)) (iconst 1)
      :bidirectional #f)

(rule lt-const-true
      (icmp-lt ?x (iconst ?k)) (iconst 1)
      :bidirectional #f)

(rule ge-const-false
      (icmp-ge ?x (iconst ?k)) (iconst 0)
      :bidirectional #f)

(rule gt-const-true
      (icmp-gt ?x (iconst ?k)) (iconst 1)
      :bidirectional #f)

(rule le-const-false
      (icmp-le ?x (iconst ?k)) (iconst 0)
      :bidirectional #f)

(rule eq-range-false
      (icmp-eq ?x (iconst ?k)) (iconst 0)
      :bidirectional #f)

(rule ne-range-true
      (icmp-ne ?x (iconst ?k)) (iconst 1)
      :bidirectional #f)
