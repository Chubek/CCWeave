;;; kernels/utf8-width-const.scm
;;; CCWeave Kernel: folds utf8proc_charwidth for constant codepoints.
;;; Character widths are: 0 (control/non-printable), 1 (narrow),
;;; 2 (wide/CJK), -1 (unassigned).  All ASCII printable codepoints
;;; (32–126) have width 1; ASCII controls (0–31, 127) have width 0.

(define-library (ccweave kernel utf8-width-const)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . utf8-width-const)
        (version     . "1.0.0")
        (description . "Folds utf8proc_charwidth calls for constant codepoints.")))

    (define (kernel-capabilities)
      '(opt.utf8-width-const))

    ;; The recognized width function.
    (define width-function "utf8proc_charwidth")

    ;; Returns #t if the call is to utf8proc_charwidth.
    (define (width-call? ins)
      (and (eq? (instr-opcode ins) 'call)
           (> (instr-operand-count ins) 0)
           (eq? (operand-kind (instr-operand ins 0)) 'func)
           (let ((name (operand-name (instr-operand ins 0))))
             (and name (string=? name width-function)))))

    ;; Compute the character width for a known ASCII codepoint.
    ;; Values per utf8proc_charwidth semantics (§utf8proc.h):
    ;;   0 — control / non-printable (U+0000–U+001F, U+007F)
    ;;   1 — narrow (U+0020–U+007E)
    ;;  -1 — unassigned / invalid (negative or > 0x10FFFF)
    (define (ascii-codepoint-width cp)
      (cond ((< cp 0)     -1)
            ((> cp 127)   1)  ;; non-ASCII: conservatively return 1
            ((< cp 32)    0)  ;; C0 controls
            ((= cp 127)   0)  ;; DEL
            (else         1))) ;; printable ASCII

    ;; Returns the constant width and a replacement instruction, or #f.
    (define (folding-for ins)
      (and (width-call? ins)
           (= (instr-operand-count ins) 2)  ;; func + codepoint arg
           (let ((arg (instr-operand ins 1)))
             (and (operand-const? arg)
                  (let* ((cp (const-int-value arg))
                         (w (ascii-codepoint-width cp))
                         (dest (instr-dest ins)))
                    (let ((replacement (instr-build 'imov (const-int-build w))))
                      (when dest
                        (instr-set-dest! replacement dest))
                      replacement))))))

    (define (rewrite-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (new (folding-for ins)))
                (when new (instr-replace! ins new))
                (loop (+ i 1) (if new (+ changed 1) changed)))))))

    (define (rewrite-function! f)
      (let ((n (function-block-count f)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (loop (+ i 1)
                    (+ changed (rewrite-block! (function-block-ref f i))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.utf8-width-const)
        (error "utf8-width-const: unsupported capability" capability))
      (unless (list? options)
        (error "utf8-width-const: options must be an alist" options))
      (unless (and (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!)
                   (glue-has? 'operand-kind)
                   (glue-has? 'operand-name))
        (error "utf8-width-const: scalar inspection accessors are unavailable"))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
