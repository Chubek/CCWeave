;;; sema-salvo/oop/dispatch/rules.scm
;;; Semantic rules for sema.oop.dispatch.

(sema-ruleset sema.oop.dispatch)

(sema-rule receiver-type
  :description "Virtual call receiver static type bounds the target set"
  :trigger (vcall $recv $slot $args)
  :target (assert (target-set (vcall $recv $slot $args) (subclasses (type-of $recv))))
  :gating #t)

(sema-rule monomorphic-site
  :description "Virtual site with a single feedback type is devirtualizable"
  :trigger (vcall $recv $slot $args)
  :target (assert (devirt-candidate (vcall $recv $slot $args)))
  :gating (fact (single-type-feedback $recv)))

(sema-rule null-receiver
  :description "Virtual call requires a non-null receiver"
  :trigger (vcall $recv $slot $args)
  :target (require (nullability $recv non-null))
  :gating #t)

