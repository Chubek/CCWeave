;;; deopt-points.scm
;;; CCWeave Kernel: Attaches deoptimization metadata at speculative On1x instructions.

(define-library ((ccweave kernel deopt-points))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . deopt-points)
        (version     . "1.0.0")
        (description . "Attaches deoptimization metadata at speculative On1x instructions.")))

    (define (kernel-capabilities)
      '(vm.deoptimization-metadata))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'vm.deoptimization-metadata)
        (error "deopt-points: unsupported capability" capability))
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
                              ;; Placeholder: VM-specific transform.
                              (iloop (+ k 1))))))))
                  (bloop (+ j 1)))))
            (loop (+ i 1)))))
      ir)))
