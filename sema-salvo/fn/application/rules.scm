;;; sema-salvo/fn/application/rules.scm
;;; Semantic rules for sema.fn.application.

(sema-ruleset sema.fn.application)

(sema-rule lambda-arity
  :description "Saturated application must match lambda arity"
  :trigger (a
  :target 
  :gating #t)

