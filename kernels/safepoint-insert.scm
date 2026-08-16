;;; kernels/safepoint-insert.scm
;;; CCWeave Kernel: VM safepoint insertion.

(define-library (ccweave kernel safepoint-insert)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . safepoint-insert)
        (version     . "0.0.0")
        (description . "Inserts VM safepoint polls at loop back-edges and call returns for profiles that declare cooperative suspension.")))

    (define (kernel-capabilities)
      '(vm.safepoint-insertion))

    ;; Safepoint conventions and control-flow edges are host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'vm.safepoint-insertion)
        (error "safepoint-insert: unsupported capability" capability))
      (unless (list? options)
        (error "safepoint-insert: options must be an alist" options))
      ir)))
