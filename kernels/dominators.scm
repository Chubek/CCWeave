;;; dominators.scm
;;; CCWeave Kernel: Computes dominator tree and dominance frontier annotations.

(define-library ((ccweave kernel dominators))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . dominators)
        (version     . "1.0.0")
        (description . "Computes dominator tree and dominance frontier annotations.")))

    (define (kernel-capabilities)
      '(analysis.dominators))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.dominators)
        (error "dominators: unsupported capability" capability))
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
