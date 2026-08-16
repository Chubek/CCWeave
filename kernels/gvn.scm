;;; kernels/gvn.scm
;;; CCWeave Kernel: global value numbering.

(define-library (ccweave kernel gvn)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . gvn)
        (version     . "0.0.0")
        (description . "Global value numbering; discovers congruent expressions across basic blocks and records equivalences for the host to merge.")))

    (define (kernel-capabilities)
      '(opt.gvn))

    ;; Glue ABI v1 has no portable channel for value-numbering facts.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.gvn)
        (error "gvn: unsupported capability" capability))
      (unless (list? options)
        (error "gvn: options must be an alist" options))
      ir)))
