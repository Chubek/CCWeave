;;; sema-salvo/vec/lane/rules.scm
;;; Semantic rules for sema.vec.lane.

(sema-ruleset sema.vec.lane)

(sema-rule lane-type
  :description "Vector op lanes must share one element type"
  :trigger (vec-op $op $width $args)
  :target (require (uniform-lane-type $args))
  :gating #t)

(sema-rule width-legal
  :description "Vector width must be published legal for the target"
  :trigger (vec-op $op $width $args)
  :target (require (fact (analysis.vector-width $width)))
  :gating #t)

(sema-rule mask-bool
  :description "Vector mask lanes must be boolean"
  :trigger (vec-select $mask $a $b)
  :target (require (lane-type $mask bool))
  :gating #t)

