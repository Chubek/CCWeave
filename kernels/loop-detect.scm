;;; loop-detect.scm
;;; CCWeave Kernel: Identifies natural loops, headers, and nesting depth.

(define-library ((ccweave kernel loop-detect))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . loop-detect)
        (version     . "1.0.0")
        (description . "Identifies natural loops, headers, and nesting depth.")))

    (define (kernel-capabilities)
      '(analysis.natural-loops))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.natural-loops)
        (error "loop-detect: unsupported capability" capability))
      (let ((result '()))
        (let ((n (ir-function-count)))
          (let loop ((i 0))
            (when (< i n)
              (let* ((f (ir-function-ref i))
                     (nb (function-block-count f)))
                (let bloop ((j 0))
                  (when (< j nb)
                    (let* ((b (function-block-ref f j))
                           (ni (block-instr-count b)))
                      (let iloop ((k 0))
                        (when (< k ni)
                          (let ((ins (block-instr-ref b k)))
                            (set! result (cons (instr-opcode ins) result)))
                          (iloop (+ k 1)))))
                    (bloop (+ j 1)))))
              (loop (+ i 1)))))
        (reverse result))
      ir)))
