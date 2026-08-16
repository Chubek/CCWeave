;;; kernels/jump-threading.scm
;;; CCWeave Kernel: control-flow jump threading.

(define-library (ccweave kernel jump-threading)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . jump-threading)
        (version     . "0.0.0")
        (description . "Threads control flow through blocks whose branch outcome is determined by a dominating condition, duplicating small blocks where profitable.")))

    (define (kernel-capabilities)
      '(opt.jump-threading))

    ;; CFG predecessor and branch-condition queries are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.jump-threading)
        (error "jump-threading: unsupported capability" capability))
      (unless (list? options)
        (error "jump-threading: options must be an alist" options))
      ir)))
