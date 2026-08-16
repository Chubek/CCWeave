;;; stdrewrite/divmod/identity/rules.scm
;;; Division and remainder identities (x/1, x%1, x/x);
;;; guarded by nonzero-divisor conditions.

(ruleset divmod.identity)

(rule sdiv-by-one
      (sdiv ?x (iconst 1)) ?x
      :bidirectional #f)

(rule srem-by-one
      (srem ?x (iconst 1)) (iconst 0)
      :bidirectional #f)

(rule udiv-by-one
      (udiv ?x (iconst 1)) ?x
      :bidirectional #f)

(rule urem-by-one
      (urem ?x (iconst 1)) (iconst 0)
      :bidirectional #f)

(rule sdiv-by-self
      (sdiv ?x ?x) (iconst 1)
      :bidirectional #f)

(rule srem-by-self
      (srem ?x ?x) (iconst 0)
      :bidirectional #f)
