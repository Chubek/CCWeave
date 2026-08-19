;;; stdrewrite/tailcall/apply/rules.scm
;;; Tail-call apply (D-0051): materialize an eligible self tail call as a jump.
;;;
;;; Keyed on opt.tailcall facts. The tail-call / tailcall-mark kernels publish
;;; (analysis-put! 'opt.tailcall call 'eligible? #t) for a call whose result is
;;; immediately returned by its containing block. This consumer turns that
;;; canonical (call …) + (ret dst) shape into a tail jump, folding the now-dead
;;; return into the jump target. It is an equivalence: a self tail call in tail
;;; position is observationally a jump that reuses the current frame.

(ruleset tailcall.apply)

;; A tail call immediately returned is a jump to the callee. The return value
;; is exactly the call destination, so dropping the ret preserves the result.
(rule tail-call-to-jump
      (ret (call ?f ?args)) (tailcall ?f ?args)
      :bidirectional #f)

;; An indirect tail call (through a closure or function value) is likewise a
;; tail jump; the callee arrives in a register rather than as a symbol.
(rule indirect-tail-call-to-jump
      (ret (call-indirect ?f ?args)) (tailcall-indirect ?f ?args)
      :bidirectional #f)

;; A call already lowered to a runtime closure invocation in tail position
;; still folds to a tail jump after closure conversion.
(rule closure-tail-call-to-jump
      (ret (runtime.closure.call ?c ?a)) (tailcall-indirect ?c ?a)
      :bidirectional #f)
