;;; sema-salvo/mem/allocation/rules.scm
;;; Semantic rules for sema.mem.allocation.

(sema-ruleset sema.mem.allocation)

(sema-rule alloca-noalias
  :description "Each stack allocation is a distinct region"
  :trigger (alloca $ty $n)
  :target (assert (distinct-region (alloca $ty $n)))
  :gating #t)

(sema-rule malloc-fresh
  :description "Heap allocation yields a fresh region aliasing nothing prior"
  :trigger (alloc $ty $n)
  :target (assert (fresh-region (alloc $ty $n)))
  :gating #t)

(sema-rule distinct-allocs
  :description "Two distinct allocation sites never alias"
  :trigger (pair (alloc $t1 $n1) (alloc $t2 $n2))
  :target (assert (no-alias (alloc $t1 $n1) (alloc $t2 $n2)))
  :gating #t)

(sema-rule stack-no-escape
  :description "Non-escaping stack allocation is promotable"
  :trigger (alloca $ty $n)
  :target (assert (promotable (alloca $ty $n)))
  :gating (fact (not (escapes (alloca $ty $n)))))

