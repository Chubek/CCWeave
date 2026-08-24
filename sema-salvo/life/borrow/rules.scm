;;; sema-salvo/life/borrow/rules.scm
;;; Semantic rules for sema.life.borrow.

(sema-ruleset sema.life.borrow)

(sema-rule borrow-local
  :description "Address of a local must not outlive the local's scope"
  :trigger (addr-of (local $x))
  :target (require (contained-lifetime (addr-of (local $x)) (scope-of $x)))
  :gating #t)

