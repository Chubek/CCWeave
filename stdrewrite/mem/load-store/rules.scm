;;; stdrewrite/mem/load-store/rules.scm
;;; Store-to-load forwarding and redundant-load elimination;
;;; guarded by no-alias side conditions.

(ruleset mem.load-store)

(rule store-load-forward
      (load ?t (store ?t ?addr ?val)) ?val
      :bidirectional #f)

(rule redundant-load
      (load ?t ?addr) (load ?t ?addr)
      :bidirectional #t)

(rule dead-store
      (store ?t ?addr ?val) ?val
      :bidirectional #f)

(rule store-of-load
      (store ?t ?addr (load ?t ?addr)) (load ?t ?addr)
      :bidirectional #f)

(rule load-after-store
      (load ?t (store ?t ?addr ?val)) ?val
      :bidirectional #f)

(rule store-store-same-addr
      (store ?t ?addr (store ?t ?addr ?v1)) (store ?t ?addr ?v1)
      :bidirectional #f)

(rule load-const
      (load ?t (iconst ?a)) (load ?t (iconst ?a))
      :bidirectional #t)

(rule store-const
      (store ?t (iconst ?a) ?v) (store ?t (iconst ?a) ?v)
      :bidirectional #t)
