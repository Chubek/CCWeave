;;; stdrewrite/cmp/canonical/rules.scm
;;; Comparison polarity and operand-order normalization.

(ruleset cmp.canonical)

(rule gt-to-lt-swapped
      (icmp-gt ?x ?y) (icmp-lt ?y ?x)
      :bidirectional #t)

(rule ge-to-le-swapped
      (icmp-ge ?x ?y) (icmp-le ?y ?x)
      :bidirectional #t)

(rule le-to-ge-swapped
      (icmp-le ?x ?y) (icmp-ge ?y ?x)
      :bidirectional #t)

(rule lt-to-gt-swapped
      (icmp-lt ?x ?y) (icmp-gt ?y ?x)
      :bidirectional #t)

(rule eq-commutes
      (icmp-eq ?x ?y) (icmp-eq ?y ?x)
      :bidirectional #t)

(rule ne-commutes
      (icmp-ne ?x ?y) (icmp-ne ?y ?x)
      :bidirectional #t)

(rule not-eq-is-ne
      (not (icmp-eq ?x ?y)) (icmp-ne ?x ?y)
      :bidirectional #t)

(rule not-ne-is-eq
      (not (icmp-ne ?x ?y)) (icmp-eq ?x ?y)
      :bidirectional #t)

(rule not-lt-is-ge
      (not (icmp-lt ?x ?y)) (icmp-ge ?x ?y)
      :bidirectional #t)

(rule not-le-is-gt
      (not (icmp-le ?x ?y)) (icmp-gt ?x ?y)
      :bidirectional #t)

(rule not-gt-is-le
      (not (icmp-gt ?x ?y)) (icmp-le ?x ?y)
      :bidirectional #t)

(rule not-ge-is-lt
      (not (icmp-ge ?x ?y)) (icmp-lt ?x ?y)
      :bidirectional #t)
