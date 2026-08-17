;;; kernels/aggressive-dce.scm
;;; CCWeave Kernel: aggressive dead-code elimination using liveness.

(define-library (ccweave kernel aggressive-dce)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . aggressive-dce)
        (version     . "0.1.0")
        (description . "Eliminates dead instructions using liveness and side-effect analysis.")))

    (define (kernel-capabilities)
      '(opt.aggressive-dce))

    ;; An instruction is dead if it has no destination AND no side effects.
    (define (side-effect-free? opcode)
      (not (memq opcode '(istore fstore call icall return br))))

    (define (has-live-use? block ins dest)
      (let i ((ii 0))
        (if (>= ii (block-instr-count block))
            #f
            (let ((other (block-instr-ref block ii)))
              (if (uses-operand? other dest)
                  #t
                  (i (+ ii 1)))))))

    (define (uses-operand? ins name)
      (let o ((oi 0))
        (if (>= oi (instr-operand-count ins))
            #f
            (let ((op (instr-operand ins oi)))
              (if (and (eq? (operand-kind op) 'reg)
                       (string=? (operand-name op) name))
                  #t
                  (o (+ oi 1)))))))

    (define (eliminate-block! block)
      (let loop ((idx (- (block-instr-count block) 1)))
        (when (>= idx 0)
          (let ((ins (block-instr-ref block idx)))
            (when (and (side-effect-free? (instr-opcode ins))
                       (string? (instr-dest ins))
                       (not (has-live-use? block ins (instr-dest ins))))
              (instr-delete! ins)))
          (loop (- idx 1)))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.aggressive-dce)
        (error "aggressive-dce: unsupported capability" capability))
      (unless (list? options)
        (error "aggressive-dce: options must be an alist" options))
      (unless (and (glue-has? 'instr-dest) (glue-has? 'operand-kind)
                   (glue-has? 'operand-name))
        (error "aggressive-dce: accessors are unavailable"))
      (let f ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (let b ((bi 0))
              (when (< bi (function-block-count fn))
                (eliminate-block! (function-block-ref fn bi))
                (b (+ bi 1)))))
          (f (+ fi 1))))
      ir)))
