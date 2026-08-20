;;; rewrite-salvo/arith/identity/rules.scm
;;; Additive and multiplicative identities (x+0, x*1, x-0, 0-x, x-x).
;;; Every rule preserves semantics; extraction chooses the cheaper form (§7.2).

(ruleset arith.identity)

;; --- Additive identities ---

(rule add-zero
      (iadd ?x (iconst 0)) ?x
      :bidirectional #f)

(rule zero-add
      (iadd (iconst 0) ?x) ?x
      :bidirectional #f)

(rule sub-zero
      (isub ?x (iconst 0)) ?x
      :bidirectional #f)

(rule sub-self
      (isub ?x ?x) (iconst 0)
      :bidirectional #f)

(rule zero-sub-x
      (isub (iconst 0) ?x) (ineg ?x)
      :bidirectional #f)

;; --- Multiplicative identities ---

(rule mul-one
      (imul ?x (iconst 1)) ?x
      :bidirectional #f)

(rule one-mul
      (imul (iconst 1) ?x) ?x
      :bidirectional #f)

(rule mul-zero
      (imul ?x (iconst 0)) (iconst 0)
      :bidirectional #f)

(rule zero-mul
      (imul (iconst 0) ?x) (iconst 0)
      :bidirectional #f)

(rule mul-neg-one
      (imul ?x (iconst -1)) (ineg ?x)
      :bidirectional #f)

(rule neg-one-mul
      (imul (iconst -1) ?x) (ineg ?x)
      :bidirectional #f)

;; --- Division identities ---

(rule div-one
      (idiv ?x (iconst 1)) ?x
      :bidirectional #f)

(rule rem-one
      (irem ?x (iconst 1)) (iconst 0)
      :bidirectional #f)

;; --- Commutative rules ---

(rule add-commutes
      (iadd ?x ?y) (iadd ?y ?x)
      :bidirectional #t)

(rule mul-commutes
      (imul ?x ?y) (imul ?y ?x)
      :bidirectional #t)

;; --- Absorbing elements ---

(rule add-neg-self
      (iadd ?x (ineg ?x)) (iconst 0)
      :bidirectional #f)

(rule neg-self-add
      (iadd (ineg ?x) ?x) (iconst 0)
      :bidirectional #f)

(rule sub-neg-self
      (isub ?x (ineg ?x)) (imul ?x (iconst 2))
      :bidirectional #f)
