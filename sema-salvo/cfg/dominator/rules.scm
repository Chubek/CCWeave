;;; sema-salvo/cfg/dominator/rules.scm
;;; Semantic rules for sema.cfg.dominator.

(sema-ruleset sema.cfg.dominator)

(sema-rule dominates-use
  :description "Every use must be dominated by its definition"
  :trigger (use-of $def $use)
  :target (require (dominates $def $use))
  :gating #t)

