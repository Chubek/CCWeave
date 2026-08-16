(define-library (ccweave kernel regalloc-graph-color)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . regalloc-graph-color)
        (version . "0.1.0")
        (description . "Publishes a conservative unique virtual color for each local definition.")))
    (define (kernel-capabilities) '(codegen.regalloc-graph))

    ;; Giving every definition a unique color is conservative for any
    ;; interference graph and does not assume a physical target register set.
    (define (color-function! function)
      (let blocks ((block-index 0) (color 0))
        (if (>= block-index (function-block-count function))
            color
            (let ((block (function-block-ref function block-index)))
              (let instructions ((instruction-index 0) (next color))
                (if (>= instruction-index (block-instr-count block))
                    (blocks (+ block-index 1) next)
                    (let ((instruction
                            (block-instr-ref block instruction-index)))
                      (if (string? (instr-dest instruction))
                          (begin
                            (analysis-put! 'codegen.regalloc-graph instruction
                                           'virtual-color next)
                            (instructions (+ instruction-index 1)
                                          (+ next 1)))
                          (instructions (+ instruction-index 1) next)))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.regalloc-graph)
        (error "regalloc-graph-color: unsupported capability" capability))
      (unless (list? options)
        (error "regalloc-graph-color: options must be an alist" options))
      (unless (and (glue-has? 'analysis-put!)
                   (glue-has? 'instr-dest))
        (error "regalloc-graph-color: analysis accessors are unavailable"))
      (let functions ((function-index 0))
        (when (< function-index (ir-function-count))
          (color-function! (ir-function-ref function-index))
          (functions (+ function-index 1))))
      ir)))
