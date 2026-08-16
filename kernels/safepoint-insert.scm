;;; safepoint-insert.scm
;;; CCWeave Kernel: Inserts safepoints at loop back-edges and call sites in On1x modules.

(define-library ((ccweave kernel safepoint-insert))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . safepoint-insert)
        (version     . "0.0.0")
        (description . "Reserved kernel; no capability is advertised until its required IR semantics are available.")))

    (define (kernel-capabilities)
      '())

    ;; This kernel remains loadable for metadata discovery, but does not
    ;; advertise behavior that Glue ABI v1 cannot currently express.
    (define (kernel-apply capability ir options)
      (error "kernel: unsupported capability" capability))))
