;;; sema-salvo/ub/cast/rules.scm
;;; Semantic rules for sema.ub.cast.

(sema-ruleset sema.ub.cast)

(sema-rule invalid-cast
  :description "Cast between incompatible non-pointer types is flagged"
  :trigger (cast $t $x)
  :target (assert (ub-site (cast $t $x) invalid-cast))
  :gating (fact (incompatible-cast $t $x)))

