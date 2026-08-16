(define-library (ccweave kernel anf-normalize)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . anf-normalize)
        (version . "0.1.0")
        (description . "Assigns administrative names to unnamed scalar computations.")))
    (define (kernel-capabilities) '(normalize.anf))

    (define (value-opcode? opcode)
      (memq opcode
            '(imov iadd isub imul idiv irem shl lshr ashr
              iand ior ixor ineg inot load call phi
              icmp.eq icmp.ne icmp.lt icmp.le icmp.gt icmp.ge
              match-eq match-ne)))

    (define (normalize-block! block next-name)
      (let loop ((index 0) (serial next-name))
        (if (>= index (block-instr-count block))
            serial
            (let ((instruction (block-instr-ref block index)))
              (if (and (value-opcode? (instr-opcode instruction))
                       (not (string? (instr-dest instruction))))
                  (begin
                    (instr-set-dest!
                      instruction
                      (string-append "anf." (number->string serial)))
                    (loop (+ index 1) (+ serial 1)))
                  (loop (+ index 1) serial))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'normalize.anf)
        (error "anf-normalize: unsupported capability" capability))
      (unless (list? options)
        (error "anf-normalize: options must be an alist" options))
      (unless (and (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!))
        (error "anf-normalize: scalar mutation accessors are unavailable"))
      (let functions ((function-index 0) (serial 0))
        (when (< function-index (ir-function-count))
          (let ((function (ir-function-ref function-index)))
            (let blocks ((block-index 0) (next serial))
              (if (>= block-index (function-block-count function))
                  (functions (+ function-index 1) next)
                  (blocks (+ block-index 1)
                          (normalize-block!
                            (function-block-ref function block-index)
                            next)))))))
      ir)))
