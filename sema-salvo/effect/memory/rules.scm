;;; sema-salvo/effect/memory/rules.scm
;;; Semantic rules for sema.effect.memory.

(sema-ruleset sema.effect.memory)

(sema-rule load-read
  :description "Load has a read effect on its address region"
  :trigger (load $p)
  :target (assert (effect (load $p) (read (region $p))))
  :gating #t)

(sema-rule store-write
  :description "Store has a write effect on its address region"
  :trigger (store $p $v)
  :target (assert (effect (store $p $v) (write (region $p))))
  :gating #t)

(sema-rule volatile-io
  :description "Volatile access is an io effect and is unremovable"
  :trigger (load-volatile $p)
  :target (assert (effect (load-volatile $p) io))
  :gating #t)

(sema-rule atomic-sync
  :description "Atomic operation carries a synchronization effect"
  :trigger (atomic $op $args)
  :target (assert (effect (atomic $op $args) sync))
  :gating #t)

(sema-rule alloc-heap
  :description "Heap allocation has an alloc effect"
  :trigger (alloc $ty $n)
  :target (assert (effect (alloc $ty $n) alloc))
  :gating #t)

(sema-rule free-heap
  :description "Deallocation has a free effect on its region"
  :trigger (free $p)
  :target (assert (effect (free $p) (free (region $p))))
  :gating #t)

