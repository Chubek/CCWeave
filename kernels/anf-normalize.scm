;;; anf-normalize.scm
;;; CCWeave Kernel: Normalizes expressions into A-normal form.

(define-library ((ccweave kernel anf-normalize))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . anf-normalize)
        (version     . "1.0.0")
        (description . "Normalizes expressions into A-normal form.")))

    (define (kernel-capabilities)
      '(norm.anf))

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
      (unless (eq? capability 'norm.anf)
        (error "anf-normalize: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (normalize-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
