;;; sema-salvo/const/propagation/rules.scm
;;; Semantic rules for sema.const.propagation.

(sema-ruleset sema.const.propagation)

(sema-rule global-immutable
  :description "Load from an immutable global is constant"
  :trigger (load (global $g))
  :target (assert (const (load (global $g))))
  :gating (fact (immutable $g)))

(sema-rule propagate-copy
  :description "Copy of a constant is constant"
  :trigger (copy $x)
  :target (assert (const (copy $x)))
  :gating (fact (const $x)))

(sema-rule phi-same
  :description "Phi with identical constant inputs is constant"
  :trigger (phi $ops)
  :target (assert (const (phi $ops)))
  :gating (fact (all-same-const $ops)))

