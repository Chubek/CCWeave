;;; sema-salvo/null/check/rules.scm
;;; Semantic rules for sema.null.check.

(sema-ruleset sema.null.check)

(sema-rule param-unknown
  :description "Pointer parameters are maybe-null unless annotated"
  :trigger (param $name (ptr $ty))
  :target (assert (nullability (param $name (ptr $ty)) maybe-null))
  :gating (fact (not (nonnull-annotated $name))))

(sema-rule checked-nonnull
  :description "Value is non-null on the taken edge of a null check"
  :trigger (branch (cmp ne $p (null-lit)) $then $else)
  :target (assert (nullability-on-edge $p $then non-null))
  :gating #t)

(sema-rule deref-requires
  :description "Dereference requires a non-null address"
  :trigger (load $p)
  :target (require (nullability $p non-null))
  :gating #t)

