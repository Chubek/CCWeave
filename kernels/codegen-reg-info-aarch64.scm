;;; kernels/codegen-reg-info-aarch64.scm
;;; CCWeave Kernel: AArch64 register class information.
;;; Sources: .agents/ISA-Bundle/arm64.isa register classes and aliases.

(define-library (ccweave kernel codegen-reg-info-aarch64)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . codegen-reg-info-aarch64)
        (version . "0.1.0")
        (description . "Publishes AArch64 register class and allocation metadata.")))
    (define (kernel-capabilities) '(codegen.reg-info.aarch64))

    ;; GPR: R0-R15, IP0(16), IP1(17), R18-R28, FP(29), LR(30), SP(31)
    (define gpr-names
      '#("x0" "x1" "x2" "x3" "x4" "x5" "x6" "x7"
         "x8" "x9" "x10" "x11" "x12" "x13" "x14" "x15"
         "x16" "x17" "x18" "x19" "x20" "x21" "x22" "x23"
         "x24" "x25" "x26" "x27" "x28" "x29" "x30" "sp"))

    ;; FPR: V0-V30 (128-bit)
    (define fpr-names
      '#("v0" "v1" "v2" "v3" "v4" "v5" "v6" "v7"
         "v8" "v9" "v10" "v11" "v12" "v13" "v14" "v15"
         "v16" "v17" "v18" "v19" "v20" "v21" "v22" "v23"
         "v24" "v25" "v26" "v27" "v28" "v29" "v30"))

    ;; Caller-saved: x0-x18, all V registers (AAPCS64).
    (define caller-saved-gpr '(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17))
    (define callee-saved-gpr '(19 20 21 22 23 24 25 26 27 28 29))
    (define caller-saved-fpr '(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
                               16 17 18 19 20 21 22 23 24 25 26 27 28 29 30))
    (define callee-saved-fpr '(8 9 10 11 12 13 14 15))

    (define reserved-gpr '(30 31))

    (define (publish-register-info function)
      (let ((count (vector-length gpr-names)))
        (let loop ((i 0))
          (when (< i count)
            (analysis-put! 'codegen.reg-info.aarch64 function
                           (string->symbol (string-append "gpr." (vector-ref gpr-names i)))
                           (number->string i))
            (loop (+ i 1)))))
      (let ((count (vector-length fpr-names)))
        (let loop ((i 0))
          (when (< i count)
            (analysis-put! 'codegen.reg-info.aarch64 function
                           (string->symbol (string-append "fpr." (vector-ref fpr-names i)))
                           (number->string i))
            (loop (+ i 1))))))

    (define (publish-class-info function)
      (analysis-put! 'codegen.reg-info.aarch64 function
                     'gpr-count (number->string (vector-length gpr-names)))
      (analysis-put! 'codegen.reg-info.aarch64 function
                     'fpr-count (number->string (vector-length fpr-names)))
      (analysis-put! 'codegen.reg-info.aarch64 function
                     'allocatable-gpr-count
                     (number->string (- (vector-length gpr-names) (length reserved-gpr))))
      (analysis-put! 'codegen.reg-info.aarch64 function
                     'allocatable-fpr-count
                     (number->string (vector-length fpr-names)))
      (analysis-put! 'codegen.reg-info.aarch64 function
                     'stack-pointer "sp")
      (analysis-put! 'codegen.reg-info.aarch64 function
                     'frame-pointer "x29")
      (analysis-put! 'codegen.reg-info.aarch64 function
                     'link-register "x30"))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.reg-info.aarch64)
        (error "codegen-reg-info-aarch64: unsupported capability" capability))
      (unless (list? options)
        (error "codegen-reg-info-aarch64: options must be an alist" options))
      (unless (glue-has? 'analysis-put!)
        (error "codegen-reg-info-aarch64: analysis accessors are unavailable"))
      (let functions ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (publish-register-info fn)
            (publish-class-info fn))
          (functions (+ fi 1))))
      ir)))
