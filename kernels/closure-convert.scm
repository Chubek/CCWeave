;;; closure-convert.scm
;;; CCWeave Kernel: Converts free-variable references into explicit environment records.

(define-library ((ccweave kernel closure-convert))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . closure-convert)
        (version     . "1.0.0")
        (description . "Converts free-variable references into explicit environment records.")))

    (define (kernel-capabilities)
      '(lower.closure-conversion))

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
      (unless (eq? capability 'lower.closure-conversion)
        (error "closure-convert: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (lower-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
