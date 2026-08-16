;;; kernels/loop-fusion.scm
;;; CCWeave Kernel: loop fusion.

(define-library (ccweave kernel loop-fusion)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . loop-fusion)
        (version     . "0.0.0")
        (description . "Fuses adjacent loops with identical iteration spaces and no fusion-preventing dependences, reducing loop overhead and improving locality.")))

    (define (kernel-capabilities)
      '(opt.loop-fusion))

    ;; Loop iteration spaces and dependence facts are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.loop-fusion)
        (error "loop-fusion: unsupported capability" capability))
      (unless (list? options)
        (error "loop-fusion: options must be an alist" options))
      ir)))
