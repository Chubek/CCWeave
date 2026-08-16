(define-library (ccweave kernel trip-count)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info) '((name . trip-count) (version . "0.1.0") (description . "Publishes exact one-iteration facts for blocks without successors.")))
    (define (kernel-capabilities) '(analysis.trip-count))
    (define (kernel-apply cap ir options)
      (unless (eq? cap 'analysis.trip-count) (error "trip-count: unsupported capability" cap))
      (let f ((fi 0)) (when (< fi (ir-function-count))
        (let b ((bi 0) (fn (ir-function-ref fi))) (when (< bi (function-block-count fn))
          (let ((blk (function-block-ref fn bi)))
            (analysis-put! 'analysis.trip-count blk 'exact?
                           (= (block-succ-count blk) 0))
            (analysis-put! 'analysis.trip-count blk 'count
                           (if (= (block-succ-count blk) 0) 1 -1)))
          (b (+ bi 1) fn)))
        (f (+ fi 1))))
      ir)))
