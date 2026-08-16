;;; safepoint-insert.scm
;;; CCWeave Kernel: Inserts safepoints at loop back-edges and call sites in On1x modules.

(define-library ((ccweave kernel safepoint-insert))
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . safepoint-insert)
        (version     . "1.0.0")
        (description . "Inserts safepoints at loop back-edges and call sites in On1x modules.")))

    (define (kernel-capabilities)
      '(vm.safepoint-insertion))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'vm.safepoint-insertion)
        (error "safepoint-insert: unsupported capability" capability))
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
