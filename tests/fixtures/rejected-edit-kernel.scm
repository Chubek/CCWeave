;;; Test fixture: attempts a structural edit that the host rejects
;;; through its edit hook. The rejection must reach the kernel.

(define-library (ccweave kernel rejected-edit)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . rejected-edit) (version . "0.0.1") (description . "edit interposition probe")))
    (define (kernel-capabilities) '(test.rejected-edit))
    (define (kernel-apply capability ir options)
      (let* ((f (ir-function-ref 0))
             (b (function-block-ref f 0))
             (ins (block-instr-ref b 0))
             (new (instr-build 'imov (const-int-build 1))))
        (instr-replace! ins new))
      ir)))
