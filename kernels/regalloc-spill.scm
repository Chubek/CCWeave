;;; kernels/regalloc-spill.scm
;;; CCWeave Kernel: spill code insertion for register allocation.
;;; Sources: .agents/ISA-Bundle/ register classes (§9).

(define-library (ccweave kernel regalloc-spill)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . regalloc-spill)
        (version . "0.1.0")
        (description . "Inserts spill and reload instructions when virtual registers exceed physical limits.")))
    (define (kernel-capabilities) '(codegen.regalloc-spill))

    ;; Reads codegen.regalloc-linear allocator-slot and codegen.reg-info
    ;; allocatable-gpr-count to determine which virtuals need spilling.
    ;; Defaults to 14 allocatable GPRs if no reg-info kernel has run.

    (define (allocatable-gpr-count function)
      (let ((count-str (analysis-ref 'codegen.reg-info.x86-64 function
                                     'allocatable-gpr-count)))
        (if count-str
            (string->number count-str)
            14)))

    (define (needs-spill? ins fn max-regs)
      (let ((slot-str (analysis-ref 'codegen.regalloc-linear ins
                                    'allocator-slot)))
        (and slot-str (>= (string->number slot-str) max-regs))))

    (define (spill-block! block fn max-regs)
      (let loop ((i 0))
        (when (< i (block-instr-count block))
          (let* ((ins (block-instr-ref block i))
                 (dest (instr-dest ins)))
            (if (and (string? dest) (needs-spill? ins fn max-regs))
                (let ((slot (analysis-ref 'codegen.regalloc-linear ins
                                          'allocator-slot)))
                  ;; Insert spill store after the definition.
                  (let ((spill (instr-build 'store
                                            (operand-reg-build (string->symbol "sp"))
                                            (operand-reg-build dest))))
                    (instr-insert-after! ins spill))
                  ;; Replace uses in subsequent instructions with reload.
                  (let uses ((j (+ i 1)))
                    (when (< j (block-instr-count block))
                      (let ((user (block-instr-ref block j)))
                        (let ops ((k 0))
                          (when (< k (instr-operand-count user))
                            (let ((op (instr-operand user k)))
                              (when (and (eq? (operand-kind op) 'reg)
                                         (string=? (operand-name op) dest))
                                (let ((tmp (string->symbol
                                            (string-append "spill." (number->string j)))))
                                  (instr-set-dest! (block-instr-ref block (- j 1)) tmp)
                                  (instr-set-operand! user k
                                                      (operand-reg-build tmp))))
                              (ops (+ k 1)))))
                        (uses (+ j 1))))))
                (loop (+ i 1)))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.regalloc-spill)
        (error "regalloc-spill: unsupported capability" capability))
      (unless (list? options)
        (error "regalloc-spill: options must be an alist" options))
      (unless (and (glue-has? 'analysis-ref)
                   (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!)
                   (glue-has? 'instr-insert-after!))
        (error "regalloc-spill: required Glue accessors are unavailable"))
      (let functions ((fi 0))
        (when (< fi (ir-function-count))
          (let ((fn (ir-function-ref fi)))
            (let ((mr (allocatable-gpr-count fn)))
              (let blocks ((bi 0))
                (when (< bi (function-block-count fn))
                  (spill-block! (function-block-ref fn bi) fn mr)
                  (blocks (+ bi 1))))))
          (functions (+ fi 1))))
      ir)))
