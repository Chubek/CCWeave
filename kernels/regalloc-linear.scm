;;; kernels/regalloc-linear.scm
;;; CCWeave Kernel: linear-scan register allocation.

(define-library (ccweave kernel regalloc-linear)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . regalloc-linear) (version . "0.1.0")
        (description . "Publishes deterministic linear virtual-register assignments.")))
    (define (kernel-capabilities) '(codegen.regalloc-linear))
    (define (scan-function! fn)
      (let b ((bi 0) (position 0) (slot 0))
        (if (>= bi (function-block-count fn)) slot
            (let ((block (function-block-ref fn bi)))
              (let i ((ii 0) (pos position) (next slot))
                (if (>= ii (block-instr-count block)) (b (+ bi 1) pos next)
                    (let ((ins (block-instr-ref block ii)))
                      (analysis-put! 'codegen.regalloc-linear ins
                                     'allocator-position pos)
                      (if (string? (instr-dest ins))
                          (begin
                            (analysis-put! 'codegen.regalloc-linear ins
                                           'allocator-slot next)
                            (i (+ ii 1) (+ pos 1) (+ next 1)))
                          (i (+ ii 1) (+ pos 1) next)))))))))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.regalloc-linear)
        (error "regalloc-linear: unsupported capability" capability))
      (unless (list? options)
        (error "regalloc-linear: options must be an alist" options))
      (unless (and (glue-has? 'analysis-put!) (glue-has? 'instr-dest))
        (error "regalloc-linear: analysis accessors are unavailable"))
      (let f ((fi 0)) (when (< fi (ir-function-count))
        (scan-function! (ir-function-ref fi)) (f (+ fi 1))))
      ir)))
