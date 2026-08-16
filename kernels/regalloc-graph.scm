;;; kernels/regalloc-graph.scm
;;; CCWeave Kernel: graph-coloring register allocation.

(define-library (ccweave kernel regalloc-graph)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . regalloc-graph) (version . "0.1.0")
        (description . "Publishes conservative unique virtual graph colors.")))
    (define (kernel-capabilities) '(codegen.regalloc-graph))
    (define (color-function! fn)
      (let b ((bi 0) (color 0))
        (if (>= bi (function-block-count fn)) color
            (let ((block (function-block-ref fn bi)))
              (let i ((ii 0) (next color))
                (if (>= ii (block-instr-count block)) (b (+ bi 1) next)
                    (let ((ins (block-instr-ref block ii)))
                      (if (string? (instr-dest ins))
                          (begin
                            (analysis-put! 'codegen.regalloc-graph ins
                                           'allocator-color next)
                            (i (+ ii 1) (+ next 1)))
                          (i (+ ii 1) next)))))))))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.regalloc-graph)
        (error "regalloc-graph: unsupported capability" capability))
      (unless (list? options)
        (error "regalloc-graph: options must be an alist" options))
      (unless (and (glue-has? 'analysis-put!) (glue-has? 'instr-dest))
        (error "regalloc-graph: analysis accessors are unavailable"))
      (let f ((fi 0)) (when (< fi (ir-function-count))
        (color-function! (ir-function-ref fi)) (f (+ fi 1))))
      ir)))
