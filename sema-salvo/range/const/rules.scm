;;; sema-salvo/range/const/rules.scm
;;; Semantic rules for sema.range.const.

(sema-ruleset sema.range.const)

(sema-rule const
  :description "Constant has the singleton range of its value"
  :trigger (int-lit $v)
  :target (assert (range (int-lit $v) $v $v))
  :gating #t)

