;;; sema-salvo/null/allocation/rules.scm
;;; Semantic rules for sema.null.allocation.

(sema-ruleset sema.null.allocation)

(sema-rule alloc-nonnull
  :description "Successful allocation result is non-null"
  :trigger (alloc $ty $n)
  :target (assert (nullability (alloc $ty $n) non-null))
  :gating #t)

(sema-rule addr-of
  :description "Address-of always yields non-null"
  :trigger (addr-of $x)
  :target (assert (nullability (addr-of $x) non-null))
  :gating #t)

