;;; sema-salvo/cfg/misc/rules.scm
;;; Semantic rules for sema.cfg.misc.

(sema-ruleset sema.cfg.misc)

(sema-rule cold-path
  :description "Path ending in throw or abort is cold"
  :trigger (block $b)
  :target (assert (cold $b))
  :gating (fact (ends-in-abnormal $b)))

(sema-rule exception-edge
  :description "Call inside a try region has an exceptional successor"
  :trigger (call $f $args)
  :target (assert (exc-edge (call $f $args) (handler-of (current-region))))
  :gating (fact (in-try-region (call $f $args))))

