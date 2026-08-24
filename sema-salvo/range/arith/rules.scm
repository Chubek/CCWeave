;;; sema-salvo/range/arith/rules.scm
;;; Semantic rules for sema.range.arith.

(sema-ruleset sema.range.arith)

(sema-rule add
  :description "Addition range is the interval sum of operand ranges"
  :trigger (add $x $y)
  :target (assert (range (add $x $y) (range-add $x $y)))
  :gating (and (fact (ranged $x)) (fact (ranged $y))))

(sema-rule sub
  :description "Subtraction range is the interval difference"
  :trigger (sub $x $y)
  :target (assert (range (sub $x $y) (range-sub $x $y)))
  :gating (and (fact (ranged $x)) (fact (ranged $y))))

(sema-rule mul
  :description "Multiplication range is the interval product"
  :trigger (mul $x $y)
  :target (assert (range (mul $x $y) (range-mul $x $y)))
  :gating (and (fact (ranged $x)) (fact (ranged $y))))

(sema-rule div-narrow
  :description "Division by constant greater than one narrows the range"
  :trigger (div $x (int-lit $c))
  :target (assert (range (div $x (int-lit $c)) (range-div $x $c)))
  :gating (and (fact (ranged $x)) (fact (gt $c 1))))

