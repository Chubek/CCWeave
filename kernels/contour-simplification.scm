;;; kernels/contour-simplification.scm
;;; CCWeave Kernel: simplifies code contours by merging adjacent equivalent blocks.

(define-library (ccweave kernel contour-simplification)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . contour-simplification)
        (version     . "0.1.0")
        (description . "Merges adjacent blocks that share identical instruction sequences.")))

    (define (kernel-capabilities)
      '(opt.contour-simplification))

    (define (block-equivalent? a b)
      (let ((na (block-instr-count a))
            (nb (block-instr-count b)))
        (and (= na nb) (> na 0)
             (let loop ((i 0))
               (if (>= i na)
                   #t
                   (let ((ia (block-instr-ref a i))
                         (ib (block-instr-ref b i)))
                     (and (eq? (instr-opcode ia) (instr-opcode ib))
                          (= (instr-operand-count ia) (instr-operand-count ib))
                          (loop (+ i 1)))))))))

    (define (analyze-function! fn)
      (let b ((bi 0))
        (when (< (+ bi 1) (function-block-count fn))
          (let ((cur (function-block-ref fn bi))
                (nxt (function-block-ref fn (+ bi 1))))
            (when (block-equivalent? cur nxt)
              (analysis-put! 'opt.contour-simplification cur 'merge-with nxt))
            (b (+ bi 1))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.contour-simplification)
        (error "contour-simplification: unsupported capability" capability))
      (unless (list? options)
        (error "contour-simplification: options must be an alist" options))
      (unless (glue-has? 'analysis-put!)
        (error "contour-simplification: analysis accessors are unavailable"))
      (let f ((fi 0))
        (when (< fi (ir-function-count))
          (analyze-function! (ir-function-ref fi))
          (f (+ fi 1))))
      ir)))
