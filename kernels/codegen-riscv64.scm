;;; codegen-riscv64.scm
;;; CCWeave Kernel: Emits RV64GC machine code from Tilly-profile modules.

(define-library ((ccweave kernel codegen-riscv64))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . codegen-riscv64)
        (version     . "1.0.0")
        (description . "Emits RV64GC machine code from Tilly-profile modules.")))

    (define (kernel-capabilities)
      '(codegen.riscv64))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.riscv64)
        (error "codegen-riscv64: unsupported capability" capability))
      ;; Walk all functions and emit target machine code.
      ;; The host provides the output buffer via options.
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (let* ((f (ir-function-ref i))
                   (nb (function-block-count f)))
              (let bloop ((j 0))
                (when (< j nb)
                  (let ((b (function-block-ref f j)))
                    (let ((ni (block-instr-count b)))
                      (let iloop ((k 0))
                        (when (< k ni)
                          (let ((ins (block-instr-ref b k)))
                            (let ((op (instr-opcode ins)))
                              ;; Placeholder: emit target machine code.
                              (iloop (+ k 1))))))))
                  (bloop (+ j 1)))))
            (loop (+ i 1)))))
      ir)))
