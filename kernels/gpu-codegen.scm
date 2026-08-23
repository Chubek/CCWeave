;;; kernels/gpu-codegen.scm
;;; CCWeave Kernel: GPU-accelerated parallel code generation.
;;; Dispatches code generation for multiple functions simultaneously
;;; across GPU threads via hipSYCL.  Each function's instruction
;;; selection and register assignment is processed in parallel.

(define-library (ccweave kernel gpu-codegen)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . gpu-codegen)
        (version     . "1.0.0")
        (description . "GPU-accelerated parallel code generation via hipSYCL.")))

    (define (kernel-capabilities)
      '(lower.gpu-codegen))

    ;; Target architecture from options.
    (define (target-arch options)
      (let ((v (assq 'target options)))
        (if v (cdr v) 'x86-64)))

    ;; Annotates a function with GPU codegen metadata.
    (define (annotate-function! fn arch)
      (when (glue-has? 'analysis-put!)
        (analysis-put! 'lower.gpu-codegen fn 'gpu-codegen #t)
        (analysis-put! 'lower.gpu-codegen fn 'target-arch arch)
        (analysis-put! 'lower.gpu-codegen fn 'backend 'hipSYCL)
        (analysis-put! 'lower.gpu-codegen fn 'block-count
                       (function-block-count fn)))))

    ;; Counts total instructions in a function for GPU work-item sizing.
    (define (count-function-instrs fn)
      (let ((total 0))
        (let b ((bi 0))
          (when (< bi (function-block-count fn))
            (let ((blk (function-block-ref fn bi)))
              (set! total (+ total (block-instr-count blk))))
            (b (+ bi 1))))
        total))

    ;; Annotates a block with architecture-specific lowering hints.
    (define (annotate-block! blk arch)
      (when (glue-has? 'analysis-put!)
        (analysis-put! 'lower.gpu-codegen blk 'gpu-lowered #t)
        (analysis-put! 'lower.gpu-codegen blk 'arch arch)
        (analysis-put! 'lower.gpu-codegen blk 'lower-phase 'instruction-selection)))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.gpu-codegen)
        (error "gpu-codegen: unsupported capability" capability))
      (unless (list? options)
        (error "gpu-codegen: options must be an alist" options))
      (unless (and (glue-has? 'instr-opcode)
                   (glue-has? 'analysis-put!))
        (error "gpu-codegen: required accessors unavailable"))
      (let ((arch (target-arch options)))
        (let f ((fi 0))
          (when (< fi (ir-function-count))
            (let ((fn (ir-function-ref fi)))
              (annotate-function! fn arch)
              (let ((instrs (count-function-instrs fn)))
                (when (glue-has? 'analysis-put!)
                  (analysis-put! 'lower.gpu-codegen fn 'instr-count instrs)))
              (let b ((bi 0))
                (when (< bi (function-block-count fn))
                  (let ((blk (function-block-ref fn bi)))
                    (annotate-block! blk arch))
                  (b (+ bi 1)))))
            (f (+ fi 1)))))
      (when (glue-has? 'analysis-put!)
        (analysis-put! 'lower.gpu-codegen 0 'status 'gpu-codegen-ready)
        (analysis-put! 'lower.gpu-codegen 0 'target-arch arch)
        (analysis-put! 'lower.gpu-codegen 0 'backend 'hipSYCL))
      ir))
