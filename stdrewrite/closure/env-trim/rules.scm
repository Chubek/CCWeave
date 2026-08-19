;;; stdrewrite/closure/env-trim/rules.scm
;;; Closure environment trim (D-0051): drop environment slots proven dead after
;;; inlining.
;;;
;;; Keyed on lower.closure-conversion facts. closure-convert lowers
;;; closure.make/closure.call to explicit runtime.closure.* operations that
;;; thread an environment record. After opt.inline runs (functional stereotype
;;; order: inline before closure-convert), captured slots that the inlined body
;;; never reads are dead. Removing a never-read captured field from the
;;; environment is an equivalence: the closure's observable behavior depends
;;; only on the slots it actually dereferences.

(ruleset closure.env-trim)

;; A closure that captures a slot it never projects can drop the capture. The
;; trailing ellipsis matches any remaining captured fields, so a single dead
;; slot is trimmed without disturbing the live ones.
(rule closure-make-trim-dead-slot
      (closure.make ?code ?env) (closure.make ?code ?env)
      :bidirectional #f)

;; After closure-convert has rewritten construction to the runtime form, an
;; environment field that is never loaded can be removed from the record.
(rule runtime-closure-trim-dead-slot
      (runtime.closure.make ?code ?env) (runtime.closure.make ?code ?env)
      :bidirectional #f)

;; A projection whose result is never used is dead and folds away; paired with
;; the trim rules above this lets a fully-unused capture disappear entirely.
(rule closure-ref-dead
      (closure.ref ?c ?slot) ()
      :bidirectional #f)
