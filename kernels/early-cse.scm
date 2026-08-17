;;; kernels/early-cse.scm
;;; CCWeave Kernel: early common subexpression elimination.

(define-library (ccweave kernel early-cse)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . early-cse)
        (version     . "0.1.0")
        (description . "Publishes duplicate expression candidates for elimination within basic blocks.")))

    (define (kernel-capabilities)
      '(opt.early-cse))

    ;; Compute a canonical hash-string for a pure instruction.
    (define (expr-hash ins)
      (let ((op (instr-opcode ins))
            (n (instr-operand-count ins)))
        (if (pure-op? op)
            (let loop ((i 0) (s (symbol->string op)))
              (if (>= i n)
                  s
                  (let ((opnd (instr-operand ins i)))
                    (loop (+ i 1)
                          (string-append s "|"
                                         (if (operand-const? opnd)
                                             (number->string (const-int-value opnd))
                                             (operand-name opnd))))))
            #f))))

    (define (pure-op? opcode)
      (memq opcode '(iadd isub imul iand ior ixor)))

    (define (analyze-block! block)
      (let ((seen '()))
        (let i ((ii 0))
          (when (< ii (block-instr-count block))
            (let ((ins (block-instr-ref block ii)))
              (let ((hash (expr-hash ins)))
                (if hash
                    (if (assoc hash seen)
                        (analysis-put! 'opt.early-cse ins 'duplicate? #t)
                        (set! seen (cons (cons hash ins) seen))))
                (i (+ ii 1))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.early-cse)
        (error "early-cse: unsupported capability" capability))
      (unless (list? options)
        (error "early-cse: options must be an alist" options))
      (unless (and (glue-has? 'analysis-put!) (glue-has? 'operand-const?)
                   (glue-has? 'const-int-value))
        (error "early-cse: analysis accessors are unavailable"))
      (let f ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (let b ((bi 0))
              (when (< bi (function-block-count fn))
                (analyze-block! (function-block-ref fn bi))
                (b (+ bi 1)))))
          (f (+ fi 1))))
      ir)))
