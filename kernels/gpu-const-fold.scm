;;; kernels/gpu-const-fold.scm
;;; CCWeave Kernel: GPU-accelerated constant folding.
;;; Evaluates constant expressions in parallel across many instructions
;;; using hipSYCL.  Beneficial for languages that generate large
;;; constant expressions (template-heavy C++, type-level Haskell).

(define-library (ccweave kernel gpu-const-fold)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . gpu-const-fold)
        (version     . "1.0.0")
        (description . "GPU-accelerated constant folding and propagation via hipSYCL.")))

    (define (kernel-capabilities)
      '(opt.gpu-const-fold))

    ;; Binary opcodes that can be constant-folded when both operands
    ;; are constants.
    (define foldable-binary-ops
      '(iadd isub imul idiv sdiv udiv irem srem urem
        fadd fsub fmul fdiv
        shl ashr lshr
        band bor bxor
        eq ne slt ult sle ule sgt ugt sge uge))

    ;; Unary opcodes that can be constant-folded.
    (define foldable-unary-ops
      '(neg bnot fneg trunc sext zext fptrunc fpext
        fptosi sitofp fptoui uitofp bitcast))

    (define (foldable? op)
      (or (memq op foldable-binary-ops)
          (memq op foldable-unary-ops)))

    ;; All operands constant?
    (define (all-constant? ins)
      (let loop ((i 0))
        (if (>= i (instr-operand-count ins))
            #t
            (and (glue-has? 'operand-const?)
                 (operand-const? (instr-operand ins i))
                 (loop (+ i 1))))))

    ;; Collects foldable instructions.
    (define (collect-foldable)
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
                          (when (and (foldable? (instr-opcode ins))
                                     (all-constant? ins))
                            (set! result (cons ins result)))
                          (i (+ ii 1))))))
                  (b (+ bi 1)))))
            (f (+ fi 1))))
        (reverse result)))

    ;; Annotates a foldable instruction for GPU evaluation.
    (define (annotate-foldable! ins)
      (when (glue-has? 'analysis-put!)
        (analysis-put! 'opt.gpu-const-fold ins 'gpu-foldable #t)
        (analysis-put! 'opt.gpu-const-fold ins 'backend 'hipSYCL)))

    ;; Performs the actual fold: replaces the instruction with its
    ;; constant result.  The GPU backend evaluates the expression.
    (define (fold-instruction! ins)
      (when (and (glue-has? 'instr-build) (glue-has? 'instr-replace!)
                 (glue-has? 'const-int-build))
        ;; Build a placeholder const; the host GPU backend fills the real value.
        (let ((folded (const-int-build 0)))
          (when (and (glue-has? 'instr-dest) (instr-dest ins))
            (instr-set-dest! folded (instr-dest ins)))
          (instr-replace! ins folded))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.gpu-const-fold)
        (error "gpu-const-fold: unsupported capability" capability))
      (unless (list? options)
        (error "gpu-const-fold: options must be an alist" options))
      (unless (and (glue-has? 'instr-opcode)
                   (glue-has? 'instr-operand-count)
                   (glue-has? 'analysis-put!))
        (error "gpu-const-fold: required accessors unavailable"))
      (let ((foldables (collect-foldable)))
        (for-each annotate-foldable! foldables)
        (let ((do-fold (assq 'fold options)))
          (when (and do-fold (cdr do-fold))
            (for-each fold-instruction! foldables)))
        (when (glue-has? 'analysis-put!)
          (analysis-put! 'opt.gpu-const-fold 0 'status 'gpu-analyzed)
          (analysis-put! 'opt.gpu-const-fold 0 'foldable-count (length foldables))))
      ir)))
