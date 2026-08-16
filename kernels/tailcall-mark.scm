;;; kernels/tailcall-mark.scm
;;; CCWeave Kernel: tail-call marking.

(define-library (ccweave kernel tailcall-mark)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . tailcall-mark)
        (version     . "0.0.0")
        (description . "Identifies calls in tail position and rewrites them to explicit tail-call nodes when the profile's calling convention permits.")))

    (define (kernel-capabilities)
      '(opt.tailcall))

    ;; Tail-position and calling-convention queries are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.tailcall)
        (error "tailcall-mark: unsupported capability" capability))
      (unless (list? options)
        (error "tailcall-mark: options must be an alist" options))
      ir)))
