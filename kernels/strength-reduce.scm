;;; kernels/strength-reduce.scm
;;; CCWeave Kernel: strength reduction for integer multiplication.

(define-library (ccweave kernel strength-reduce)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . strength-reduce)
        (version     . "1.0.0")
        (description . "Rewrites imul-by-power-of-two into shl.")))

    (define (kernel-capabilities)
      '(opt.strength-reduction))

    ;; Returns the exponent k if n = 2^k with k >= 1, else #f.
    (define (log2-exact n)
      (let loop ((n n) (k 0))
        (cond ((<= n 1) (and (= n 1) (> k 0) k))
              ((odd? n) #f)
              (else (loop (quotient n 2) (+ k 1))))))

    ;; If ins is (imul x const-2^k), build (shl x k) — else #f.
    (define (reduction-for ins)
      (and (eq? (instr-opcode ins) 'imul)
           (= (instr-operand-count ins) 2)
           (let ((a (instr-operand ins 0))
                 (b (instr-operand ins 1)))
             ;; Normalize: constant on the right.
             (let-values (((x c) (if (operand-const? a)
                                     (values b a)
                                     (values a b))))
               (and (operand-const? c)
                    (not (operand-const? x))
                    (let ((k (log2-exact (const-int-value c))))
                      (and k
                           (instr-build 'shl x (const-int-build k)))))))))

    (define (rewrite-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (new (reduction-for ins)))
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
      (unless (eq? capability 'opt.strength-reduction)
        (error "strength-reduce: unsupported capability" capability))
      (unless (list? options)
        (error "strength-reduce: options must be an alist" options))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
