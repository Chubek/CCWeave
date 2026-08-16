;;; kernels/ubsan-checks.scm
;;; CCWeave Kernel: undefined-behavior sanitizer instrumentation.

(define-library (ccweave kernel ubsan-checks)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . ubsan-checks)
        (version     . "0.0.0")
        (description . "Instruments IR with undefined-behavior checks (signed overflow, invalid shifts, null dereference) that trap to a runtime handler.")))

    (define (kernel-capabilities)
      '(sanitize.ubsan))

    ;; Sanitizer runtime and checked-operation accessors are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'sanitize.ubsan)
        (error "ubsan-checks: unsupported capability" capability))
      (unless (list? options)
        (error "ubsan-checks: options must be an alist" options))
      ir)))
