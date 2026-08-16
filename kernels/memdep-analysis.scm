;;; kernels/memdep-analysis.scm
;;; CCWeave Kernel: memory-dependence analysis.

(define-library (ccweave kernel memdep-analysis)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . memdep-analysis)
        (version     . "0.0.0")
        (description . "Memory dependence analysis relating loads, stores, and calls; results are keyed by node ID pairs and consumed by scheduling and redundancy elimination.")))

    (define (kernel-capabilities)
      '(analysis.memdep))

    ;; Memory-effect inspection and fact storage require host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.memdep)
        (error "memdep-analysis: unsupported capability" capability))
      (unless (list? options)
        (error "memdep-analysis: options must be an alist" options))
      ir)))
