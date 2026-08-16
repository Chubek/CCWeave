;;; kernels/profile-report.scm
;;; CCWeave Kernel: read-only inspection; reports module shape.
;;; Exercises the navigation accessors without mutating anything.

(define-library (ccweave kernel profile-report)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . profile-report)
        (version     . "1.0.0")
        (description . "Counts functions, blocks, and instructions per module.")))

    (define (kernel-capabilities)
      '(analysis.module-shape))

    (define (count-instructions f)
      (let ((nb (function-block-count f)))
        (let loop ((i 0) (total 0))
          (if (>= i nb)
              total
              (loop (+ i 1)
                    (+ total (block-instr-count (function-block-ref f i))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.module-shape)
        (error "profile-report: unsupported capability" capability))
      ;; Feature-test before touching anything beyond the Core Accessor Set.
      (let ((profile (if (glue-has? 'ir-profile) (ir-profile) 'unknown))
            (n (ir-function-count)))
        (let loop ((i 0) (instrs 0))
          (if (>= i n)
              (list profile n instrs)
              (loop (+ i 1)
                    (+ instrs (count-instructions (ir-function-ref i)))))))
      ir)))
