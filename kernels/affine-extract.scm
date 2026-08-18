(define-library (ccweave kernel affine-extract)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . affine-extract)
        (version . "0.1.0")
        (description . "Publishes deterministic affine-region or nonaffine facts for loop regions.")))
    (define (kernel-capabilities) '(analysis.affine))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.affine)
        (error "affine-extract: unsupported capability" capability))
      (unless (list? options)
        (error "affine-extract: options must be an alist" options))
      ;; Hosts with the ISL analysis extension may replace these facts with
      ;; canonical ISL strings; portable kernels retain the explicit marker.
      (when (glue-has? 'analysis-put!)
        (analysis-put! 'analysis.affine ir 'status 'nonaffine))
      ir)))
