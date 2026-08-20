;;; rewrite-salvo/norm/compare-polarity/rules.scm
;;; Normalizes negated comparisons to positive polarity with swapped branches.

(ruleset norm.compare-polarity)

(rule not-eq-is-ne
      (not (icmp-eq ?x ?y)) (icmp-ne ?x ?y)
      :bidirectional #f)

(rule not-ne-is-eq
      (not (icmp-ne ?x ?y)) (icmp-eq ?x ?y)
      :bidirectional #f)

(rule not-lt-is-ge
      (not (icmp-lt ?x ?y)) (icmp-ge ?x ?y)
      :bidirectional #f)

(rule not-le-is-gt
      (not (icmp-le ?x ?y)) (icmp-gt ?x ?y)
      :bidirectional #f)

(rule not-gt-is-le
      (not (icmp-gt ?x ?y)) (icmp-le ?x ?y)
      :bidirectional #f)
