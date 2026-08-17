;;; kernels/argument-promotion.scm
;;; CCWeave Kernel: promotes by-reference arguments to by-value.

(define-library (ccweave kernel argument-promotion)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . argument-promotion)
        (version     . "0.1.0")
        (description . "Promotes scalar by-reference arguments to by-value copies.")))

    (define (kernel-capabilities)
      '(opt.argument-promotion))

    ;; Detect pointer parameters that are only loaded, never stored.
    (define (promotable-param? fn param-idx)
      (and (glue-has? 'function-param-name)
           (let ((pname (function-param-name fn param-idx)))
             (and pname
                  (string? pname)
                  (> (string-length pname) 0)
                  (let b ((bi 0))
                    (if (>= bi (function-block-count fn))
                        #t
                        (let ((block (function-block-ref fn bi)))
                          (let i ((ii 0))
                            (if (>= ii (block-instr-count block))
                                (b (+ bi 1))
                                (let ((ins (block-instr-ref block ii)))
                                  (if (param-stored? ins pname)
                                      #f
                                      (i (+ ii 1)))))))))))))

    (define (param-stored? ins pname)
      (let o ((oi 0))
        (if (>= oi (instr-operand-count ins))
            #f
            (let ((op (instr-operand ins oi)))
              (if (and (eq? (operand-kind op) 'reg)
                       (string=? (operand-name op) pname)
                       (store-opcode? (instr-opcode ins)))
                  #t
                  (o (+ oi 1)))))))

    (define (store-opcode? opcode)
      (memq opcode '(istore fstore)))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.argument-promotion)
        (error "argument-promotion: unsupported capability" capability))
      (unless (list? options)
        (error "argument-promotion: options must be an alist" options))
      (unless (and (glue-has? 'function-param-name)
                   (glue-has? 'operand-kind)
                   (glue-has? 'operand-name))
        (error "argument-promotion: function inspection accessors are unavailable"))
      (let f ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (let p ((pi 0))
              (when (< pi (function-param-count fn))
                (when (promotable-param? fn pi)
                  (analysis-put! 'opt.argument-promotion fn pi 'promotable? #t))
                (p (+ pi 1)))))
          (f (+ fi 1))))
      ir)))
