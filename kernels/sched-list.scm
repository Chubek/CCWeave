;;; kernels/sched-list.scm
;;; CCWeave Kernel: machine-node list scheduling.

(define-library (ccweave kernel sched-list)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . sched-list)
        (version     . "0.0.0")
        (description . "List scheduler ordering machine nodes within blocks using latency tables from the target profile and memdep facts.")))

    (define (kernel-capabilities)
      '(codegen.sched-list))

    ;; Target latency tables and memory-dependence facts are extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.sched-list)
        (error "sched-list: unsupported capability" capability))
      (unless (list? options)
        (error "sched-list: options must be an alist" options))
      ir)))
