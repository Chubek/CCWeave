;;; ISL schedule materialization hooks.  The host applies canonical schedule
;;; facts; these rules preserve the IR expression while making the consumer
;;; explicit to the ruleset manifest.
(ruleset polyhedral.schedule-apply)

(rule schedule-applied-identity
      ?x ?x
      :bidirectional #f)
