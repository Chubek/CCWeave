;;; kernels/switch-lower.scm
;;; CCWeave Kernel: switch lowering.

(define-library (ccweave kernel switch-lower)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . switch-lower) (version . "0.1.0")
        (description . "Folds constant-selector switches to direct branches.")))
    (define (kernel-capabilities) '(lower.switch))
    ;; Layout: selector, default block, then repeated integer, block pairs.
    (define (selected-target ins selector)
      (let loop ((i 2))
        (if (>= (+ i 1) (instr-operand-count ins))
            (instr-operand ins 1)
            (let ((value (instr-operand ins i))
                  (target (instr-operand ins (+ i 1))))
              (if (and (eq? (operand-kind value) 'const-int)
                       (eq? (operand-kind target) 'block)
                       (= selector (const-int-value value)))
                  target
                  (loop (+ i 2)))))))
    (define (lower-block! block)
      (let loop ((i 0))
        (when (< i (block-instr-count block))
          (let ((ins (block-instr-ref block i)))
            (when (and (eq? (instr-opcode ins) 'switch)
                       (>= (instr-operand-count ins) 2)
                       (eq? (operand-kind (instr-operand ins 0)) 'const-int)
                       (eq? (operand-kind (instr-operand ins 1)) 'block))
              (instr-replace! ins
                (instr-build 'br
                  (selected-target ins
                    (const-int-value (instr-operand ins 0))))))
            (loop (+ i 1))))))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.switch)
        (error "switch-lower: unsupported capability" capability))
      (unless (list? options)
        (error "switch-lower: options must be an alist" options))
      (unless (glue-has? 'operand-kind) (error "switch-lower: scalar inspection accessors are unavailable"))
      (let f ((fi 0)) (when (< fi (ir-function-count))
        (let ((fn (ir-function-ref fi))) (let b ((bi 0))
          (when (< bi (function-block-count fn))
            (lower-block! (function-block-ref fn bi)) (b (+ bi 1)))))
        (f (+ fi 1))))
      ir)))
