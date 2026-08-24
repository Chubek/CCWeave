;;; sema-salvo/const/not-const/rules.scm
;;; Semantic rules for sema.const.not-const.

(sema-ruleset sema.const.not-const)

(sema-rule addr-not-const
  :description "Taking an address is never a compile-time constant value"
  :trigger (addr-of $x)
  :target (assert (not-const (addr-of $x)))
  :gating #t)

(sema-rule volatile-not-const
  :description "Volatile loads are never constant"
  :trigger (load-volatile $p)
  :target (assert (not-const (load-volatile $p)))
  :gating #t)

