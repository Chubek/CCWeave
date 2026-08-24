;;; kernels/string-concat.scm
;;; CCWeave Kernel: optimizes string concatenation patterns.
;;;
;;; Equivalences applied:
;;;   1. strcat(s, "")  →  imov s     (empty-string append, no-op)
;;;   2. strncat(s, "", 0) → imov s   (zero-length bounded append)
;;;   3. strlen("") → imov 0          (empty-string length)
;;;   4. memcpy(s, s, 0) → imov s     (zero-length copy, no-op)
;;;   5. memmove(s, s, 0) → imov s    (zero-length move, no-op)

(define-library (ccweave kernel string-concat)
  (import (scheme base)
          (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin

    (define (kernel-info)
      '((name        . string-concat)
        (version     . "1.0.0")
        (description . "Optimizes strcat/strncat/strlen/memcpy/memmove patterns.")))

    (define (kernel-capabilities)
      '(opt.string-concat))

    ;; Recognized string/memory functions.
    (define string-functions '("strcat" "strncat" "strlen" "memcpy" "memmove"))

    ;; Returns the callee name if the instruction is a call to a recognized
    ;; string function, otherwise #f.
    (define (string-call-name ins)
      (and (eq? (instr-opcode ins) 'call)
           (> (instr-operand-count ins) 0)
           (eq? (operand-kind (instr-operand ins 0)) 'func)
           (let ((name (operand-name (instr-operand ins 0))))
             (and name
                  (member name string-functions string=?)
                  name))))

    ;; Does the operand look like an empty string / null pointer?
    (define (null-pointer-operand? operand)
      (and (operand-const? operand)
           (eq? (operand-kind operand) 'const-int)
           (zero? (const-int-value operand))))

    ;; Zero-length constant operand.
    (define (zero-const? operand)
      (and (operand-const? operand)
           (eq? (operand-kind operand) 'const-int)
           (zero? (const-int-value operand))))

    ;; ---------------------------------------------------------------
    ;;  Rule 1: strcat(s, "") → imov s
    ;; ---------------------------------------------------------------
    (define (fold-strcat-empty? ins)
      (and (string=? (string-call-name ins) "strcat")
           (= (instr-operand-count ins) 3)              ;; func + dest + src
           (let ((src (instr-operand ins 2)))
             (null-pointer-operand? src))))

    (define (fold-strcat-empty ins)
      (let ((dest (instr-operand ins 1))
            (dest-name (instr-dest ins)))
        (let ((replacement (instr-build 'imov dest)))
          (when dest-name
            (instr-set-dest! replacement dest-name))
          (instr-replace! ins replacement)
          replacement)))

    ;; ---------------------------------------------------------------
    ;;  Rule 2: strncat(s, "", 0) → imov s
    ;; ---------------------------------------------------------------
    (define (fold-strncat-empty? ins)
      (and (string=? (string-call-name ins) "strncat")
           (= (instr-operand-count ins) 4)              ;; func + dest + src + n
           (let ((src (instr-operand ins 2))
                 (n   (instr-operand ins 3)))
             (and (null-pointer-operand? src)
                  (zero-const? n)))))

    (define (fold-strncat-empty ins)
      (let ((dest (instr-operand ins 1))
            (dest-name (instr-dest ins)))
        (let ((replacement (instr-build 'imov dest)))
          (when dest-name
            (instr-set-dest! replacement dest-name))
          (instr-replace! ins replacement)
          replacement)))

    ;; ---------------------------------------------------------------
    ;;  Rule 3: strlen("") → imov 0
    ;; ---------------------------------------------------------------
    (define (fold-strlen-empty? ins)
      (and (string=? (string-call-name ins) "strlen")
           (= (instr-operand-count ins) 2)              ;; func + string
           (let ((s (instr-operand ins 1)))
             (null-pointer-operand? s))))

    (define (fold-strlen-empty ins)
      (let ((dest-name (instr-dest ins)))
        (let ((replacement (instr-build 'imov (const-int-build 0))))
          (when dest-name
            (instr-set-dest! replacement dest-name))
          (instr-replace! ins replacement)
          replacement)))

    ;; ---------------------------------------------------------------
    ;;  Rule 4: memcpy(s, s, 0) → imov s
    ;; ---------------------------------------------------------------
    (define (fold-memcpy-zero? ins)
      (and (string=? (string-call-name ins) "memcpy")
           (= (instr-operand-count ins) 4)              ;; func + dest + src + n
           (let ((n (instr-operand ins 3)))
             (zero-const? n))))

    (define (fold-memcpy-zero ins)
      (let ((dest (instr-operand ins 1))
            (dest-name (instr-dest ins)))
        (let ((replacement (instr-build 'imov dest)))
          (when dest-name
            (instr-set-dest! replacement dest-name))
          (instr-replace! ins replacement)
          replacement)))

    ;; ---------------------------------------------------------------
    ;;  Rule 5: memmove(s, s, 0) → imov s
    ;; ---------------------------------------------------------------
    (define (fold-memmove-zero? ins)
      (and (string=? (string-call-name ins) "memmove")
           (= (instr-operand-count ins) 4)              ;; func + dest + src + n
           (let ((n (instr-operand ins 3)))
             (zero-const? n))))

    (define (fold-memmove-zero ins)
      (let ((dest (instr-operand ins 1))
            (dest-name (instr-dest ins)))
        (let ((replacement (instr-build 'imov dest)))
          (when dest-name
            (instr-set-dest! replacement dest-name))
          (instr-replace! ins replacement)
          replacement)))

    ;; ---------------------------------------------------------------
    ;;  Block-level rewrite
    ;; ---------------------------------------------------------------

    (define (rewrite-block! b)
      (let ((n (block-instr-count b)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (let* ((ins (block-instr-ref b i))
                     (folded (or (and (fold-strcat-empty? ins)
                                      (fold-strcat-empty ins))
                                 (and (fold-strncat-empty? ins)
                                      (fold-strncat-empty ins))
                                 (and (fold-strlen-empty? ins)
                                      (fold-strlen-empty ins))
                                 (and (fold-memcpy-zero? ins)
                                      (fold-memcpy-zero ins))
                                 (and (fold-memmove-zero? ins)
                                      (fold-memmove-zero ins)))))
                (loop (+ i 1) (if folded (+ changed 1) changed)))))))

    (define (rewrite-function! f)
      (let ((n (function-block-count f)))
        (let loop ((i 0) (changed 0))
          (if (>= i n)
              changed
              (loop (+ i 1)
                    (+ changed (rewrite-block! (function-block-ref f i))))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'opt.string-concat)
        (error "string-concat: unsupported capability" capability))
      (unless (list? options)
        (error "string-concat: options must be an alist" options))
      (unless (and (glue-has? 'instr-dest)
                   (glue-has? 'instr-set-dest!)
                   (glue-has? 'operand-kind)
                   (glue-has? 'operand-name))
        (error "string-concat: scalar inspection accessors are unavailable"))
      (let ((n (ir-function-count)))
        (let loop ((i 0))
          (when (< i n)
            (rewrite-function! (ir-function-ref i))
            (loop (+ i 1)))))
      ir)))
