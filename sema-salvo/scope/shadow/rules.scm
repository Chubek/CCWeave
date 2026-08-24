;;; sema-salvo/scope/shadow/rules.scm
;;; Semantic rules for sema.scope.shadow.

(sema-ruleset sema.scope.shadow)

(sema-rule shadow
  :description "Inner binding shadowing an outer one is recorded"
  :trigger (def $name $val)
  :target (assert (shadows $name (outer-binding $name)))
  :gating (fact (bound-in-outer $name)))

(sema-rule dead-binding
  :description "Binding with no uses is marked dead"
  :trigger (def $name $val)
  :target (assert (dead-binding $name))
  :gating (fact (use-count $name 0)))

