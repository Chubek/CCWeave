;;; Test fixture: triggers a host accessor failure (out-of-range index).
;;; The failure must reach the kernel as a Scheme condition.

(define-library (ccweave kernel accessor-error)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . accessor-error) (version . "0.0.1") (description . "accessor failure probe")))
    (define (kernel-capabilities) '(test.accessor-error))
    (define (kernel-apply capability ir options)
      (ir-function-ref 9999)
      ir)))
