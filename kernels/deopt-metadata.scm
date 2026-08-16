;;; kernels/deopt-metadata.scm
;;; CCWeave Kernel: deoptimization metadata construction.

(define-library (ccweave kernel deopt-metadata)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . deopt-metadata)
        (version     . "0.0.0")
        (description . "Records deoptimization state maps (live values and their abstract locations) at designated side-exit nodes for VM profiles.")))

    (define (kernel-capabilities)
      '(vm.deopt-metadata))

    ;; Deoptimization state maps are VM-profile host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'vm.deopt-metadata)
        (error "deopt-metadata: unsupported capability" capability))
      (unless (list? options)
        (error "deopt-metadata: options must be an alist" options))
      ir)))
