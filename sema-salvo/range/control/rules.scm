;;; sema-salvo/range/control/rules.scm
;;; Semantic rules for sema.range.control.

(sema-ruleset sema.range.control)

(sema-rule cmp-guard
  :description "Comparison guard refines the range on the taken edge"
  :trigger (branch (cmp $op $x (int-lit $c)) $then $else)
  :target (assert (range-on-edge $x $then (refine $op $x $c)))
  :gating #t)

(sema-rule loop-iv
  :description "Affine induction variable range from bounds and step"
  :trigger (loop-iv $iv $init $step $bound)
  :target (assert (range $iv (iv-range $init $step $bound)))
  :gating (fact (loop-detected $iv)))

