;;; sema-salvo/scope/bind/rules.scm
;;; Semantic rules for sema.scope.bind.

(sema-ruleset sema.scope.bind)

(sema-rule bind-def
  :description "Definition introduces a binding in the current scope"
  :trigger (def $name $val)
  :target (assert (binds (current-scope) $name (def $name $val)))
  :gating #t)

(sema-rule bind-use
  :description "Use resolves to the nearest enclosing binding"
  :trigger (use $name)
  :target (assert (resolves (use $name) (lookup (current-scope) $name)))
  :gating #t)

(sema-rule param-bind
  :description "Formal parameter binds within the function body"
  :trigger (param $name $ty)
  :target (assert (binds (function-scope) $name (param $name $ty)))
  :gating #t)

(sema-rule letrec-cycle
  :description "Mutually recursive bindings form a resolution group"
  :trigger (letrec $binds $body)
  :target (assert (resolution-group $binds))
  :gating #t)

