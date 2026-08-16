;;; kernels/atomics-lower.scm
;;; CCWeave Kernel: atomic-operation lowering.

(define-library (ccweave kernel atomics-lower)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . atomics-lower)
        (version     . "0.0.0")
        (description . "Lowers atomic operations to target-supported primitives, inserting fences or libcall fallbacks where the profile lacks native support.")))

    (define (kernel-capabilities)
      '(lower.atomics))

    ;; Atomic semantics and target primitives are profile extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.atomics)
        (error "atomics-lower: unsupported capability" capability))
      (unless (list? options)
        (error "atomics-lower: options must be an alist" options))
      ir)))
