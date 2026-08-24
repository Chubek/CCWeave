;;; sema-salvo/oop/ctor/rules.scm
;;; Semantic rules for sema.oop.ctor.

(sema-ruleset sema.oop.ctor)

(sema-rule ctor-init
  :description "Every field must be initialized on all constructor paths"
  :trigger (ctor $cls $body)
  :target (require (all-fields-initialized $cls $body))
  :gating #t)

(sema-rule override-check
  :description "Override must match the overridden slot's signature"
  :trigger (method $cls $name $sig)
  :target (require (signature-compatible $sig (super-slot $cls $name)))
  :gating (fact (overrides $cls $name)))

