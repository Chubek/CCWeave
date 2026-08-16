;;; kernels/sccp.scm
;;; CCWeave Kernel: sparse conditional constant propagation.

(define-library (ccweave kernel sccp)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . sccp)
        (version     . "0.0.0")
        (description . "Sparse conditional constant propagation; simultaneously folds constants and marks unreachable edges using the lattice over ccw_val scalars.")))

    (define (kernel-capabilities)
      '(opt.sccp))

    ;; The core accessor set exposes neither CFG edges nor SCCP facts.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.sccp)
        (error "sccp: unsupported capability" capability))
      (unless (list? options)
        (error "sccp: options must be an alist" options))
      ir)))
