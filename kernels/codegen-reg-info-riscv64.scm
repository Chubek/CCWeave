;;; kernels/codegen-reg-info-riscv64.scm
;;; CCWeave Kernel: RV64 register class information.
;;; Sources: .agents/ISA-Bundle/rv64.isa register classes and aliases.

(define-library (ccweave kernel codegen-reg-info-riscv64)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . codegen-reg-info-riscv64)
        (version . "0.1.0")
        (description . "Publishes RV64 register class and allocation metadata.")))
    (define (kernel-capabilities) '(codegen.reg-info.riscv64))

    ;; GPR: T0-T5, A0-A7, S1-S11, FP, SP, GP, TP, RA, T6
    (define gpr-names
      '#("t0" "t1" "t2" "t3" "t4" "t5" "a0" "a1"
         "a2" "a3" "a4" "a5" "a6" "a7" "s1" "s2"
         "s3" "s4" "s5" "s6" "s7" "s8" "s9" "s10"
         "s11" "sp" "gp" "tp" "ra" "t6"))

    ;; FPR: FT0-FT11, FA0-FA7, FS0-FS11
    (define fpr-names
      '#("ft0" "ft1" "ft2" "ft3" "ft4" "ft5" "ft6" "ft7"
         "ft8" "ft9" "ft10" "ft11" "fa0" "fa1" "fa2" "fa3"
         "fa4" "fa5" "fa6" "fa7" "fs0" "fs1" "fs2" "fs3"
         "fs4" "fs5" "fs6" "fs7" "fs8" "fs9" "fs10" "fs11"))

    ;; Caller-saved: t0-t6, a0-a7, ft0-ft11, fa0-fa7 (RISC-V calling convention).
    (define caller-saved-gpr '(0 1 2 3 4 5 6 7 8 9 10 11 12 13 29))
    (define callee-saved-gpr '(14 15 16 17 18 19 20 21 22 23 24))
    (define caller-saved-fpr
      '(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19))
    (define callee-saved-fpr
      '(20 21 22 23 24 25 26 27 28 29 30 31))

    (define reserved-gpr '(25 26 27 28))

    (define (publish-register-info function)
      (let ((count (vector-length gpr-names)))
        (let loop ((i 0))
          (when (< i count)
            (analysis-put! 'codegen.reg-info.riscv64 function
                           (string->symbol (string-append "gpr." (vector-ref gpr-names i)))
                           (number->string i))
            (loop (+ i 1)))))
      (let ((count (vector-length fpr-names)))
        (let loop ((i 0))
          (when (< i count)
            (analysis-put! 'codegen.reg-info.riscv64 function
                           (string->symbol (string-append "fpr." (vector-ref fpr-names i)))
                           (number->string i))
            (loop (+ i 1))))))

    (define (publish-class-info function)
      (analysis-put! 'codegen.reg-info.riscv64 function
                     'gpr-count (number->string (vector-length gpr-names)))
      (analysis-put! 'codegen.reg-info.riscv64 function
                     'fpr-count (number->string (vector-length fpr-names)))
      (analysis-put! 'codegen.reg-info.riscv64 function
                     'allocatable-gpr-count
                     (number->string (- (vector-length gpr-names) (length reserved-gpr))))
      (analysis-put! 'codegen.reg-info.riscv64 function
                     'allocatable-fpr-count
                     (number->string (vector-length fpr-names)))
      (analysis-put! 'codegen.reg-info.riscv64 function
                     'stack-pointer "sp")
      (analysis-put! 'codegen.reg-info.riscv64 function
                     'frame-pointer "s0")
      (analysis-put! 'codegen.reg-info.riscv64 function
                     'link-register "ra"))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.reg-info.riscv64)
        (error "codegen-reg-info-riscv64: unsupported capability" capability))
      (unless (list? options)
        (error "codegen-reg-info-riscv64: options must be an alist" options))
      (unless (glue-has? 'analysis-put!)
        (error "codegen-reg-info-riscv64: analysis accessors are unavailable"))
      (let functions ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (publish-register-info fn)
            (publish-class-info fn))
          (functions (+ fi 1))))
      ir)))
