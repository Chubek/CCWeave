(define-library (ccweave kernel spill-slot-pack)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . spill-slot-pack)
        (version . "0.1.0")
        (description . "Packs explicit byte-sized spill slots into contiguous offsets.")))
    (define (kernel-capabilities) '(codegen.spill-slot-pack))

    (define (pack-function! function)
      (let blocks ((block-index 0) (offset 0))
        (if (>= block-index (function-block-count function))
            offset
            (let ((block (function-block-ref function block-index)))
              (let instructions ((instruction-index 0) (next offset))
                (if (>= instruction-index (block-instr-count block))
                    (blocks (+ block-index 1) next)
                    (let ((instruction
                            (block-instr-ref block instruction-index)))
                      (if (and (eq? (instr-opcode instruction) 'spill-slot)
                               (= (instr-operand-count instruction) 1)
                               (eq? (operand-kind
                                      (instr-operand instruction 0))
                                    'const-int)
                               (> (const-int-value
                                    (instr-operand instruction 0))
                                  0))
                          (let ((size
                                  (const-int-value
                                    (instr-operand instruction 0))))
                            (analysis-put! 'codegen.spill-slot-pack
                                           instruction 'offset next)
                            (analysis-put! 'codegen.spill-slot-pack
                                           instruction 'size size)
                            (instructions (+ instruction-index 1)
                                          (+ next size)))
                          (instructions (+ instruction-index 1) next)))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.spill-slot-pack)
        (error "spill-slot-pack: unsupported capability" capability))
      (unless (list? options)
        (error "spill-slot-pack: options must be an alist" options))
      (unless (and (glue-has? 'analysis-put!)
                   (glue-has? 'operand-kind))
        (error "spill-slot-pack: analysis accessors are unavailable"))
      (let functions ((function-index 0))
        (when (< function-index (ir-function-count))
          (pack-function! (ir-function-ref function-index))
          (functions (+ function-index 1))))
      ir)))
