;;; ic-install.scm
;;; CCWeave Kernel: Allocates inline-cache slots at On1x dynamic dispatch sites.

(define-library ((ccweave kernel ic-install))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . ic-install)
        (version     . "1.0.0")
        (description . "Allocates inline-cache slots at On1x dynamic dispatch sites.")))

    (define (kernel-capabilities)
      '(vm.inline-cache-installation))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'vm.inline-cache-installation)
        (error "ic-install: unsupported capability" capability))
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
