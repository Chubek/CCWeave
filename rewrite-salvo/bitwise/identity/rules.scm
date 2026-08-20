;;; rewrite-salvo/bitwise/identity/rules.scm
;;; And/or/xor identities with 0, all-ones, and self.

(ruleset bitwise.identity)

(rule and-zero
      (and ?x (iconst 0)) (iconst 0)
      :bidirectional #f)

(rule zero-and
      (and (iconst 0) ?x) (iconst 0)
      :bidirectional #f)

(rule and-all-ones
      (and ?x (iconst -1)) ?x
      :bidirectional #f)

(rule all-ones-and
      (and (iconst -1) ?x) ?x
      :bidirectional #f)

(rule and-self
      (and ?x ?x) ?x
      :bidirectional #f)

(rule or-zero
      (or ?x (iconst 0)) ?x
      :bidirectional #f)

(rule zero-or
      (or (iconst 0) ?x) ?x
      :bidirectional #f)

(rule or-all-ones
      (or ?x (iconst -1)) (iconst -1)
      :bidirectional #f)

(rule all-ones-or
      (or (iconst -1) ?x) (iconst -1)
      :bidirectional #f)

(rule or-self
      (or ?x ?x) ?x
      :bidirectional #f)

(rule xor-zero
      (xor ?x (iconst 0)) ?x
      :bidirectional #f)

(rule zero-xor
      (xor (iconst 0) ?x) ?x
      :bidirectional #f)

(rule xor-self
      (xor ?x ?x) (iconst 0)
      :bidirectional #f)

(rule xor-all-ones
      (xor ?x (iconst -1)) (not ?x)
      :bidirectional #f)

(rule all-ones-xor
      (xor (iconst -1) ?x) (not ?x)
      :bidirectional #f)

(rule not-not
      (not (not ?x)) ?x
      :bidirectional #f)
