;;; lambda-lift.scm
;;; CCWeave Kernel: Lifts nested functions to top level with extra parameters.

(define-library ((ccweave kernel lambda-lift))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . lambda-lift)
        (version     . "1.0.0")
        (description . "Lifts nested functions to top level with extra parameters.")))

    (define (kernel-capabilities)
      '(lower.lambda-lifting))

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
      (unless (eq? capability 'lower.lambda-lifting)
        (error "lambda-lift: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (lower-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
