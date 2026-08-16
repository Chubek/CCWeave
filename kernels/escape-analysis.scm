;;; kernels/escape-analysis.scm
;;; CCWeave Kernel: interprocedural escape analysis.

(define-library (ccweave kernel escape-analysis)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . escape-analysis)
        (version     . "0.0.0")
        (description . "Interprocedural escape analysis; classifies allocations as stack-local, thread-local, or escaping to enable allocation sinking and scalar replacement.")))

    (define (kernel-capabilities)
      '(analysis.escape))

    ;; Portable Glue has no channel for analysis facts.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.escape)
        (error "escape-analysis: unsupported capability" capability))
      (unless (list? options)
        (error "escape-analysis: options must be an alist" options))
      ir)))
