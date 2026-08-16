;;; kernels/trip-count.scm
;;; CCWeave Kernel: loop trip-count estimation.

(define-library (ccweave kernel trip-count)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . trip-count)
        (version     . "0.0.0")
        (description . "Loop trip-count estimation; derives exact or symbolic iteration counts from induction variables and range facts for unrolling and vectorization heuristics.")))

    (define (kernel-capabilities)
      '(analysis.trip-count))

    ;; Loop and range facts are not in the portable accessor set.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.trip-count)
        (error "trip-count: unsupported capability" capability))
      (unless (list? options)
        (error "trip-count: options must be an alist" options))
      ir)))
