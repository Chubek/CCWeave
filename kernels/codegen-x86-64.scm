;;; kernels/codegen-x86-64.scm
;;; CCWeave Kernel: x86-64 instruction selection.

(define-library (ccweave kernel codegen-x86-64)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . codegen-x86-64) (version . "0.2.0")
        (description . "Selects a scalar x86-64 instruction subset covering arithmetic, compares, memory, and control flow.")))
    (define (kernel-capabilities) '(codegen.x86-64))

    ;; §5.4: Weave IR's conventional scalar-integer inventory, mapped to
    ;; x86-64 mnemonics one opcode at a time. Same-arity, same-operand-order
    ;; renaming only -- register/immediate legalization is isel-legalize's
    ;; job (§9), and physical allocation is regalloc's.
    (define op-table
      '((imov       . x86-64.mov)
        (iadd       . x86-64.add)
        (isub       . x86-64.sub)
        (imul       . x86-64.imul)
        (idiv       . x86-64.idiv)
        (irem       . x86-64.idiv-rem)
        (iand       . x86-64.and)
        (ior        . x86-64.or)
        (ixor       . x86-64.xor)
        (shl        . x86-64.shl)
        (lshr       . x86-64.shr)
        (ashr       . x86-64.sar)
        (ineg       . x86-64.neg)
        (inot       . x86-64.not)
        (icmp.eq    . x86-64.cmp.eq)
        (icmp.ne    . x86-64.cmp.ne)
        (icmp.lt    . x86-64.cmp.lt)
        (icmp.le    . x86-64.cmp.le)
        (icmp.gt    . x86-64.cmp.gt)
        (icmp.ge    . x86-64.cmp.ge)
        (load       . x86-64.load)
        (store      . x86-64.store)
        (jmp        . x86-64.jmp)
        (br         . x86-64.br)
        (ret        . x86-64.ret)
        (call       . x86-64.call)
        (syscall    . x86-64.syscall)
        (call.dynamic . x86-64.call.dynamic)
        (call.virtual . x86-64.call.virtual)
        (phi        . x86-64.phi)))

    (define (mapped op)
      (let ((entry (assq op op-table)))
        (and entry (cdr entry))))

    (define (ops ins)
      (let loop ((i 0) (xs '())) (if (>= i (instr-operand-count ins)) (reverse xs)
        (loop (+ i 1) (cons (instr-operand ins i) xs)))))

    (define (select! b)
      (let loop ((i 0)) (when (< i (block-instr-count b))
        (let* ((old (block-instr-ref b i)) (op (mapped (instr-opcode old))))
          (when op (let ((new (apply instr-build (cons op (ops old)))) (d (instr-dest old)))
            (when (string? d) (instr-set-dest! new d)) (instr-replace! old new)))
          (loop (+ i 1))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.x86-64) (error "codegen-x86-64: unsupported capability" capability))
      (unless (list? options) (error "codegen-x86-64: options must be an alist" options))
      (unless (glue-has? 'instr-set-dest!) (error "codegen-x86-64: scalar mutation accessors are unavailable"))
      (let f ((fi 0)) (when (< fi (ir-function-count))
        (let ((fn (ir-function-ref fi))) (let b ((bi 0)) (when (< bi (function-block-count fn))
          (select! (function-block-ref fn bi)) (b (+ bi 1))))) (f (+ fi 1))))
      ir)))
