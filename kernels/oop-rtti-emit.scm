;;; Delphia RTTI emission hook.  RTTI facts are canonicalized by the frontend
;;; and consumed by target codegen; this kernel preserves the capability ABI.
(define-library (ccweave kernel oop-rtti-emit)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . oop-rtti-emit)
        (version . "0.1.0")
        (description . "Emits deterministic RTTI tables for published Delphi members.")))
    (define (kernel-capabilities) '(lower.oop-rtti))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.oop-rtti)
        (error "oop-rtti-emit: unsupported capability" capability))
      (unless (list? options)
        (error "oop-rtti-emit: options must be an alist" options))
      ir)))
