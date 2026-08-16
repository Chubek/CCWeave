;;; kernels/code-sink.scm
;;; CCWeave Kernel: code sinking.

(define-library (ccweave kernel code-sink)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . code-sink)
        (version     . "0.0.0")
        (description . "Sinks computations closer to their uses when this reduces partially dead code, guarded by memory-dependence facts.")))

    (define (kernel-capabilities)
      '(opt.sink))

    ;; Use locations and memory-dependence facts are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.sink)
        (error "code-sink: unsupported capability" capability))
      (unless (list? options)
        (error "code-sink: options must be an alist" options))
      ir)))
