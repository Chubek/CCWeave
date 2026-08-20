;;; rewrite-salvo/bitwise/demorgan/rules.scm
;;; De Morgan dualities between and/or under complement.

(ruleset bitwise.demorgan)

(rule demorgan-and
      (not (and ?x ?y)) (or (not ?x) (not ?y))
      :bidirectional #t)

(rule demorgan-or
      (not (or ?x ?y)) (and (not ?x) (not ?y))
      :bidirectional #t)

(rule demorgan-and-to-or
      (and ?x ?y) (not (or (not ?x) (not ?y)))
      :bidirectional #t)

(rule demorgan-or-to-and
      (or ?x ?y) (not (and (not ?x) (not ?y)))
      :bidirectional #t)

(rule demorgan-nand
      (not (and ?x ?y)) (or (not ?x) (not ?y))
      :bidirectional #t)

(rule demorgan-nor
      (not (or ?x ?y)) (and (not ?x) (not ?y))
      :bidirectional #t)
