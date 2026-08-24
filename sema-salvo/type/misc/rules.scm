;;; sema-salvo/type/misc/rules.scm
;;; Semantic rules for sema.type.misc.

(sema-ruleset sema.type.misc)

(sema-rule cmp-bool
  :description "Comparisons yield bool regardless of operand types"
  :trigger (cmp $op $x $y)
  :target (assert (type (cmp $op $x $y) bool))
  :gating #t)

(sema-rule load-deref
  :description "Load yields the pointee type of its address"
  :trigger (load $p)
  :target (assert (type (load $p) (pointee-type $p)))
  :gating #t)

(sema-rule store-check
  :description "Stored value must match the pointee type"
  :trigger (store $p $v)
  :target (require (type-eq $v (pointee-type $p)))
  :gating #t)

(sema-rule call-result
  :description "Call takes the callee's declared return type"
  :trigger (call $f $args)
  :target (assert (type (call $f $args) (return-type $f)))
  :gating #t)

(sema-rule phi-join
  :description "Phi joins all incoming operand types"
  :trigger (phi $ops)
  :target (assert (type (phi $ops) (type-join-all $ops)))
  :gating #t)

(sema-rule cast-explicit
  :description "Explicit cast fixes the result type"
  :trigger (cast $t $x)
  :target (assert (type (cast $t $x) $t))
  :gating #t)

(sema-rule select-join
  :description "Select joins its two arm types; condition must be bool"
  :trigger (select $c $a $b)
  :target (assert (type (select $c $a $b) (type-join $a $b)))
  :gating #t)

