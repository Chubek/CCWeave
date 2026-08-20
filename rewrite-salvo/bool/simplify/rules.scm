;;; rewrite-salvo/bool/simplify/rules.scm
;;; Boolean absorption, idempotence, complement, and constant rules.

(ruleset bool.simplify)

(rule and-true
      (and ?x (iconst 1)) ?x
      :bidirectional #f)

(rule true-and
      (and (iconst 1) ?x) ?x
      :bidirectional #f)

(rule and-false
      (and ?x (iconst 0)) (iconst 0)
      :bidirectional #f)

(rule false-and
      (and (iconst 0) ?x) (iconst 0)
      :bidirectional #f)

(rule or-true
      (or ?x (iconst 1)) (iconst 1)
      :bidirectional #f)

(rule true-or
      (or (iconst 1) ?x) (iconst 1)
      :bidirectional #f)

(rule or-false
      (or ?x (iconst 0)) ?x
      :bidirectional #f)

(rule false-or
      (or (iconst 0) ?x) ?x
      :bidirectional #f)

(rule and-self
      (and ?x ?x) ?x
      :bidirectional #f)

(rule or-self
      (or ?x ?x) ?x
      :bidirectional #f)

(rule not-not
      (not (not ?x)) ?x
      :bidirectional #f)

(rule not-true
      (not (iconst 1)) (iconst 0)
      :bidirectional #f)
