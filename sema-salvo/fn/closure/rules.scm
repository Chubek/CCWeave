;;; sema-salvo/fn/closure/rules.scm
;;; Semantic rules for sema.fn.closure.

(sema-ruleset sema.fn.closure)

(sema-rule closure-env
  :description "Closure environment layout is derived from its capture set"
  :trigger (lambda $params $body)
  :target (assert (env-layout (lambda $params $body) (sorted-captures $body)))
  :gating #t)

