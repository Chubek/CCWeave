;;; sema-salvo/null/join/rules.scm
;;; Semantic rules for sema.null.join.

(sema-ruleset sema.null.join)

(sema-rule phi-join
  :description "Phi nullability is the join of incoming nullabilities"
  :trigger (phi $ops)
  :target (assert (nullability (phi $ops) (null-join $ops)))
  :gating #t)

