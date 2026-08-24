;;; sema-salvo/life/liveness/rules.scm
;;; Semantic rules for sema.life.liveness.

(sema-ruleset sema.life.liveness)

(sema-rule def-before-use
  :description "Definition point opens the value's live range"
  :trigger (def $name $val)
  :target (assert (live-from $name (def $name $val)))
  :gating #t)

(sema-rule last-use
  :description "Use with no later uses closes the live range"
  :trigger (use $name)
  :target (assert (live-to $name (use $name)))
  :gating (fact (no-later-use $name)))

(sema-rule dead-store
  :description "Store overwritten before any read is dead"
  :trigger (store $p $v)
  :target (assert (dead-store (store $p $v)))
  :gating (fact (overwritten-before-read $p)))

(sema-rule live-across-call
  :description "Value live across a call needs a callee-saved home"
  :trigger (use $name)
  :target (assert (live-across-call $name))
  :gating (fact (call-between-def-use $name)))

(sema-rule live-across-safepoint
  :description "Managed pointer live across a safepoint must be in the stack map"
  :trigger (use $name)
  :target (require (in-stack-map $name))
  :gating (and (fact (managed-pointer $name)) (fact (safepoint-between-def-use $name))))

