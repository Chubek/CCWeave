;;; kernels/ubsan-checks.scm
;;; CCWeave Kernel: undefined-behavior sanitizer instrumentation.

(define-library (ccweave kernel ubsan-checks)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . ubsan-checks)
        (version . "0.1.0")
        (description . "Instruments potentially unsafe integer divisors.")))
    (define (kernel-capabilities) '(sanitize.ubsan))

    (define (division? opcode)
      (or (eq? opcode 'idiv) (eq? opcode 'irem)))

    (define (safe-constant-divisor? operand)
      (and (eq? (operand-kind operand) 'const-int)
           (not (= (const-int-value operand) 0))))

    (define (check-before? block index)
      (and (> index 0)
           (eq? (instr-opcode (block-instr-ref block (- index 1)))
                'ubsan-check-divisor)))

    (define (instrument-block! block)
      (let loop ((index 0))
        (when (< index (block-instr-count block))
          (let ((instruction (block-instr-ref block index)))
            (if (and (division? (instr-opcode instruction))
                     (= (instr-operand-count instruction) 2)
                     (not (safe-constant-divisor?
                            (instr-operand instruction 1)))
                     (not (check-before? block index)))
                (begin
                  (instr-insert-before!
                    instruction
                    (instr-build 'ubsan-check-divisor
                                 (instr-operand instruction 1)))
                  (loop (+ index 2)))
                (loop (+ index 1)))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'sanitize.ubsan)
        (error "ubsan-checks: unsupported capability" capability))
      (unless (list? options)
        (error "ubsan-checks: options must be an alist" options))
      (unless (glue-has? 'operand-kind)
        (error "ubsan-checks: scalar inspection accessors are unavailable"))
      (let functions ((function-index 0))
        (when (< function-index (ir-function-count))
          (let ((function (ir-function-ref function-index)))
            (let blocks ((block-index 0))
              (when (< block-index (function-block-count function))
                (instrument-block! (function-block-ref function block-index))
                (blocks (+ block-index 1)))))
          (functions (+ function-index 1))))
      ir)))
