;;; kernels/range-analysis.scm
;;; CCWeave Kernel: integer value-range analysis.

(define-library (ccweave kernel range-analysis)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . range-analysis)
        (version     . "0.0.0")
        (description . "Integer value-range analysis over the dominator tree; annotates nodes with conservative min/max intervals consumed by bounds-check and overflow-check elision.")))

    (define (kernel-capabilities)
      '(analysis.range))

    ;; Range annotations and dominance data require host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.range)
        (error "range-analysis: unsupported capability" capability))
      (unless (list? options)
        (error "range-analysis: options must be an alist" options))
      ir)))
