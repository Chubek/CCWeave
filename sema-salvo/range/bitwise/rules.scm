;;; sema-salvo/range/bitwise/rules.scm
;;; Semantic rules for sema.range.bitwise.

(sema-ruleset sema.range.bitwise)

(sema-rule and-mask
  :description "Bitwise and with constant mask bounds the result"
  :trigger (and $x (int-lit $m))
  :target (assert (range (and $x (int-lit $m)) 0 $m))
  :gating #t)

(sema-rule shift-bound
  :description "Right shift by constant narrows the range"
  :trigger (lshr $x (int-lit $k))
  :target (assert (range (lshr $x (int-lit $k)) (range-lshr $x $k)))
  :gating (fact (ranged $x)))

(sema-rule zext-nonneg
  :description "Zero extension yields a non-negative value"
  :trigger (zext $ty $x)
  :target (assert (range (zext $ty $x) 0 (type-max $ty)))
  :gating #t)

