;;; exception-lower.scm
;;; CCWeave Kernel: Lowers structured exception constructs to explicit control flow.

(define-library ((ccweave kernel exception-lower))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . exception-lower)
        (version     . "1.0.0")
        (description . "Lowers structured exception constructs to explicit control flow.")))

    (define (kernel-capabilities)
      '(lower.exceptions))

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
      (unless (eq? capability 'lower.exceptions)
        (error "exception-lower: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (lower-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
