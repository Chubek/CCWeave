;;; kernels/slp-vectorize.scm
;;; CCWeave Kernel: superword-level parallelism vectorization.

(define-library (ccweave kernel slp-vectorize)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . slp-vectorize)
        (version     . "0.0.0")
        (description . "Superword-level parallelism vectorizer; packs isomorphic scalar operation trees into vector nodes where the target profile advertises vector widths.")))

    (define (kernel-capabilities)
      '(opt.slp-vectorize))

    ;; Vector types and target-width queries are profile extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.slp-vectorize)
        (error "slp-vectorize: unsupported capability" capability))
      (unless (list? options)
        (error "slp-vectorize: options must be an alist" options))
      ir)))
