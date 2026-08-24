;;; sema-salvo/ub/arithmetic/rules.scm
;;; Semantic rules for sema.ub.arithmetic.

(sema-ruleset sema.ub.arithmetic)

(sema-rule div-by-zero
  :description "Division by literal zero is a definite error"
  :trigger (div $x (int-lit 0))
  :target (error (div-by-zero (div $x (int-lit 0))))
  :gating #t)

(sema-rule shift-oob
  :description "Shift amount at or above bit width is undefined"
  :trigger (shl $x (int-lit $k))
  :target (error (shift-oob $k))
  :gating (fact (geq $k (bit-width $x))))

(sema-rule signed-overflow-add
  :description "Signed add proven to overflow by ranges is flagged"
  :trigger (add $x $y)
  :target (assert (ub-site (add $x $y) signed-overflow))
  :gating (fact (ranges-overflow add $x $y)))

(sema-rule signed-overflow-mul
  :description "Signed mul proven to overflow by ranges is flagged"
  :trigger (mul $x $y)
  :target (assert (ub-site (mul $x $y) signed-overflow))
  :gating (fact (ranges-overflow mul $x $y)))

