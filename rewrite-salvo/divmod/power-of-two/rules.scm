;;; rewrite-salvo/divmod/power-of-two/rules.scm
;;; Unsigned div/mod by powers of two into shift/mask;
;;; guarded by power-of-two side conditions.

(ruleset divmod.power-of-two)

(rule udiv-2
      (udiv ?x (iconst 2)) (shr ?x (iconst 1))
      :bidirectional #t)

(rule udiv-4
      (udiv ?x (iconst 4)) (shr ?x (iconst 2))
      :bidirectional #t)

(rule udiv-8
      (udiv ?x (iconst 8)) (shr ?x (iconst 3))
      :bidirectional #t)

(rule udiv-16
      (udiv ?x (iconst 16)) (shr ?x (iconst 4))
      :bidirectional #t)

(rule urem-2
      (urem ?x (iconst 2)) (and ?x (iconst 1))
      :bidirectional #t)

(rule urem-4
      (urem ?x (iconst 4)) (and ?x (iconst 3))
      :bidirectional #t)

(rule urem-8
      (urem ?x (iconst 8)) (and ?x (iconst 7))
      :bidirectional #t)

(rule urem-16
      (urem ?x (iconst 16)) (and ?x (iconst 15))
      :bidirectional #t)
