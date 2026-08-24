;;; sema-salvo/oop/layout/rules.scm
;;; Semantic rules for sema.oop.layout.

(sema-ruleset sema.oop.layout)

(sema-rule field-offset
  :description "Field access resolves to a fixed offset once layout is final"
  :trigger (field $obj $f)
  :target (assert (offset (field $obj $f) (layout-offset (type-of $obj) $f)))
  :gating (fact (layout-final (type-of $obj))))

(sema-rule upcast-safe
  :description "Cast to a superclass is always safe and effect-free"
  :trigger (cast $t $x)
  :target (assert (safe-cast (cast $t $x)))
  :gating (fact (superclass-of $t (type-of $x))))

(sema-rule downcast-check
  :description "Cast to a subclass requires a runtime type check"
  :trigger (cast $t $x)
  :target (assert (needs-type-check (cast $t $x)))
  :gating (fact (subclass-of $t (type-of $x))))

