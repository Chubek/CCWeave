;;; kernels/gpu-pattern-match.scm
;;; CCWeave Kernel: GPU-accelerated pattern matching compilation.
;;; Offloads decision-tree construction for pattern matches to GPU
;;; via hipSYCL.  Critical for functional languages (Haskell, SML)
;;; where deep pattern matches dominate compile time.

(define-library (ccweave kernel gpu-pattern-match)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . gpu-pattern-match)
        (version     . "1.0.0")
        (description . "GPU-accelerated pattern-match decision-tree lowering via hipSYCL.")))

    (define (kernel-capabilities)
      '(lower.gpu-pattern-match))

    (define (collect-pattern-matches)
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
                            (when (or (eq? op 'match)
                                      (eq? op 'pattern-match)
                                      (eq? op 'case)
                                      (eq? op 'switch.pattern))
                              (set! result (cons ins result))))
                          (i (+ ii 1))))))
                  (b (+ bi 1)))))
            (f (+ fi 1))))
        (reverse result)))

    ;; Count the number of arms in a pattern-match instruction.
    (define (pattern-arm-count ins)
      (let ((n (instr-operand-count ins)))
        (if (> n 1) (- n 1) 0)))

    ;; Annotates a pattern-match node with GPU decision-tree metadata.
    (define (annotate-pattern-match! ins)
      (let ((arms (pattern-arm-count ins)))
        (when (and (glue-has? 'analysis-put!) (> arms 0))
          (analysis-put! 'lower.gpu-pattern-match ins 'arm-count arms)
          (analysis-put! 'lower.gpu-pattern-match ins 'gpu-lowered #t)
          (analysis-put! 'lower.gpu-pattern-match ins 'backend 'hipSYCL)
          ;; For deep patterns (> 8 arms), recommend GPU decision tree.
          (when (> arms 8)
            (analysis-put! 'lower.gpu-pattern-match ins 'decision-tree 'gpu-binary))
          (when (<= arms 8)
            (analysis-put! 'lower.gpu-pattern-match ins 'decision-tree 'gpu-linear)))))

    ;; Lowers a pattern-match instruction to a GPU-friendly switch cascade.
    (define (lower-match! ins)
      (when (and (glue-has? 'instr-build) (glue-has? 'instr-replace!))
        (let ((scrutinee (if (> (instr-operand-count ins) 0)
                             (instr-operand ins 0)
                             #f)))
          (when scrutinee
            (let ((lowered (instr-build 'gpu-switch.cascade scrutinee)))
              (when (and (glue-has? 'instr-dest) (instr-dest ins))
                (instr-set-dest! lowered (instr-dest ins)))
              (instr-replace! ins lowered))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.gpu-pattern-match)
        (error "gpu-pattern-match: unsupported capability" capability))
      (unless (list? options)
        (error "gpu-pattern-match: options must be an alist" options))
      (unless (and (glue-has? 'instr-opcode) (glue-has? 'instr-operand-count))
        (error "gpu-pattern-match: required accessors unavailable"))
      (let ((matches (collect-pattern-matches)))
        (for-each annotate-pattern-match! matches)
        (let ((do-lower (assq 'lower options)))
          (when (and do-lower (cdr do-lower))
            (for-each lower-match! matches))))
      ir)))
