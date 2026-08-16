(define-library (ccweave kernel dominators)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . dominators) (version . "0.1.0")
        (description . "Computes block dominators and publishes reachability facts.")))
    (define (kernel-capabilities) '(analysis.dominators))

    (define (member-node? node nodes)
      (if (memv node nodes) #t #f))

    (define (set-intersection left right)
      (let loop ((rest left) (result '()))
        (if (null? rest)
            result
            (loop (cdr rest)
                  (if (member-node? (car rest) right)
                      (cons (car rest) result)
                      result)))))

    (define (set=? left right)
      (and (= (length left) (length right))
           (let loop ((rest left))
             (or (null? rest)
                 (and (member-node? (car rest) right)
                      (loop (cdr rest)))))))

    (define (blocks-of function)
      (let loop ((index 0) (result '()) (count (function-block-count function)))
        (if (>= index count)
            (reverse result)
            (loop (+ index 1) (cons (function-block-ref function index) result) count))))

    (define (predecessors block)
      (let loop ((index 0) (result '()) (count (block-pred-count block)))
        (if (>= index count)
            result
            (loop (+ index 1) (cons (block-pred-ref block index) result) count))))

    (define (lookup block table)
      (let ((entry (assv block table)))
        (if entry (cdr entry) '())))

    (define (intersect-predecessors predecessors table)
      (if (null? predecessors)
          '()
          (let loop ((rest (cdr predecessors)) (result (lookup (car predecessors) table)))
            (if (null? rest)
                result
                (loop (cdr rest) (set-intersection result (lookup (car rest) table)))))))

    (define (next-table blocks entry table)
      (let loop ((rest blocks) (result '()))
        (if (null? rest)
            (reverse result)
            (let* ((block (car rest))
                   (dominators
                    (if (= block entry)
                        (list block)
                        (let ((incoming (predecessors block)))
                          (if (null? incoming)
                              '()
                              (cons block (intersect-predecessors incoming table))))))
              (loop (cdr rest) (cons (cons block dominators) result)))))))

    (define (table=? left right)
      (let loop ((rest left))
        (or (null? rest)
            (and (set=? (cdr (car rest)) (lookup (caar rest) right))
                 (loop (cdr rest))))))

    (define (solve blocks entry)
      (let ((initial (map (lambda (block)
                            (cons block (if (= block entry) (list block) blocks)))
                          blocks)))
        (let loop ((table initial))
          (let ((next (next-table blocks entry table)))
            (if (table=? table next) next (loop next))))))

    (define (publish! table)
      (for-each
       (lambda (entry)
         (let ((block (car entry)) (dominators (cdr entry)))
           (analysis-put! 'analysis.dominators block 'reachable? (not (null? dominators)))
           (analysis-put! 'analysis.dominators block 'dominator-count (length dominators))
           (for-each
            (lambda (dominator)
              (analysis-put! 'analysis.dominators block
                             (string->symbol
                              (string-append "dominates-" (number->string dominator)))
                             #t))
            dominators)))
       table))

    (define (analyze-function! function)
      (let ((blocks (blocks-of function)))
        (unless (null? blocks) (publish! (solve blocks (car blocks))))))

    (define (kernel-apply capability ir options)
      (unless (eq? capability 'analysis.dominators)
        (error "dominators: unsupported capability" capability))
      (unless (and (glue-has? 'block-pred-count) (glue-has? 'analysis-put!))
        (error "dominators: Phase 2 accessors are unavailable"))
      (let loop ((index 0) (count (ir-function-count)))
        (when (< index count)
          (analyze-function! (ir-function-ref index))
          (loop (+ index 1) count)))
      ir)))
