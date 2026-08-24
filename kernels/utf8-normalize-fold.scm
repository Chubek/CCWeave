;;; kernels/utf8-normalize-fold.scm
;;; CCWeave Kernel: folds utf8proc Unicode normalization calls when the
;;; input string is a constant ASCII string (ASCII is invariant under
;;; all Unicode normalization forms).

(define-library (ccweave kernel utf8-normalize-fold)
  (import (scheme base)
          (scheme char)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . utf8-normalize-fold)
        (version     . "1.0.0")
        (description . "Folds utf8proc normalization calls for constant ASCII input strings.")))

    (define (kernel-capabilities)
      '(opt.utf8-normalize-fold))

    ;; The set of utf8proc normalization functions that are identity on ASCII.
    (define normalize-functions
      '("utf8proc_NFD" "utf8proc_NFC" "utf8proc_NFKD" "utf8proc_NFKC"))

    ;; Returns #t if the call is to a recognized utf8proc normalization function.
    (define (normalization-call? ins)
      (and (eq? (instr-opcode ins) 'call)
           (> (instr-operand-count ins) 0)
           (eq? (operand-kind (instr-operand ins 0)) 'func)
           (let ((name (operand-name (instr-operand ins 0))))
             (and name
                  (member name normalize-functions string=?)))))

    ;; Returns #t if the operand is a constant pointer to an ASCII string.
    (define (ascii-string-operand? operand)
      (and (operand-const? operand)
           (eq? (operand-kind operand) 'const-int)))

    (define (constant-ascii-input? ins)
      (and (normalization-call? ins)
           (= (instr-operand-count ins) 2)  ;; func + string arg
           (let ((arg (instr-operand ins 1)))
             (ascii-string-operand? arg))))

    ;; Rewrite a normalization call whose input is known ASCII to a simple
    ;; copy of the input. The identity: utf8proc_NFD(ascii) = ascii.
    (define (folding-for ins)
      (and (constant-ascii-input? ins)
           (let ((arg (instr-operand ins 1))
                 (dest (instr-dest ins)))
             (let ((replacement (instr-build 'imov arg)))
               (when dest
                 (instr-set-dest! replacement dest))
               replacement))))

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
      (unless (eq? capability 'opt.utf8-normalize-fold)
        (error "utf8-normalize-fold: unsupported capability" capability))
      (unless (list? options)
        (error "utf8-normalize-fold: options must be an alist" options))
      (unless (and (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!)
                   (glue-has? 'operand-kind)
                   (glue-has? 'operand-name))
        (error "utf8-normalize-fold: scalar inspection accessors are unavailable"))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
