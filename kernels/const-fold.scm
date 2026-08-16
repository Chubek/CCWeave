;;; kernels/const-fold.scm
;;; CCWeave Kernel: folds integer arithmetic on constant operands.

(define-library (ccweave kernel const-fold)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . const-fold)
        (version     . "1.0.0")
        (description . "Folds integer add/sub/mul on constant operands.")))

    (define (kernel-capabilities)
      '(opt.constant-folding))

    (define (fold-op opcode a b)
      (cond ((eq? opcode 'iadd) (+ a b))
            ((eq? opcode 'isub) (- a b))
            ((eq? opcode 'imul) (* a b))
            (else #f)))

    ;; (iadd c1 c2) -> (imov k). Equivalence only: no repair.
    (define (folding-for ins)
      (and (= (instr-operand-count ins) 2)
           (let ((a (instr-operand ins 0))
                 (b (instr-operand ins 1)))
             (and (operand-const? a)
                  (operand-const? b)
                  (let ((v (fold-op (instr-opcode ins)
                                    (const-int-value a)
                                    (const-int-value b))))
                    (and v (instr-build 'imov (const-int-build v))))))))

    (define (rewrite-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (new (folding-for ins)))
                (when new (instr-replace! ins new))
                (loop (+ i 1) (if new (+ changed 1) changed)))))))

    (define (rewrite-function! f)
      (let ((n (function-block-count f)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (loop (+ i 1)
                    (+ changed (rewrite-block! (function-block-ref f i))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.constant-folding)
        (error "const-fold: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
