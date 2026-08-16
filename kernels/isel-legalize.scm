;;; isel-legalize.scm
;;; CCWeave Kernel: Expands operations unsupported by the target into legal sequences.

(define-library ((ccweave kernel isel-legalize))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . isel-legalize)
        (version     . "1.0.0")
        (description . "Expands operations unsupported by the target into legal sequences.")))

    (define (kernel-capabilities)
      '(isel.legalization))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'isel.legalization)
        (error "isel-legalize: unsupported capability" capability))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (let* ((f (ir-function-ref i))
                   (nb (function-block-count f)))
              (let bloop ((j 0))
                (when (< j nb)
                  (let ((b (function-block-ref f j)))
                    (let ((ni (block-instr-count b)))
                      (let iloop ((k 0))
                        (when (< k ni)
                          (let ((ins (block-instr-ref b k)))
                            (let ((op (instr-opcode ins)))
                              ;; Placeholder: match opcode to target pattern.
                              (iloop (+ k 1))))))))
                  (bloop (+ j 1)))))
            (loop (+ i 1)))))
      ir)))
