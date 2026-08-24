;;; kernels/codegen-frame.scm
;;; CCWeave Kernel: frame layout and prologue/epilogue insertion.
;;; Sources: .agents/ISA-Bundle/ register classes and calling conventions.

(define-library (ccweave kernel codegen-frame)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . codegen-frame)
        (version . "0.1.0")
        (description . "Inserts prologue, epilogue, and frame layout instructions.")))
    (define (kernel-capabilities) '(codegen.frame))

    ;; Accepts an options alist with keys:
    ;;   target  -> 'x86-64, 'aarch64, or 'riscv64 (default: 'x86-64)
    ;;   frame-size -> integer byte count (default: 0, computed from stack slots)

    (define (target-abi options)
      (let ((val (assq 'target options)))
        (if val (cdr val) 'x86-64)))

    (define (frame-size options)
      (let ((val (assq 'frame-size options)))
        (if val (cdr val) 0)))

    (define (compute-stack-slots function)
      (let ((max-slot 0))
        (let blocks ((bi 0))
          (when (< bi (function-block-count function))
            (let ((block (function-block-ref function bi)))
              (let instrs ((ii 0))
                (when (< ii (block-instr-count block))
                  (let* ((ins (block-instr-ref block ii))
                         (slot-str (analysis-ref 'codegen.regalloc-linear ins
                                                 'allocator-slot)))
                    (when slot-str
                      (let ((s (string->number slot-str)))
                        (when (> s max-slot) (set! max-slot s))))
                    (instrs (+ ii 1))))))
            (blocks (+ bi 1))))
        ;; 8 bytes per slot, 16-byte aligned
        (let ((raw (* max-slot 8)))
          (* (ceiling (/ raw 16)) 16))))

    (define (insert-prologue! function entry target frame-sz)
      (let* ((first (block-instr-ref entry 0))
             (sp-op (if (eq? target 'aarch64) "sp" "rsp"))
             (fp-op (cond ((eq? target 'aarch64) "x29")
                          ((eq? target 'riscv64) "s0")
                          (else "rbp")))
             (lr-op (cond ((eq? target 'aarch64) "x30")
                          ((eq? target 'riscv64) "ra")
                          (else #f))))
        ;; push frame pointer
        (let ((push-fp (instr-build 'store
                                    (operand-reg-build (string->symbol sp-op))
                                    (operand-reg-build (string->symbol fp-op)))))
          (instr-insert-before! first push-fp))
        ;; mov fp, sp
        (let ((mov-fp (instr-build 'imov
                                   (operand-reg-build (string->symbol sp-op)))))
          (instr-set-dest! mov-fp fp-op)
          (instr-insert-before! first mov-fp))
        ;; sub sp, sp, frame-sz
        (when (> frame-sz 0)
          (let ((alloc-sp (instr-build 'isub
                                       (operand-reg-build (string->symbol sp-op))
                                       (const-int-build frame-sz))))
            (instr-set-dest! alloc-sp sp-op)
            (instr-insert-before! first alloc-sp)))
        ;; save link register if applicable
        (when lr-op
          (let ((save-lr (instr-build 'store
                                      (operand-reg-build (string->symbol sp-op))
                                      (operand-reg-build (string->symbol lr-op)))))
            (instr-insert-before! first save-lr)))))

    (define (insert-epilogue! function exit target frame-sz)
      (let ((sp-op (if (eq? target 'aarch64) "sp" "rsp"))
            (fp-op (cond ((eq? target 'aarch64) "x29")
                         ((eq? target 'riscv64) "s0")
                         (else "rbp")))
            (lr-op (cond ((eq? target 'aarch64) "x30")
                         ((eq? target 'riscv64) "ra")
                         (else #f)))
            (ret-ins (block-instr-ref exit (- (block-instr-count exit) 1))))
        ;; restore link register if applicable
        (when lr-op
          (let ((restore-lr (instr-build 'load
                                         (operand-reg-build (string->symbol sp-op)))))
            (instr-set-dest! restore-lr lr-op)
            (instr-insert-before! ret-ins restore-lr)))
        ;; mov sp, fp
        (let ((mov-sp (instr-build 'imov
                                   (operand-reg-build (string->symbol fp-op)))))
          (instr-set-dest! mov-sp sp-op)
          (instr-insert-before! ret-ins mov-sp))
        ;; pop frame pointer
        (let ((pop-fp (instr-build 'load
                                   (operand-reg-build (string->symbol sp-op)))))
          (instr-set-dest! pop-fp fp-op)
          (instr-insert-before! ret-ins pop-fp))))

    (define (apply-to-function function target frame-sz)
      (let ((entry (function-block-ref function 0)))
        (insert-prologue! function entry target frame-sz))
      (let ((last-bi (- (function-block-count function) 1)))
        (let ((last (function-block-ref function last-bi)))
          (insert-epilogue! function last target frame-sz))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.frame)
        (error "codegen-frame: unsupported capability" capability))
      (unless (list? options)
        (error "codegen-frame: options must be an alist" options))
      (unless (and (glue-has? 'analysis-ref)
                   (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!)
                   (glue-has? 'instr-insert-before!)
                   (glue-has? 'const-int-build))
        (error "codegen-frame: required Glue accessors are unavailable"))
      (let ((target (target-abi options))
            (declared-sz (frame-size options)))
        (let functions ((fi 0))
          (when (< fi (ir-function-count))
            (let ((fn (ir-function-ref fi)))
              (let ((computed-sz (compute-stack-slots fn)))
                (let ((frame-sz (if (> declared-sz 0) declared-sz computed-sz)))
                  (when (> frame-sz 0)
                    (apply-to-function fn target frame-sz)))))
            (functions (+ fi 1))))
        ir))))
