;;; ISL tile-plan materialization hook.
(ruleset polyhedral.tile-apply)

(rule tile-plan-identity
      ?x ?x
      :bidirectional #f)
