;;; sema-salvo/call/abi/rules.scm
;;; Semantic rules for sema.call.abi.

(sema-ruleset sema.call.abi)

(sema-rule tail-position
  :description "Call whose result is immediately returned is in tail position"
  :trigger (return (call $f $args))
  :target (assert (tail-position (call $f $args)))
  :gating #t)

(sema-rule abi-mismatch
  :description "Call whose argument types differ from the declaration errors"
  :trigger (call (global $f) $args)
  :target (error (abi-mismatch $f $args))
  :gating (fact (not (signature-match $f $args))))

(sema-rule arity-check
  :description "Non-variadic call must match declared arity"
  :trigger (call (global $f) $args)
  :target (require (arity-eq $f $args))
  :gating (fact (not (variadic $f))))

(sema-rule noreturn
  :description "Call to a noreturn function terminates the block"
  :trigger (call (global $f) $args)
  :target (assert (block-terminator (call (global $f) $args)))
  :gating (fact (noreturn $f)))

