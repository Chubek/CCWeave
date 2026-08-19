;;; Delphia interface lowering hook.  Interface-specific thunks are emitted
;;; by the frontend; this kernel records the capability boundary for hosts.
(define-library (ccweave kernel oop-interface-lower)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . oop-interface-lower)
        (version . "0.1.0")
        (description . "Lowers Delphi interface thunk and reference-count operations.")))
    (define (kernel-capabilities) '(lower.oop-interface))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.oop-interface)
        (error "oop-interface-lower: unsupported capability" capability))
      (unless (list? options)
        (error "oop-interface-lower: options must be an alist" options))
      ir)))
