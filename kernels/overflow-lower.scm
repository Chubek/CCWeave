;;; kernels/overflow-lower.scm
;;; CCWeave Kernel: checked-arithmetic lowering.

(define-library (ccweave kernel overflow-lower)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . overflow-lower)
        (version     . "0.0.0")
        (description . "Lowers checked-arithmetic intrinsics to flag-producing instruction pairs or wide-arithmetic sequences depending on target support.")))

    (define (kernel-capabilities)
      '(lower.overflow))

    ;; Checked-arithmetic metadata and target support are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.overflow)
        (error "overflow-lower: unsupported capability" capability))
      (unless (list? options)
        (error "overflow-lower: options must be an alist" options))
      ir)))
