;;; sema-salvo/mem/field/rules.scm
;;; Semantic rules for sema.mem.field.

(sema-ruleset sema.mem.field)

(sema-rule field-disjoint
  :description "Distinct fields of one object are disjoint regions"
  :trigger (field $obj $f)
  :target (assert (disjoint-within $obj $f))
  :gating #t)

