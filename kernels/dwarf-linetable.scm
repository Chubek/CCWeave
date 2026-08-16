;;; kernels/dwarf-linetable.scm
;;; CCWeave Kernel: DWARF line-table emission.

(define-library (ccweave kernel dwarf-linetable)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . dwarf-linetable) (version . "0.1.0")
        (description . "Publishes deterministic instruction ordinals for line-table emission.")))
    (define (kernel-capabilities) '(debug.dwarf-linetable))
    (define (number-function! fn next)
      (let b ((bi 0) (line next))
        (if (>= bi (function-block-count fn)) line
            (let ((block (function-block-ref fn bi)))
              (let i ((ii 0) (current line))
                (if (>= ii (block-instr-count block))
                    (b (+ bi 1) current)
                    (begin
                      (analysis-put! 'debug.dwarf-linetable
                                     (block-instr-ref block ii)
                                     'line-ordinal current)
                      (i (+ ii 1) (+ current 1)))))))))
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'debug.dwarf-linetable)
        (error "dwarf-linetable: unsupported capability" capability))
      (unless (list? options)
        (error "dwarf-linetable: options must be an alist" options))
      (unless (glue-has? 'analysis-put!) (error "dwarf-linetable: analysis accessors are unavailable"))
      (let f ((fi 0) (line 1))
        (when (< fi (ir-function-count))
          (f (+ fi 1) (number-function! (ir-function-ref fi) line))))
      ir)))
