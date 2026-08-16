;;; kernels/deopt-metadata.scm
;;; CCWeave Kernel: deoptimization metadata construction.

(define-library (ccweave kernel deopt-metadata)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . deopt-metadata)
        (version . "0.1.0")
        (description . "Publishes explicit deopt targets and state operand counts.")))
    (define (kernel-capabilities) '(vm.deopt-metadata))

    (define (publish-block! block)
      (let loop ((index 0))
        (when (< index (block-instr-count block))
          (let ((instruction (block-instr-ref block index)))
            (when (eq? (instr-opcode instruction) 'deopt)
              (analysis-put! 'vm.deopt-metadata instruction
                             'state-operand-count
                             (instr-operand-count instruction))
              (when (> (instr-operand-count instruction) 0)
                (let ((target (instr-operand instruction 0)))
                  (when (eq? (operand-kind target) 'block)
                    (analysis-put! 'vm.deopt-metadata instruction
                                   'target (operand-name target)))))))
          (loop (+ index 1)))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'vm.deopt-metadata)
        (error "deopt-metadata: unsupported capability" capability))
      (unless (list? options)
        (error "deopt-metadata: options must be an alist" options))
      (unless (eq? (ir-profile) 'on1x)
        (error "deopt-metadata: capability requires the on1x profile"))
      (unless (and (glue-has? 'analysis-put!)
                   (glue-has? 'operand-kind)
                   (glue-has? 'operand-name))
        (error "deopt-metadata: analysis accessors are unavailable"))
      (let functions ((function-index 0))
        (when (< function-index (ir-function-count))
          (let ((function (ir-function-ref function-index)))
            (let blocks ((block-index 0))
              (when (< block-index (function-block-count function))
                (publish-block! (function-block-ref function block-index))
                (blocks (+ block-index 1)))))
          (functions (+ function-index 1))))
      ir)))
