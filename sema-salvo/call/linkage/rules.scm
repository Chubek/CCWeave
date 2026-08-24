;;; sema-salvo/call/linkage/rules.scm
;;; Semantic rules for sema.call.linkage.

(sema-ruleset sema.call.linkage)

(sema-rule direct-known
  :description "Call to a defined symbol is direct and inlinable-candidate"
  :trigger (call (global $f) $args)
  :target (assert (direct-call (call (global $f) $args)))
  :gating (fact (defined $f)))

(sema-rule indirect-unknown
  :description "Call through a value has an unknown target set"
  :trigger (call-indirect $p $args)
  :target (assert (targets (call-indirect $p $args) unknown))
  :gating #t)

(sema-rule recursive
  :description "Call targeting the enclosing function is recursive"
  :trigger (call (global $f) $args)
  :target (assert (recursive-call (call (global $f) $args)))
  :gating (fact (enclosing-function $f)))

(sema-rule extern-linkage
  :description "Call to an external symbol crosses the module boundary"
  :trigger (call (global $f) $args)
  :target (assert (extern-call (call (global $f) $args)))
  :gating (fact (external $f)))

