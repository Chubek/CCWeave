;;; stdrewrite/power/rules.scm
;;; Low-toggle and low-work integer rewrites.  These are unconditional
;;; equivalences: the extraction cost model decides whether the cheaper
;;; form is preferable for a target (§7.2).

(ruleset power.consumption)

;; Idempotent bitwise operations avoid a second data-dependent operation.

(rule xor-self-zero
      (ixor ?x ?x) (iconst 0)
      :bidirectional #f)

(rule and-self
      (iand ?x ?x) ?x
      :bidirectional #f)

(rule or-self
      (ior ?x ?x) ?x
      :bidirectional #f)

;; Constant bitwise identities reduce switching activity in the datapath.

(rule xor-zero
      (ixor ?x (iconst 0)) ?x
      :bidirectional #f)

(rule and-zero
      (iand ?x (iconst 0)) (iconst 0)
      :bidirectional #f)

(rule or-zero
      (ior ?x (iconst 0)) ?x
      :bidirectional #f)

;; Zero-distance shifts should not consume a shift unit.

(rule shl-zero
      (shl ?x (iconst 0)) ?x
      :bidirectional #f)

(rule lshr-zero
      (lshr ?x (iconst 0)) ?x
      :bidirectional #f)

(rule ashr-zero
      (ashr ?x (iconst 0)) ?x
      :bidirectional #f)

;; Replace small constant multiplies with the cheaper shift datapath.

(rule mul-two-shl
      (imul ?x (iconst 2)) (shl ?x (iconst 1))
      :bidirectional #f)

(rule mul-four-shl
      (imul ?x (iconst 4)) (shl ?x (iconst 2))
      :bidirectional #f)

(rule mul-eight-shl
      (imul ?x (iconst 8)) (shl ?x (iconst 3))
      :bidirectional #f)

;; Doubling is a shift, avoiding a general-purpose add in low-power paths.

(rule add-self-shl
      (iadd ?x ?x) (shl ?x (iconst 1))
      :bidirectional #f)

(rule sub-self-zero
      (isub ?x ?x) (iconst 0)
      :bidirectional #f)
