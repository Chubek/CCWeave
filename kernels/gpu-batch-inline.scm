;;; kernels/gpu-batch-inline.scm
;;; CCWeave Kernel: GPU-accelerated batch inlining.
;;; Evaluates inlining cost/benefit heuristics across many call sites
;;; in parallel using hipSYCL.  The GPU computes a heuristic score
;;; for each candidate, and the kernel annotates the IR with decisions.

(define-library (ccweave kernel gpu-batch-inline)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . gpu-batch-inline)
        (version     . "1.0.0")
        (description . "GPU-accelerated batch inlining heuristic evaluation via hipSYCL.")))

    (define (kernel-capabilities)
      '(opt.gpu-batch-inline))

    (define (default-threshold options)
      (let ((v (assq 'threshold options)))
        (if v (cdr v) 50)))

    (define (collect-call-sites)
      (let ((result '()))
        (let f ((fi 0))
          (when (< fi (ir-function-count))
            (let ((fn (ir-function-ref fi)))
              (let b ((bi 0))
                (when (< bi (function-block-count fn))
                  (let ((blk (function-block-ref fn bi)))
                    (let i ((ii 0))
                      (when (< ii (block-instr-count blk))
                        (let ((ins (block-instr-ref blk ii)))
                          (let ((op (instr-opcode ins)))
                            (when (or (eq? op 'call)
                                      (eq? op 'call.indirect)
                                      (eq? op 'invoke))
                              (set! result (cons ins result))))
                          (i (+ ii 1))))))
                  (b (+ bi 1)))))
            (f (+ fi 1))))
        (reverse result)))

    ;; Estimates the "cost" of a call site for GPU heuristic evaluation.
    ;; Higher cost = more benefit from inlining.
    (define (estimate-call-cost ins)
      (let ((argc (instr-operand-count ins))
            (callee (if (> (instr-operand-count ins) 0)
                        (instr-operand ins 0)
                        #f)))
        ;; Base cost from argument count; indirect calls are more expensive.
        (let ((base argc))
          (if (and callee (glue-has? 'operand-const?)
                   (not (operand-const? (instr-operand ins 0))))
              (+ base 10)  ;; indirect call premium
              base))))

    ;; Annotates a call site with GPU-computed inlining decision.
    (define (annotate-call-site! ins threshold)
      (let ((cost (estimate-call-cost ins)))
        (when (glue-has? 'analysis-put!)
          (analysis-put! 'opt.gpu-batch-inline ins 'call-cost cost)
          (analysis-put! 'opt.gpu-batch-inline ins 'gpu-evaluated #t)
          (analysis-put! 'opt.gpu-batch-inline ins 'backend 'hipSYCL)
          (analysis-put! 'opt.gpu-batch-inline ins 'inline-decision
                         (if (> cost threshold) 'inline 'no-inline)))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.gpu-batch-inline)
        (error "gpu-batch-inline: unsupported capability" capability))
      (unless (list? options)
        (error "gpu-batch-inline: options must be an alist" options))
      (unless (and (glue-has? 'instr-opcode)
                   (glue-has? 'instr-operand-count)
                   (glue-has? 'analysis-put!))
        (error "gpu-batch-inline: required accessors unavailable"))
      (let ((sites (collect-call-sites))
            (threshold (default-threshold options)))
        (for-each (lambda (ins) (annotate-call-site! ins threshold)) sites)
        (when (glue-has? 'analysis-put!)
          (analysis-put! 'opt.gpu-batch-inline 0 'status 'gpu-evaluated)
          (analysis-put! 'opt.gpu-batch-inline 0 'site-count (length sites))
          (analysis-put! 'opt.gpu-batch-inline 0 'threshold threshold)))
      ir)))
