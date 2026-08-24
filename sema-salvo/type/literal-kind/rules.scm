;;; sema-salvo/type/literal-kind/rules.scm
;;; Semantic rules for sema.type.literal-kind.

(sema-ruleset sema.type.literal-kind)

(sema-rule int-literal
  :description "Integer literals carry type int"
  :trigger (int-lit $v)
  :target (assert (type (int-lit $v) int))
  :gating #t)

(sema-rule float-literal
  :description "Float literals carry type f64"
  :trigger (float-lit $v)
  :target (assert (type (float-lit $v) f64))
  :gating #t)

(sema-rule bool-literal
  :description "Boolean literals carry type bool"
  :trigger (bool-lit $v)
  :target (assert (type (bool-lit $v) bool))
  :gating #t)

(sema-rule string-literal
  :description "String literals carry pointer-to-const-char type"
  :trigger (str-lit $s)
  :target (assert (type (str-lit $s) (ptr (const char))))
  :gating #t)

