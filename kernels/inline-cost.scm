;;; kernels/inline-cost.scm
;;; CCWeave Kernel: inlining cost analysis.

(define-library (ccweave kernel inline-cost)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . inline-cost)
        (version     . "0.0.0")
        (description . "Computes per-callsite inlining cost/benefit scores from callee size, argument constness, and call frequency; emits advisory annotations only.")))

    (define (kernel-capabilities)
      '(opt.inline-cost))

    ;; Callsite frequency and annotation storage are host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.inline-cost)
        (error "inline-cost: unsupported capability" capability))
      (unless (list? options)
        (error "inline-cost: options must be an alist" options))
      ir)))
