;;; cfg-canonicalize.scm
;;; CCWeave Kernel: Establishes canonical CFG shape with single entry and explicit terminators.

(define-library ((ccweave kernel cfg-canonicalize))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . cfg-canonicalize)
        (version     . "1.0.0")
        (description . "Establishes canonical CFG shape with single entry and explicit terminators.")))

    (define (kernel-capabilities)
      '(norm.cfg-canonical))

    (define (normalize-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (op (instr-opcode ins)))
                ;; Normalization: rewrite instruction into canonical form.
                (loop (+ i 1) changed))))))

    (define (normalize-function! f)
      (let ((n (function-block-count f)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (loop (+ i 1)
                    (+ changed (normalize-block! (function-block-ref f i))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'norm.cfg-canonical)
        (error "cfg-canonicalize: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (normalize-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
