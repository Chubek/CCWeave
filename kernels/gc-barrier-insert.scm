(define-library (ccweave kernel gc-barrier-insert)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . gc-barrier-insert)
        (version . "0.1.0")
        (description . "Inserts idempotent write barriers before On1x stores.")))
    (define (kernel-capabilities) '(vm.gc-barrier-insertion))

    (define (instruction-operands instruction)
      (let loop ((index 0) (operands '()))
        (if (>= index (instr-operand-count instruction))
            (reverse operands)
            (loop (+ index 1)
                  (cons (instr-operand instruction index) operands)))))

    (define (barrier-before? block index)
      (and (> index 0)
           (eq? (instr-opcode (block-instr-ref block (- index 1)))
                'gc-write-barrier)))

    (define (instrument-block! block)
      (let loop ((index 0))
        (when (< index (block-instr-count block))
          (let ((instruction (block-instr-ref block index)))
            (if (and (eq? (instr-opcode instruction) 'store)
                     (not (barrier-before? block index)))
                (begin
                  (instr-insert-before!
                    instruction
                    (apply instr-build
                           (cons 'gc-write-barrier
                                 (instruction-operands instruction))))
                  (loop (+ index 2)))
                (loop (+ index 1)))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'vm.gc-barrier-insertion)
        (error "gc-barrier-insert: unsupported capability" capability))
      (unless (list? options)
        (error "gc-barrier-insert: options must be an alist" options))
      (unless (eq? (ir-profile) 'on1x)
        (error "gc-barrier-insert: capability requires the on1x profile"))
      (let functions ((function-index 0))
        (when (< function-index (ir-function-count))
          (let ((function (ir-function-ref function-index)))
            (let blocks ((block-index 0))
              (when (< block-index (function-block-count function))
                (instrument-block! (function-block-ref function block-index))
                (blocks (+ block-index 1)))))
          (functions (+ function-index 1))))
      ir)))
