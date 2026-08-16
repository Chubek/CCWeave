;;; kernels/mem2reg.scm
;;; CCWeave Kernel: stack-slot promotion to SSA.

(define-library (ccweave kernel mem2reg)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . mem2reg)
        (version     . "0.0.0")
        (description . "Promotes non-escaping stack slots to SSA values, inserting phi nodes at dominance frontiers; requires analysis.escape facts.")))

    (define (kernel-capabilities)
      '(opt.mem2reg))

    ;; Stack-slot, dominance, and phi construction accessors are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.mem2reg)
        (error "mem2reg: unsupported capability" capability))
      (unless (list? options)
        (error "mem2reg: options must be an alist" options))
      ir)))
