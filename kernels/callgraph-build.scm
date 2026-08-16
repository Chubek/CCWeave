;;; kernels/callgraph-build.scm
;;; CCWeave Kernel: module call-graph construction.

(define-library (ccweave kernel callgraph-build)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . callgraph-build)
        (version     . "0.0.0")
        (description . "Constructs the module call graph, including conservative treatment of indirect calls via address-taken sets; prerequisite for interprocedural passes.")))

    (define (kernel-capabilities)
      '(analysis.callgraph))

    ;; Callee and address-taken queries require host extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.callgraph)
        (error "callgraph-build: unsupported capability" capability))
      (unless (list? options)
        (error "callgraph-build: options must be an alist" options))
      ir)))
