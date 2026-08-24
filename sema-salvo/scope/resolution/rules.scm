;;; sema-salvo/scope/resolution/rules.scm
;;; Semantic rules for sema.scope.resolution.

(sema-ruleset sema.scope.resolution)

(sema-rule unresolved
  :description "Use with no binding in any scope is a semantic error"
  :trigger (use $name)
  :target (error (unresolved-name $name))
  :gating (fact (not (bound-any $name))))

(sema-rule global-ref
  :description "Reference to module-level binding is a global reference"
  :trigger (use $name)
  :target (assert (global-ref (use $name)))
  :gating (fact (bound-at-module $name)))

(sema-rule module-export
  :description "Exported name must be bound at module level"
  :trigger (export $name)
  :target (require (bound-at-module $name))
  :gating #t)

(sema-rule module-import
  :description "Import introduces an external binding"
  :trigger (import $mod $name)
  :target (assert (binds (module-scope) $name (external $mod $name)))
  :gating #t)

