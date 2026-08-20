;;; rewrite-salvo/bool/branch-select/rules.scm
;;; Converts branch-on-constant-condition and same-target diamonds into selects.

(ruleset bool.branch-select)

(rule br-cond-true
      (br (iconst 1) ?then ?else) (br (iconst 1) ?then ?then)
      :bidirectional #f)

(rule br-cond-false
      (br (iconst 0) ?then ?else) (br (iconst 0) ?else ?else)
      :bidirectional #f)

(rule br-same-targets
      (br ?c ?t ?t) ?t
      :bidirectional #f)

(rule br-to-select
      (br ?c ?t ?f) (select ?c ?t ?f)
      :bidirectional #t)

(rule select-eq-to-br
      (select (icmp-eq ?x ?y) ?t ?f) (br (icmp-eq ?x ?y) ?t ?f)
      :bidirectional #t)

(rule select-ne-to-br
      (select (icmp-ne ?x ?y) ?t ?f) (br (icmp-ne ?x ?y) ?f ?t)
      :bidirectional #t)

(rule select-not-to-br
      (select (not ?c) ?t ?f) (select ?c ?f ?t)
      :bidirectional #t)

(rule br-not-to-select
      (br (not ?c) ?t ?f) (select ?c ?f ?t)
      :bidirectional #t)
