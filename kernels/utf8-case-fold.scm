;;; kernels/utf8-case-fold.scm
;;; CCWeave Kernel: folds utf8proc case conversion calls when the input
;;; is a constant ASCII character or string.  ASCII case folding is
;;; trivial: lower↔upper within [A-Za-z]; everything else is identity.

(define-library (ccweave kernel utf8-case-fold)
  (import (scheme base)
          (scheme char)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . utf8-case-fold)
        (version     . "1.0.0")
        (description . "Folds utf8proc case conversion for constant ASCII inputs.")))

    (define (kernel-capabilities)
      '(opt.utf8-case-fold))

    ;; Recognized utf8proc case functions.
    (define case-functions
      '("utf8proc_tolower" "utf8proc_toupper"))

    ;; Returns #t if the call is to a recognized utf8proc case function.
    (define (case-call? ins)
      (and (eq? (instr-opcode ins) 'call)
           (> (instr-operand-count ins) 0)
           (eq? (operand-kind (instr-operand ins 0)) 'func)
           (let ((name (operand-name (instr-operand ins 0))))
             (and name
                  (member name case-functions string=?)))))

    ;; ASCII case folding: map a codepoint to its folded form.
    (define (fold-ascii-codepoint cp to-lower?)
      (if to-lower?
          (if (and (>= cp 65) (<= cp 90)) (+ cp 32) cp)   ;; A-Z → a-z
          (if (and (>= cp 97) (<= cp 122)) (- cp 32) cp))) ;; a-z → A-Z

    ;; Returns the folded constant and a replacement instruction, or #f.
    (define (folding-for ins)
      (and (case-call? ins)
           (= (instr-operand-count ins) 2)  ;; func + codepoint arg
           (let ((arg (instr-operand ins 1)))
             (and (operand-const? arg)
                  (let* ((cp (const-int-value arg))
                         (callee (operand-name (instr-operand ins 0)))
                         (to-lower? (string=? callee "utf8proc_tolower"))
                         (folded (fold-ascii-codepoint cp to-lower?))
                         (dest (instr-dest ins)))
                    (let ((replacement (instr-build 'imov (const-int-build folded))))
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
      (unless (eq? capability 'opt.utf8-case-fold)
        (error "utf8-case-fold: unsupported capability" capability))
      (unless (list? options)
        (error "utf8-case-fold: options must be an alist" options))
      (unless (and (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!)
                   (glue-has? 'operand-kind)
                   (glue-has? 'operand-name))
        (error "utf8-case-fold: scalar inspection accessors are unavailable"))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
