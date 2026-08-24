;;; kernels/codegen-reg-info-x86-64.scm
;;; CCWeave Kernel: x86-64 register class information.
;;; Sources: .agents/ISA-Bundle/amd64.isa register classes and aliases.

(define-library (ccweave kernel codegen-reg-info-x86-64)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . codegen-reg-info-x86-64)
        (version . "0.1.0")
        (description . "Publishes x86-64 register class and allocation metadata.")))
    (define (kernel-capabilities) '(codegen.reg-info.x86-64))

    (define gpr-names
      '#("rax" "rcx" "rdx" "rsi" "rdi" "r8" "r9"
         "r10" "r11" "rbx" "r12" "r13" "r14" "r15"
         "rbp" "rsp"))

    (define fpr-names
      '#("xmm0" "xmm1" "xmm2" "xmm3" "xmm4" "xmm5" "xmm6" "xmm7"
         "xmm8" "xmm9" "xmm10" "xmm11" "xmm12" "xmm13" "xmm14" "xmm15"))

    (define caller-saved-gpr
      '(0 1 2 3 4 5 6 7 8 9 10 11))
    (define callee-saved-gpr
      '(9 12 13 14 15))
    (define caller-saved-fpr
      '(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15))
    (define callee-saved-fpr '())

    (define reserved-gpr '(14 15))

    (define (publish-register-info function)
      (let ((count (vector-length gpr-names)))
        (let loop ((i 0))
          (when (< i count)
            (analysis-put! 'codegen.reg-info.x86-64 function
                           (string->symbol (string-append "gpr." (vector-ref gpr-names i)))
                           (number->string i))
            (loop (+ i 1)))))
      (let ((count (vector-length fpr-names)))
        (let loop ((i 0))
          (when (< i count)
            (analysis-put! 'codegen.reg-info.x86-64 function
                           (string->symbol (string-append "fpr." (vector-ref fpr-names i)))
                           (number->string i))
            (loop (+ i 1))))))

    (define (publish-class-info function)
      (analysis-put! 'codegen.reg-info.x86-64 function
                     'gpr-count (number->string (vector-length gpr-names)))
      (analysis-put! 'codegen.reg-info.x86-64 function
                     'fpr-count (number->string (vector-length fpr-names)))
      (analysis-put! 'codegen.reg-info.x86-64 function
                     'allocatable-gpr-count
                     (number->string (- (vector-length gpr-names) (length reserved-gpr))))
      (analysis-put! 'codegen.reg-info.x86-64 function
                     'allocatable-fpr-count
                     (number->string (vector-length fpr-names)))
      (analysis-put! 'codegen.reg-info.x86-64 function
                     'stack-pointer "rsp")
      (analysis-put! 'codegen.reg-info.x86-64 function
                     'frame-pointer "rbp")
      (analysis-put! 'codegen.reg-info.x86-64 function
                     'link-register "none"))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.reg-info.x86-64)
        (error "codegen-reg-info-x86-64: unsupported capability" capability))
      (unless (list? options)
        (error "codegen-reg-info-x86-64: options must be an alist" options))
      (unless (glue-has? 'analysis-put!)
        (error "codegen-reg-info-x86-64: analysis accessors are unavailable"))
      (let functions ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (publish-register-info fn)
            (publish-class-info fn))
          (functions (+ fi 1))))
      ir)))
