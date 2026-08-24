;;; sema-salvo/vec/access/rules.scm
;;; Semantic rules for sema.vec.access.

(sema-ruleset sema.vec.access)

(sema-rule unit-stride
  :description "Unit-stride access pattern is recorded for legality"
  :trigger (index $arr (loop-iv $iv $init 1 $bound))
  :target (assert (unit-stride (index $arr $iv)))
  :gating #t)

(sema-rule splat-uniform
  :description "Splat of a loop-invariant value is uniform across lanes"
  :trigger (splat $x $width)
  :target (assert (uniform (splat $x $width)))
  :gating (fact (loop-invariant $x)))

(sema-rule gather-deferred
  :description "Non-unit-stride access is published as a negative legality fact"
  :trigger (index $arr $i)
  :target (assert (not-vectorizable (index $arr $i) non-unit-stride))
  :gating (fact (not (unit-stride (index $arr $i)))))

