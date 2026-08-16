;;; stdrewrite/canonical-form.scm
;;; Normalization ruleset: legal-but-undesirable spellings and their
;;; canonical equivalents. Same equivalence relation as optimization;
;;; only the extraction cost model differs (§7.2).

(ruleset normalize.canonical-form)

(rule sub-to-add-negate
      (isub ?x ?y) (iadd ?x (ineg ?y))
      :bidirectional #t)

(rule double-negate
      (ineg (ineg ?x)) ?x
      :bidirectional #t)

(rule move-of-move
      (imov (imov ?x)) (imov ?x)
      :bidirectional #f)
