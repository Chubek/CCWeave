;;; kernels/bitfield-lower.scm
;;; CCWeave Kernel: bitfield lowering.

(define-library (ccweave kernel bitfield-lower)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . bitfield-lower)
        (version . "0.1.0")
        (description . "Expands constant-width 64-bit bitfield extracts into shift-and-mask sequences.")))
    (define (kernel-capabilities) '(lower.bitfield))

    (define (power-of-two exponent)
      (let loop ((remaining exponent) (value 1))
        (if (= remaining 0)
            value
            (loop (- remaining 1) (* value 2)))))

    (define (lower-extract! block index instruction serial)
      (and (eq? (instr-opcode instruction) 'bitfield-extract)
           (= (instr-operand-count instruction) 3)
           (let ((offset (instr-operand instruction 1))
                 (width (instr-operand instruction 2)))
             (and (eq? (operand-kind offset) 'const-int)
                  (eq? (operand-kind width) 'const-int)
                  (let ((offset-value (const-int-value offset))
                        (width-value (const-int-value width)))
                    (and (>= offset-value 0)
                         (> width-value 0)
                         (< width-value 63)
                         (< (+ offset-value width-value) 64)
                         (let* ((temporary
                                  (string-append
                                    "bitfield." (number->string serial)))
                                (shift
                                  (instr-build
                                    'lshr
                                    (instr-operand instruction 0)
                                    (const-int-build offset-value)))
                                (replacement
                                  (instr-build
                                    'iand
                                    (operand-reg-build temporary)
                                    (const-int-build
                                      (- (power-of-two width-value) 1))))
                                (destination (instr-dest instruction)))
                           (instr-set-dest! shift temporary)
                           (when (string? destination)
                             (instr-set-dest! replacement destination))
                           (instr-insert-before! instruction shift)
                           (instr-replace! instruction replacement)
                           #t)))))))

    (define (lower-block! block next-serial)
      (let loop ((index 0) (serial next-serial))
        (if (>= index (block-instr-count block))
            serial
            (if (lower-extract!
                  block index (block-instr-ref block index) serial)
                (loop (+ index 2) (+ serial 1))
                (loop (+ index 1) serial)))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'lower.bitfield)
        (error "bitfield-lower: unsupported capability" capability))
      (unless (list? options)
        (error "bitfield-lower: options must be an alist" options))
      (unless (and (glue-has? 'operand-kind)
                   (glue-has? 'operand-reg-build)
                   (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!))
        (error "bitfield-lower: scalar mutation accessors are unavailable"))
      (let functions ((function-index 0) (serial 0))
        (when (< function-index (ir-function-count))
          (let ((function (ir-function-ref function-index)))
            (let blocks ((block-index 0) (next serial))
              (if (>= block-index (function-block-count function))
                  (functions (+ function-index 1) next)
                  (blocks (+ block-index 1)
                          (lower-block!
                            (function-block-ref function block-index)
                            next)))))))
      ir)))
