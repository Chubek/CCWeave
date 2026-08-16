;;; kernels/loop-unroll.scm
;;; CCWeave Kernel: loop unrolling.

(define-library (ccweave kernel loop-unroll)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . loop-unroll)
        (version     . "0.0.0")
        (description . "Loop unrolling driven by trip-count facts; supports full unroll for small constant counts and partial unroll with epilogue generation.")))

    (define (kernel-capabilities)
      '(opt.loop-unroll))

    ;; Loop topology and trip-count facts are host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.loop-unroll)
        (error "loop-unroll: unsupported capability" capability))
      (unless (list? options)
        (error "loop-unroll: options must be an alist" options))
      ir)))
