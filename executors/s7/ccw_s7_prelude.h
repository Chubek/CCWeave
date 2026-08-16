/* Engine-side R7RS shim for the reference executor.
 *
 * Kernels are portable R7RS `define-library` modules; S7 is not an R7RS
 * module system, so the executor supplies the library form itself. This
 * is executor-side only: nothing here leaks into kernel source, which
 * stays engine-agnostic. */

#ifndef CCW_S7_PRELUDE_H
#define CCW_S7_PRELUDE_H

/* R7RS-small forms that this S7 predates. Kernels are portable R7RS and
 * may use them; supplying them executor-side keeps kernel source free of
 * engine-specific conditionals. The vendored s7 r7rs.scm is not used: it
 * requires compiling C at load time, which the build forbids. */
static const char *const CCW_S7_R7RS =
"(begin\n"
"(define-macro (let-values bindings . body)\n"
"  (if (null? bindings)\n"
"      `(begin ,@body)\n"
"      (let ((b (car bindings)))\n"
"        `(call-with-values (lambda () ,(cadr b))\n"
"           (lambda ,(car b) (let-values ,(cdr bindings) ,@body))))))\n"
"\n"
"(define-macro (let*-values bindings . body)\n"
"  `(let-values ,bindings ,@body))\n"
"\n"
"(define-macro (define-values formals expr)\n"
"  `(begin\n"
"     ,@(map (lambda (n) `(define ,n #f)) formals)\n"
"     (call-with-values (lambda () ,expr)\n"
"       (lambda args\n"
"         ,@(let loop ((names formals) (i 0) (sets '()))\n"
"             (if (null? names)\n"
"                 (reverse sets)\n"
"                 (loop (cdr names) (+ i 1)\n"
"                       (cons `(set! ,(car names) (list-ref args ,i)) sets))))))))\n"
"\n"
"(define (exact-integer? x) (and (integer? x) (exact? x)))\n"
"(define (square x) (* x x))\n"
"(define (list-copy lst) (if (pair? lst) (append lst '()) lst))\n"
"(define (boolean=? a b . rest)\n"
"  (and (eq? a b) (or (null? rest) (apply boolean=? b rest))))\n"
"(define (error-object? obj) (pair? obj))\n"
"(define (error-object-message obj) (if (pair? obj) (car obj) \"\"))\n"
"(define (error-object-irritants obj) (if (pair? obj) (cdr obj) '()))\n"
"(define (raise obj) (error 'ccw-raise \"~A\" obj))\n"
")\n";

static const char *const CCW_S7_PRELUDE =
"(begin\n"
"(define *ccw-libraries* (inlet))\n"
"(define *ccw-last-library* #f)\n"
"\n"
";; (define-library <name> (import ...) (export ...) (begin ...))\n"
";; Bodies are evaluated in a fresh environment rooted at the global\n"
";; environment, so host-registered glue accessors are visible while\n"
";; kernel definitions stay private unless exported.\n"
"(define-macro (define-library libname . clauses)\n"
"  (let ((exports '()) (body '()) (imports '()))\n"
"    (for-each\n"
"      (lambda (clause)\n"
"        (if (pair? clause)\n"
"            (case (car clause)\n"
"              ((export) (set! exports (append exports (cdr clause))))\n"
"              ((begin)  (set! body (append body (cdr clause))))\n"
"              ((import) (set! imports (append imports (cdr clause))))\n"
"              (else #f))))\n"
"      clauses)\n"
"    `(let ((lib-env (sublet (rootlet))))\n"
"       (with-let lib-env ,@body)\n"
"       (let ((key (string->symbol ,(object->string libname))))\n"
"         (varlet *ccw-libraries* key\n"
"                 (apply inlet (map (lambda (n) (cons n (lib-env n))) ',exports)))\n"
"         (set! *ccw-last-library* key)\n"
"         key))))\n"
"\n"
"(define (ccw-library-ref key sym)\n"
"  (let ((lib (*ccw-libraries* key)))\n"
"    (and (let? lib) (lib sym))))\n"
"\n"
"(define (ccw-library-has? key sym)\n"
"  (let ((lib (*ccw-libraries* key)))\n"
"    (and (let? lib) (procedure? (lib sym)))))\n"
")\n";

/* Error trapping happens in Scheme: the executor calls these helpers and
 * reads (#t . result) or (#f . condition-text) back. That keeps Scheme
 * conditions intact while giving the host the text for error_message. */
static const char *const CCW_S7_GUARDS =
"(begin\n"
"(define (ccw-condition->string type info)\n"
"  (string-append (if (symbol? type) (symbol->string type) (object->string type))\n"
"                 \": \"\n"
"                 (if (and (pair? info) (string? (car info)))\n"
"                     (apply format #f info)\n"
"                     (object->string info))))\n"
"\n"
"(define (ccw-load-guarded path)\n"
"  (set! *ccw-last-library* #f)\n"
"  (catch #t\n"
"    (lambda () (load path) (cons #t *ccw-last-library*))\n"
"    (lambda (type info) (cons #f (ccw-condition->string type info)))))\n"
"\n"
"(define (ccw-call-guarded proc . args)\n"
"  (catch #t\n"
"    (lambda () (cons #t (apply proc args)))\n"
"    (lambda (type info) (cons #f (ccw-condition->string type info)))))\n"
")\n";

#endif /* CCW_S7_PRELUDE_H */
