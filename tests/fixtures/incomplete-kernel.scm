;;; Test fixture: exports only two of the three required procedures.
;;; Loading this MUST fail with CCW_ERR_LOAD.

(define-library (ccweave kernel incomplete)
  (import (scheme base))
  (export kernel-info kernel-capabilities)
  (begin
    (define (kernel-info)
      '((name . incomplete) (version . "0.0.1") (description . "missing kernel-apply")))
    (define (kernel-capabilities) '(test.incomplete))))
