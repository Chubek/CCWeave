;;; load-store-forward.scm
;;; CCWeave Kernel: Forwards stored values to subsequent loads of the same location.

(define-library ((ccweave kernel load-store-forward))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . load-store-forward)
        (version     . "0.0.0")
        (description . "Reserved kernel; no capability is advertised until its required IR semantics are available.")))

    (define (kernel-capabilities)
      '())

    ;; This kernel remains loadable for metadata discovery, but does not
    ;; advertise behavior that Glue ABI v1 cannot currently express.
    (define (kernel-apply capability ir options)
      (error "kernel: unsupported capability" capability))))
