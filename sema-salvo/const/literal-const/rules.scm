;;; sema-salvo/const/literal-const/rules.scm
;;; Semantic rules for sema.const.literal-const.

(sema-ruleset sema.const.literal-const)

(sema-rule literal
  :description "Every literal is a compile-time constant"
  :trigger (int-lit $v)
  :target (assert (const (int-lit $v)))
  :gating #t)

(sema-rule string-immutable
  :description "String literal storage is immutable"
  :trigger (str-lit $s)
  :target (assert (immutable-storage (str-lit $s)))
  :gating #t)

