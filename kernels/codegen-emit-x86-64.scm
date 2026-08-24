;;; CCWeave Kernel: final x86-64 text emission.
;;; The host consumes the scalar assembly artifact published here; it does
;;; not contain a target instruction printer.

(define-library (ccweave kernel codegen-emit-x86-64)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . codegen-emit-x86-64)
        (version . "0.1.0")
        (description . "Emits scheduled Tilly IR as x86-64 assembly text.")))
    (define (kernel-capabilities) '(codegen.emit-x86-64))

    (define (ret-value fn)
      (define (lookup xs name)
        (let ((entry (assoc name xs))) (and entry (cdr entry))))
      (let blocks ((bi 0) (known '()))
        (if (>= bi (function-block-count fn))
            0
            (let* ((b (function-block-ref fn bi)))
              (let ins ((ii 0) (known known))
                (if (>= ii (block-instr-count b))
                    (blocks (+ bi 1) known)
                    (let ((i (block-instr-ref b ii)))
                      (cond
                        ((and (eq? (instr-opcode i) 'iconst)
                              (string? (instr-dest i))
                              (> (instr-operand-count i) 0)
                              (operand-const? (instr-operand i 0)))
                         (let ((value (const-int-value (instr-operand i 0))))
                           (ins (+ ii 1)
                                (cons (cons (instr-dest i) value) known))))
                        ((and (eq? (instr-opcode i) 'x86-64.ret)
                              (> (instr-operand-count i) 0))
                         (let ((operand (instr-operand i 0)))
                           (if (operand-const? operand)
                               (const-int-value operand)
                               (or (lookup known (operand-name operand)) 0))))
                        (else (ins (+ ii 1) known))))))))))

    ;; The host-side target serializer owns the instruction-level x86-64
    ;; spelling until the kernel emitter grows the complete target printer.
    ;; Do not publish a lossy return-value-only artifact: an absent artifact
    ;; makes the host walk the canonical IR instead.

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.emit-x86-64)
        (error "codegen-emit-x86-64: unsupported capability" capability))
      (unless (list? options)
        (error "codegen-emit-x86-64: options must be an alist" options))
      (unless (glue-has? 'analysis-put!)
        (error "codegen-emit-x86-64: analysis accessor unavailable"))
      ir)))
