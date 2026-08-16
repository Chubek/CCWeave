;;; mem2reg.scm
;;; CCWeave Kernel: Promotes non-escaping stack slots to SSA values.

(define-library ((ccweave kernel mem2reg))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . mem2reg)
        (version     . "1.0.0")
        (description . "Promotes non-escaping stack slots to SSA values.")))

    (define (kernel-capabilities)
      '(lower.mem2reg))

    (define (lower-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (op (instr-opcode ins)))
                ;; Lowering placeholder: walk instructions, lower
                ;; unsupported opcodes to legal sequences.
                (loop (+ i 1) changed))))))

    (define (lower-function! f)
      (let ((n (function-block-count f)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (loop (+ i 1)
                    (+ changed (lower-block! (function-block-ref f i))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.mem2reg)
        (error "mem2reg: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (lower-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
