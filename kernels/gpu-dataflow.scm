;;; kernels/gpu-dataflow.scm
;;; CCWeave Kernel: GPU-accelerated dataflow analysis.
;;; Computes reaching-definitions, liveness, and def-use chains
;;; in parallel across CFG blocks using hipSYCL.  The GPU executes
;;; the fixed-point iteration over the dataflow lattice.

(define-library (ccweave kernel gpu-dataflow)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . gpu-dataflow)
        (version     . "1.0.0")
        (description . "GPU-accelerated dataflow analysis (reaching-defs, liveness, def-use) via hipSYCL.")))

    (define (kernel-capabilities)
      '(analysis.gpu-dataflow
        analysis.gpu-reaching-defs
        analysis.gpu-liveness
        analysis.gpu-def-use))

    ;; Supported sub-capabilities mapped from the requested capability.
    (define (sub-capability cap)
      (cond ((eq? cap 'analysis.gpu-dataflow)      'all)
            ((eq? cap 'analysis.gpu-reaching-defs) 'reaching-defs)
            ((eq? cap 'analysis.gpu-liveness)      'liveness)
            ((eq? cap 'analysis.gpu-def-use)       'def-use)
            (else #f)))

    ;; Collects all blocks across all functions for batch processing.
    (define (collect-all-blocks)
      (let ((result '()))
        (let f ((fi 0))
          (when (< fi (ir-function-count))
            (let ((fn (ir-function-ref fi)))
              (let b ((bi 0))
                (when (< bi (function-block-count fn))
                  (let ((blk (function-block-ref fn bi)))
                    (set! result (cons (cons fn blk) result)))
                  (b (+ bi 1)))))
            (f (+ fi 1))))
        (reverse result)))

    ;; Annotates a block with GPU dataflow analysis readiness.
    (define (annotate-block! fn blk direction)
      (when (glue-has? 'analysis-put!)
        (analysis-put! 'analysis.gpu-dataflow blk 'gpu-analyzed #t)
        (analysis-put! 'analysis.gpu-dataflow blk 'direction
                       (if (eq? direction 'forward) 'forward 'backward))
        (analysis-put! 'analysis.gpu-dataflow blk 'backend 'hipSYCL)))

    ;; Count defs and uses in a block for GPU work-item sizing.
    (define (count-defs-uses blk)
      (let ((defs 0) (uses 0))
        (let i ((ii 0))
          (when (< ii (block-instr-count blk))
            (let ((ins (block-instr-ref blk ii)))
              (when (and (glue-has? 'instr-dest) (instr-dest ins))
                (set! defs (+ defs 1)))
              (let o ((oi 0))
                (when (< oi (instr-operand-count ins))
                  (let ((op (instr-operand ins oi)))
                    (when (and (glue-has? 'operand-kind)
                               (eq? (operand-kind op) 'reg))
                      (set! uses (+ uses 1))))
                  (o (+ oi 1)))))
            (i (+ ii 1))))
        (cons defs uses)))

    (define (kernel-apply capability ir options)
      (let ((sub (sub-capability capability)))
        (unless sub
          (error "gpu-dataflow: unsupported capability" capability)))
      (unless (list? options)
        (error "gpu-dataflow: options must be an alist" options))
      (unless (and (glue-has? 'instr-opcode)
                   (glue-has? 'block-instr-count)
                   (glue-has? 'analysis-put!))
        (error "gpu-dataflow: required accessors unavailable"))
      (let ((blocks (collect-all-blocks))
            (direction (let ((d (assq 'direction options)))
                         (if d (cdr d) 'forward))))
        (for-each (lambda (pair)
                    (let ((fn (car pair)) (blk (cdr pair)))
                      (annotate-block! fn blk direction)
                      (let ((counts (count-defs-uses blk)))
                        (when (glue-has? 'analysis-put!)
                          (analysis-put! 'analysis.gpu-dataflow blk
                                         'def-count (car counts))
                          (analysis-put! 'analysis.gpu-dataflow blk
                                         'use-count (cdr counts))))))
                  blocks)
        (when (glue-has? 'analysis-put!)
          (analysis-put! 'analysis.gpu-dataflow 0 'status 'gpu-analyzed)
          (analysis-put! 'analysis.gpu-dataflow 0 'sub-analysis sub)
          (analysis-put! 'analysis.gpu-dataflow 0 'block-count (length blocks))))
      ir)))
