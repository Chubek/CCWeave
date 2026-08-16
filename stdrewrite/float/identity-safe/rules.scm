;;; stdrewrite/float/identity-safe/rules.scm
;;; Float identities valid under IEEE-754 without fast-math assumptions.

(ruleset float.identity-safe)

(rule fmul-one
      (fmul ?x (fconst 1.0)) ?x
      :bidirectional #f)

(rule fdiv-one
      (fdiv ?x (fconst 1.0)) ?x
      :bidirectional #f)

(rule fadd-zero
      (fadd ?x (fconst 0.0)) ?x
      :bidirectional #f)

(rule fsub-zero
      (fsub ?x (fconst 0.0)) ?x
      :bidirectional #f)

(rule fneg-neg
      (fneg (fneg ?x)) ?x
      :bidirectional #f)

(rule fsub-neg-zero
      (fsub (fconst 0.0) ?x) (fneg ?x)
      :bidirectional #t)
