;;; Dependence facts gate structural loop rewrites in the host scheduler.
(ruleset polyhedral.dependence-gated)

(rule dependence-gated-identity
      ?x ?x
      :bidirectional #f)
