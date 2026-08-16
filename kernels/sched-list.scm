;;; sched-list.scm
;;; CCWeave Kernel: List scheduling within basic blocks using latency tables.

(define-library ((ccweave kernel sched-list))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . sched-list)
        (version     . "1.0.0")
        (description . "List scheduling within basic blocks using latency tables.")))

    (define (kernel-capabilities)
      '(sched.list-scheduling))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'sched.list-scheduling)
        (error "sched-list: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (let* ((f (ir-function-ref i))
                   (nb (function-block-count f)))
              (let bloop ((j 0))
                (when (< j nb)
                  (let ((b (function-block-ref f j)))
                    (let ((ni (block-instr-count b)))
                      (let iloop ((k 0))
                        (when (< k ni)
                          (let ((ins (block-instr-ref b k)))
                            ;; Placeholder: reorder instructions by latency.
                            (iloop (+ k 1)))))))
                  (bloop (+ j 1)))))
            (loop (+ i 1)))))
      ir)))
