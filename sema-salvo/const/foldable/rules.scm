;;; sema-salvo/const/foldable/rules.scm
;;; Semantic rules for sema.const.foldable.

(sema-ruleset sema.const.foldable)

(sema-rule fold-candidate
  :description "Binop over two constants is foldable"
  :trigger (binop $op $x $y)
  :target (assert (foldable (binop $op $x $y)))
  :gating (and (fact (const $x)) (fact (const $y))))

(sema-rule pure-call
  :description "Pure call with constant args is foldable"
  :trigger (call $f $args)
  :target (assert (foldable (call $f $args)))
  :gating (and (fact (pure $f)) (fact (all-const $args))))

(sema-rule select-const-cond
  :description "Select with constant condition is foldable"
  :trigger (select $c $a $b)
  :target (assert (foldable (select $c $a $b)))
  :gating (fact (const $c)))

