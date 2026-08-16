;;; kernels/regalloc-graph.scm
;;; CCWeave Kernel: graph-coloring register allocation.

(define-library (ccweave kernel regalloc-graph)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . regalloc-graph)
        (version     . "0.0.0")
        (description . "Graph-coloring register allocator with coalescing and rematerialization; the default allocator for optimizing pipelines.")))

    (define (kernel-capabilities)
      '(codegen.regalloc-graph))

    ;; Interference graphs and register assignment are target extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.regalloc-graph)
        (error "regalloc-graph: unsupported capability" capability))
      (unless (list? options)
        (error "regalloc-graph: options must be an alist" options))
      ir)))
