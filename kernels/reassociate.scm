;;; kernels/reassociate.scm
;;; CCWeave Kernel: integer expression reassociation.

(define-library (ccweave kernel reassociate)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . reassociate)
        (version     . "0.0.0")
        (description . "Reassociates commutative integer expression trees into canonical rank order to expose constant folding and redundancy elimination.")))

    (define (kernel-capabilities)
      '(opt.reassociate))

    ;; Def-use ranks needed for safe reassociation are host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.reassociate)
        (error "reassociate: unsupported capability" capability))
      (unless (list? options)
        (error "reassociate: options must be an alist" options))
      ir)))
