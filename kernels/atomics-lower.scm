;;; kernels/atomics-lower.scm
;;; CCWeave Kernel: atomic-operation lowering.

(define-library (ccweave kernel atomics-lower)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . atomics-lower)
        (version . "0.1.0")
        (description . "Lowers atomic load/store nodes to sequentially consistent libcall operations.")))
    (define (kernel-capabilities) '(lower.atomics))

    (define (lowered-opcode opcode)
      (cond ((eq? opcode 'atomic.load) 'atomic-libcall.load.seq-cst)
            ((eq? opcode 'atomic.store) 'atomic-libcall.store.seq-cst)
            (else #f)))

    (define (instruction-operands instruction)
      (let loop ((index 0) (operands '()))
        (if (>= index (instr-operand-count instruction))
            (reverse operands)
            (loop (+ index 1)
                  (cons (instr-operand instruction index) operands)))))

    (define (lower-block! block)
      (let loop ((index 0))
        (when (< index (block-instr-count block))
          (let* ((instruction (block-instr-ref block index))
                 (opcode (lowered-opcode (instr-opcode instruction))))
            (when opcode
              (let ((replacement
                      (apply instr-build
                             (cons opcode
                                   (instruction-operands instruction))))
                    (destination (instr-dest instruction)))
                (when (string? destination)
                  (instr-set-dest! replacement destination))
                (instr-replace! instruction replacement)))
            (loop (+ index 1))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.atomics)
        (error "atomics-lower: unsupported capability" capability))
      (unless (list? options)
        (error "atomics-lower: options must be an alist" options))
      (unless (and (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!))
        (error "atomics-lower: scalar mutation accessors are unavailable"))
      (let functions ((function-index 0))
        (when (< function-index (ir-function-count))
          (let ((function (ir-function-ref function-index)))
            (let blocks ((block-index 0))
              (when (< block-index (function-block-count function))
                (lower-block! (function-block-ref function block-index))
                (blocks (+ block-index 1)))))
          (functions (+ function-index 1))))
      ir)))
