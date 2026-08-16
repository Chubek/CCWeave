;;; tail-call.scm
;;; CCWeave Kernel: Converts self-recursive tail calls into loops.

(define-library ((ccweave kernel tail-call))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . tail-call)
        (version     . "1.0.0")
        (description . "Converts self-recursive tail calls into loops.")))

    (define (kernel-capabilities)
      '(opt.tail-call-elimination))

    ;; Predicate: return a replacement instruction (via instr-build) or #f.
    (define (opt-predicate ins)
      ;; Default: no transformation.  Concrete kernels override this
      ;; with their specific rewrite logic.
      #f)

    (define (rewrite-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (new (opt-predicate ins)))
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
      (unless (eq? capability 'opt.tail-call-elimination)
        (error "tail-call: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
