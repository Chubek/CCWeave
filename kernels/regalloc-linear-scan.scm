;;; regalloc-linear-scan.scm
;;; CCWeave Kernel: Linear-scan register allocation over live intervals.

(define-library ((ccweave kernel regalloc-linear-scan))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . regalloc-linear-scan)
        (version     . "1.0.0")
        (description . "Linear-scan register allocation over live intervals.")))

    (define (kernel-capabilities)
      '(regalloc.linear-scan))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'regalloc.linear-scan)
        (error "regalloc-linear-scan: unsupported capability" capability))
      ;; Walk all functions and perform register allocation.
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
                            ;; Placeholder for register allocation logic.
                            (iloop (+ k 1)))))))
                  (bloop (+ j 1)))))
            (loop (+ i 1)))))
      ir)))
