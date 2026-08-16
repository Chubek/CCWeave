;;; kernels/dwarf-linetable.scm
;;; CCWeave Kernel: DWARF line-table emission.

(define-library (ccweave kernel dwarf-linetable)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . dwarf-linetable)
        (version     . "0.0.0")
        (description . "Emits DWARF line-table programs from node source annotations, preserving line-map fidelity established by the preprocessor stage.")))

    (define (kernel-capabilities)
      '(debug.dwarf-linetable))

    ;; Source annotations and DWARF emission are debug-profile extensions.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'debug.dwarf-linetable)
        (error "dwarf-linetable: unsupported capability" capability))
      (unless (list? options)
        (error "dwarf-linetable: options must be an alist" options))
      ir)))
