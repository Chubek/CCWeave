;;; sema-salvo/mem/alias/rules.scm
;;; Semantic rules for sema.mem.alias.

(sema-ruleset sema.mem.alias)

(sema-rule alias-may
  :description "Unrelated pointer pair defaults to may-alias"
  :trigger (pair $p $q)
  :target (assert (may-alias $p $q))
  :gating (fact (not (proven-distinct $p $q))))

