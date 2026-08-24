;;; sema-salvo/cfg/loop/rules.scm
;;; Semantic rules for sema.cfg.loop.

(sema-ruleset sema.cfg.loop)

(sema-rule loop-header
  :description "Block targeted by a back edge is a loop header"
  :trigger (block $b)
  :target (assert (loop-header $b))
  :gating (fact (has-back-edge $b)))

(sema-rule loop-exit
  :description "Edge leaving a loop body is an exit edge"
  :trigger (edge $from $to)
  :target (assert (loop-exit-edge $from $to))
  :gating (and (fact (in-loop $from)) (fact (not (in-loop $to)))))

(sema-rule back-edge
  :description "Edge to a dominator is a back edge"
  :trigger (edge $from $to)
  :target (assert (back-edge $from $to))
  :gating (fact (dominates $to $from)))

(sema-rule irreducible
  :description "Multiple-entry cycle is flagged irreducible"
  :trigger (cycle $blocks)
  :target (assert (irreducible $blocks))
  :gating (fact (multi-entry $blocks)))

(sema-rule infinite-loop
  :description "Loop with no exit edges and no io effect is flagged"
  :trigger (loop $body)
  :target (assert (non-terminating (loop $body)))
  :gating (and (fact (no-exit-edges $body)) (fact (effect-free $body))))

