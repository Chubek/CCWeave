;;; kernels/complex-lower.scm
;;; CCWeave Kernel: complex-number lowering.

(define-library (ccweave kernel complex-lower)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . complex-lower)
        (version     . "0.0.0")
        (description . "Lowers complex-number arithmetic to pairs of scalar floating-point operations following the profile's ABI for _Complex returns.")))

    (define (kernel-capabilities)
      '(lower.complex))

    ;; Complex types and ABI construction are profile-specific extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.complex)
        (error "complex-lower: unsupported capability" capability))
      (unless (list? options)
        (error "complex-lower: options must be an alist" options))
      ir)))
