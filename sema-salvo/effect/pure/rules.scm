;;; sema-salvo/effect/pure/rules.scm
;;; Semantic rules for sema.effect.pure.

(sema-ruleset sema.effect.pure)

(sema-rule pure-arith
  :description "Arithmetic on non-volatile operands is effect-free"
  :trigger (binop $op $x $y)
  :target (assert (effect (binop $op $x $y) none))
  :gating #t)

