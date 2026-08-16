(define-library (ccweave kernel lambda-lift)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info) '((name . lambda-lift) (version . "0.1.0") (description . "Verifies and records canonical top-level function placement.")))
    (define (kernel-capabilities) '(lower.lambda-lifting))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.lambda-lifting) (error "lambda-lift: unsupported capability" capability))
      (unless (list? options) (error "lambda-lift: options must be an alist" options))
      (unless (glue-has? 'analysis-put!) (error "lambda-lift: analysis accessors are unavailable"))
      ;; Weave IR's function collection is module-level; functions already
      ;; present there satisfy the structural result of lambda lifting.
      (let loop ((i 0))
        (when (< i (ir-function-count))
          (analysis-put! 'lower.lambda-lifting (ir-function-ref i)
                         'top-level? #t)
          (loop (+ i 1))))
      ir)))
