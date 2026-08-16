;;; Test fixture: calls a glue accessor with the wrong argument count.
;;; The executor must raise in Scheme without invoking the accessor.

(define-library (ccweave kernel bad-arity)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . bad-arity) (version . "0.0.1") (description . "arity violation probe")))
    (define (kernel-capabilities) '(test.arity))
    (define (kernel-apply capability ir options)
      (ir-function-ref 0 1 2)
      ir)))
