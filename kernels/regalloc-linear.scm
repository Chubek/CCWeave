;;; kernels/regalloc-linear.scm
;;; CCWeave Kernel: linear-scan register allocation.

(define-library (ccweave kernel regalloc-linear)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . regalloc-linear)
        (version     . "0.0.0")
        (description . "Linear-scan register allocator intended for fast, unoptimized builds; single pass over live intervals with on-demand spilling.")))

    (define (kernel-capabilities)
      '(codegen.regalloc-linear))

    ;; Live intervals and register assignment are target extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.regalloc-linear)
        (error "regalloc-linear: unsupported capability" capability))
      (unless (list? options)
        (error "regalloc-linear: options must be an alist" options))
      ir)))
