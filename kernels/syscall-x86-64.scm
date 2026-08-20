;;; CCWeave Kernel: Linux x86-64 system-call lowering.
;;; Numbers and register convention: .agents/SYSCALL-x86.txt and
;;; .agents/DOING-SYSCALLS.md.

(define-library (ccweave kernel syscall-x86-64)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . syscall-x86-64)
        (version . "0.1.0")
        (description . "Lowers the shared Linux syscall operation to x86-64 syscall.")))
    (define (kernel-capabilities) '(syscall.x86-64))

    ;; The table contains the stable Linux calls used by the low-level
    ;; runtime.  The complete numbering authority is SYSCALL-x86.txt.
    (define numbers
      '((read . 0) (write . 1) (open . 2) (close . 3)
        (mmap . 9) (munmap . 11) (ioctl . 16) (sched-yield . 24)
        (dup . 32) (dup2 . 33) (getpid . 39) (socket . 41)
        (exit . 60) (execve . 59) (fork . 57) (wait4 . 61)
        (openat . 257) (getrandom . 318)))

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
              (let ((new (apply instr-build
                                (cons 'x86-64.syscall (ops old))))
                    (dest (instr-dest old)))
                (when (string? dest) (instr-set-dest! new dest))
                (instr-replace! old new))))
          (loop (+ i 1)))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'syscall.x86-64)
        (error "syscall-x86-64: unsupported capability" capability))
      (unless (list? options)
        (error "syscall-x86-64: options must be an alist" options))
      (unless (glue-has? 'instr-set-dest!)
        (error "syscall-x86-64: scalar mutation accessors unavailable"))
      (let functions ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (let blocks ((bi 0))
              (when (< bi (function-block-count fn))
                (lower-block! (function-block-ref fn bi))
                (blocks (+ bi 1)))))
          (functions (+ fi 1))))
      ir)))
