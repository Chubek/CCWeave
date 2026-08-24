;;; sema-salvo/type/binop-propagate/rules.scm
;;; Semantic rules for sema.type.binop-propagate.

(sema-ruleset sema.type.binop-propagate)

(sema-rule add-propagate
  :description "Addition joins operand types"
  :trigger (add $x $y)
  :target (assert (type (add $x $y) (type-join $x $y)))
  :gating #t)

(sema-rule sub-propagate
  :description "Subtraction joins operand types"
  :trigger (sub $x $y)
  :target (assert (type (sub $x $y) (type-join $x $y)))
  :gating #t)

(sema-rule mul-propagate
  :description "Multiplication joins operand types"
  :trigger (mul $x $y)
  :target (assert (type (mul $x $y) (type-join $x $y)))
  :gating #t)

(sema-rule div-propagate
  :description "Division joins operand types"
  :trigger (div $x $y)
  :target (assert (type (div $x $y) (type-join $x $y)))
  :gating #t)

