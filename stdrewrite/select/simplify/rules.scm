;;; stdrewrite/select/simplify/rules.scm
;;; Select with equal arms, constant condition, or negated condition.

(ruleset select.simplify)

(rule select-equal-arms
      (select ?c ?x ?x) ?x
      :bidirectional #f)

(rule select-true
      (select (iconst 1) ?t ?f) ?t
      :bidirectional #f)

(rule select-false
      (select (iconst 0) ?t ?f) ?f
      :bidirectional #f)

(rule select-not-swap
      (select (not ?c) ?t ?f) (select ?c ?f ?t)
      :bidirectional #t)

(rule select-eq-swap
      (select (icmp-eq ?x ?y) ?t ?f) (select (icmp-ne ?x ?y) ?f ?t)
      :bidirectional #t)

(rule select-of-select-merge
      (select ?c1 (select ?c2 ?x ?y) ?y)
      (select (and ?c1 ?c2) ?x ?y)
      :bidirectional #f)

(rule select-of-select-merge-alt
      (select ?c1 ?x (select ?c2 ?x ?y))
      (select (or (not ?c1) ?c2) ?x ?y)
      :bidirectional #f)

(rule select-cast
      (select ?c (zext ?t ?x) (zext ?t ?y))
      (zext ?t (select ?c ?x ?y))
      :bidirectional #t)

(rule select-add
      (select ?c (iadd ?x ?a) (iadd ?y ?a))
      (iadd (select ?c ?x ?y) ?a)
      :bidirectional #t)

(rule select-mul
      (select ?c (imul ?x ?a) (imul ?y ?a))
      (imul (select ?c ?x ?y) ?a)
      :bidirectional #t)
