;;; sema-salvo/ub/memory/rules.scm
;;; Semantic rules for sema.ub.memory.

(sema-ruleset sema.ub.memory)

(sema-rule null-deref
  :description "Dereference of definitely-null pointer is a definite error"
  :trigger (load $p)
  :target (error (null-deref (load $p)))
  :gating (fact (nullability $p null)))

(sema-rule oob-index
  :description "Index proven out of bounds by ranges is flagged"
  :trigger (index $arr $i)
  :target (assert (ub-site (index $arr $i) out-of-bounds))
  :gating (fact (range-exceeds $i (length $arr))))

(sema-rule uninit-read
  :description "Read of a variable with no reaching definition is flagged"
  :trigger (use $name)
  :target (assert (ub-site (use $name) uninitialized))
  :gating (fact (no-reaching-def $name)))

(sema-rule misaligned
  :description "Access with provably insufficient alignment is flagged"
  :trigger (load $p)
  :target (assert (ub-site (load $p) misaligned))
  :gating (fact (align-below $p (natural-align (pointee-type $p)))))

(sema-rule dangling-load
  :description "Load from a freed region is a definite error"
  :trigger (load $p)
  :target (error (use-after-free (load $p)))
  :gating (fact (region-freed (region $p))))

