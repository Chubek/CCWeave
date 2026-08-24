;;; sema-salvo/mem/escape/rules.scm
;;; Semantic rules for sema.mem.escape.

(sema-ruleset sema.mem.escape)

(sema-rule escape-store
  :description "Storing a pointer into memory escapes it"
  :trigger (store $p $q)
  :target (assert (escapes $q))
  :gating (fact (is-pointer $q)))

(sema-rule escape-call
  :description "Passing a pointer to an unknown call escapes it"
  :trigger (call $f $args)
  :target (assert (escapes-any (pointer-args $args)))
  :gating (fact (not (effect-annotated $f))))

(sema-rule escape-return
  :description "Returning a pointer escapes it"
  :trigger (return $p)
  :target (assert (escapes $p))
  :gating (fact (is-pointer $p)))

