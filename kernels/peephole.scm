;;; kernels/peephole.scm
;;; CCWeave Kernel: target-parameterized peephole optimization.

(define-library (ccweave kernel peephole)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . peephole)
        (version     . "0.0.0")
        (description . "Target-parameterized peephole optimizer over machine nodes; pattern tables are supplied per profile.")))

    (define (kernel-capabilities)
      '(codegen.peephole))

    ;; Machine-node pattern tables are profile-specific host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.peephole)
        (error "peephole: unsupported capability" capability))
      (unless (list? options)
        (error "peephole: options must be an alist" options))
      ir)))
