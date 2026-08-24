;;; sema-salvo/effect/call-effects/rules.scm
;;; Semantic rules for sema.effect.call-effects.

(sema-ruleset sema.effect.call-effects)

(sema-rule call-unknown
  :description "Call to unannotated function assumes full effects"
  :trigger (call $f $args)
  :target (assert (effect (call $f $args) all))
  :gating (fact (not (effect-annotated $f))))

(sema-rule call-annotated
  :description "Annotated call inherits the callee's declared effect set"
  :trigger (call $f $args)
  :target (assert (effect (call $f $args) (declared-effects $f)))
  :gating (fact (effect-annotated $f)))

