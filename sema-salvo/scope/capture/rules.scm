;;; sema-salvo/scope/capture/rules.scm
;;; Semantic rules for sema.scope.capture.

(sema-ruleset sema.scope.capture)

(sema-rule closure-capture
  :description "Free variable in a lambda is a capture"
  :trigger (lambda $params $body)
  :target (assert (captures (lambda $params $body) (free-vars $body $params)))
  :gating #t)

(sema-rule capture-by-ref
  :description "Captured variable that is mutated must be captured by reference"
  :trigger (lambda $params $body)
  :target (assert (capture-mode $body by-ref))
  :gating (fact (mutates-capture $body)))

