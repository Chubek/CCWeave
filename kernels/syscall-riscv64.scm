;;; CCWeave Kernel: Linux RV64 system-call lowering.
;;; Numbers and register convention: .agents/SYSCALL-RV64.txt and
;;; .agents/DOING-SYSCALLS.md.

(define-library (ccweave kernel syscall-riscv64)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . syscall-riscv64)
        (version . "0.1.0")
        (description . "Lowers the shared Linux syscall operation to RV64 ecall.")))
    (define (kernel-capabilities) '(syscall.riscv64))

    ;; RV64 uses the asm-generic table (openat, not legacy open).
    (define numbers
      '((read . 63) (write . 64) (close . 57) (openat . 56)
        (mmap . 222) (munmap . 215) (ioctl . 29) (dup . 23)
        (dup3 . 24) (getpid . 172) (exit . 93) (exit-group . 94)
        (getrandom . 278)))

    (define (ops ins)
      (let loop ((i 0) (out '()))
        (if (>= i (instr-operand-count ins))
            (reverse out)
            (loop (+ i 1) (cons (instr-operand ins i) out)))))

    (define (lower-block! block)
      (let loop ((i 0))
        (when (< i (block-instr-count block))
          (let ((old (block-instr-ref block i)))
            (when (eq? (instr-opcode old) 'syscall)
              (let ((new (apply instr-build (cons 'riscv64.ecall (ops old))))
                    (dest (instr-dest old)))
                (when (string? dest) (instr-set-dest! new dest))
                (instr-replace! old new))))
          (loop (+ i 1)))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'syscall.riscv64)
        (error "syscall-riscv64: unsupported capability" capability))
      (unless (list? options)
        (error "syscall-riscv64: options must be an alist" options))
      (unless (glue-has? 'instr-set-dest!)
        (error "syscall-riscv64: scalar mutation accessors unavailable"))
      (let functions ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (let blocks ((bi 0))
              (when (< bi (function-block-count fn))
                (lower-block! (function-block-ref fn bi))
                (blocks (+ bi 1)))))
          (functions (+ fi 1))))
      ir)))
