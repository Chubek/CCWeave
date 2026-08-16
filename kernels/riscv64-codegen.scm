;;; kernels/riscv64-codegen.scm
;;; CCWeave Kernel: RV64GC instruction selection.

(define-library (ccweave kernel riscv64-codegen)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name        . riscv64-codegen)
        (version     . "0.0.0")
        (description . "Instruction selection for RV64GC; maps Weave IR nodes to RISC-V machine nodes under the lp64d ABI.")))

    (define (kernel-capabilities)
      '(codegen.riscv64))

    ;; RV64 machine-node construction is a target-profile extension.
    (define (kernel-apply capability ir options)
      (unless (eq? capability 'codegen.riscv64)
        (error "riscv64-codegen: unsupported capability" capability))
      (unless (list? options)
        (error "riscv64-codegen: options must be an alist" options))
      ir)))
