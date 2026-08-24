;;; sema-salvo/vec/reduction/rules.scm
;;; Semantic rules for sema.vec.reduction.

(sema-ruleset sema.vec.reduction)

(sema-rule reduction-assoc
  :description "Reduction over a strictly associative op is vectorizable"
  :trigger (reduce $op $init $seq)
  :target (assert (vectorizable-reduction (reduce $op $init $seq)))
  :gating (fact (associative $op)))

(sema-rule fp-reassoc-optin
  :description "Float reduction reorder requires the explicit reassociation flag"
  :trigger (reduce $op $init $seq)
  :target (require (flag fp-reassociate))
  :gating (fact (float-lane $seq)))

