;;; kernels/if-conversion.scm
;;; CCWeave Kernel: converts simple diamond branches to predicated selects.

(define-library (ccweave kernel if-conversion)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . if-conversion)
        (version     . "0.1.0")
        (description . "Converts simple diamond branches to predicated select instructions.")))

    (define (kernel-capabilities)
      '(opt.if-conversion))

    ;; Check if a block is a simple diamond: one conditional branch
    ;; to two successor blocks that converge to a single merge block.
    (define (simple-diamond? block)
      (and (= (block-succ-count block) 2)
           (let ((a (block-succ-ref block 0))
                 (b (block-succ-ref block 1)))
             (and (not (eqv? a b))
                  (let ((a-succ (block-succ-count a))
                        (b-succ (block-succ-count b)))
                    (and (= a-succ 1) (= b-succ 1)
                         (eqv? (block-succ-ref a 0) (block-succ-ref b 0))))))))

    (define (branch-condition block)
      (let ((n (block-instr-count block)))
        (and (> n 0)
             (let ((last (block-instr-ref block (- n 1))))
               (and (eq? (instr-opcode last) 'br)
                    (= (instr-operand-count last) 1)
                    (instr-operand last 0))))))

    (define (analyze-block! block)
      (when (simple-diamond? block)
        (let ((cond (branch-condition block)))
          (when cond
            (analysis-put! 'opt.if-conversion block 'diamond? #t)
            (analysis-put! 'opt.if-conversion block 'condition cond)
            (analysis-put! 'opt.if-conversion block 'true-arm
                           (block-succ-ref block 0))
            (analysis-put! 'opt.if-conversion block 'false-arm
                           (block-succ-ref block 1))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.if-conversion)
        (error "if-conversion: unsupported capability" capability))
      (unless (list? options)
        (error "if-conversion: options must be an alist" options))
      (unless (and (glue-has? 'block-succ-count) (glue-has? 'block-succ-ref)
                   (glue-has? 'analysis-put!))
        (error "if-conversion: CFG accessors are unavailable"))
      (let f ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (let b ((bi 0))
              (when (< bi (function-block-count fn))
                (analyze-block! (function-block-ref fn bi))
                (b (+ bi 1)))))
          (f (+ fi 1))))
      ir)))
