;;; sema-salvo/effect/control/rules.scm
;;; Semantic rules for sema.effect.control.

(sema-ruleset sema.effect.control)

(sema-rule throw
  :description "Throw carries a control effect"
  :trigger (throw $e)
  :target (assert (effect (throw $e) control))
  :gating #t)

(sema-rule wasi-io
  :description "Any wasi-op carries an io effect"
  :trigger (wasi-op $op $args)
  :target (assert (effect (wasi-op $op $args) io))
  :gating #t)

(sema-rule safepoint
  :description "Safepoint carries a gc effect"
  :trigger (safepoint $id)
  :target (assert (effect (safepoint $id) gc))
  :gating #t)

