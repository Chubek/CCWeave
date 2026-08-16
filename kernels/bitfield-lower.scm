;;; kernels/bitfield-lower.scm
;;; CCWeave Kernel: bitfield lowering.

(define-library (ccweave kernel bitfield-lower)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . bitfield-lower)
        (version     . "0.0.0")
        (description . "Expands bitfield load/store nodes into explicit mask-and-shift sequences over the containing storage unit.")))

    (define (kernel-capabilities)
      '(lower.bitfield))

    ;; Bitfield layout inspection is a profile-specific extension.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.bitfield)
        (error "bitfield-lower: unsupported capability" capability))
      (unless (list? options)
        (error "bitfield-lower: options must be an alist" options))
      ir)))
