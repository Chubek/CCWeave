;;; sema-salvo/cfg/unreachable/rules.scm
;;; Semantic rules for sema.cfg.unreachable.

(sema-ruleset sema.cfg.unreachable)

(sema-rule unreachable-after-return
  :description "Code after return in the same block is unreachable"
  :trigger (seq (return $v) $rest)
  :target (assert (unreachable $rest))
  :gating #t)

(sema-rule unreachable-after-throw
  :description "Code after throw in the same block is unreachable"
  :trigger (seq (throw $e) $rest)
  :target (assert (unreachable $rest))
  :gating #t)

