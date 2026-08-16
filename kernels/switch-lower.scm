;;; kernels/switch-lower.scm
;;; CCWeave Kernel: switch lowering.

(define-library (ccweave kernel switch-lower)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . switch-lower)
        (version     . "0.0.0")
        (description . "Lowers switch nodes to jump tables, bit tests, or branch trees according to case density and target profile thresholds.")))

    (define (kernel-capabilities)
      '(lower.switch))

    ;; Switch cases and target thresholds are profile-specific extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.switch)
        (error "switch-lower: unsupported capability" capability))
      (unless (list? options)
        (error "switch-lower: options must be an alist" options))
      ir)))
