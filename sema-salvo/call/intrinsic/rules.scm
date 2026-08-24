;;; sema-salvo/call/intrinsic/rules.scm
;;; Semantic rules for sema.call.intrinsic.

(sema-ruleset sema.call.intrinsic)

(sema-rule vararg
  :description "Variadic call sites are marked and excluded from strict arity"
  :trigger (call $f $args)
  :target (assert (vararg-site (call $f $args)))
  :gating (fact (variadic $f)))

(sema-rule intrinsic-recognized
  :description "Call matching a libc intrinsic idiom is tagged"
  :trigger (call (global $f) $args)
  :target (assert (intrinsic-site (call (global $f) $args) (intrinsic-id $f)))
  :gating (fact (analysis.libc-intrinsic $f)))

