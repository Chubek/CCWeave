;;; sema-salvo/mem/restrict/rules.scm
;;; Semantic rules for sema.mem.restrict.

(sema-ruleset sema.mem.restrict)

(sema-rule restrict-param
  :description "Restrict-qualified parameter aliases no other parameter"
  :trigger (param $name (restrict-ptr $ty))
  :target (assert (no-alias-params $name))
  :gating #t)

