;;; stdrewrite/peephole/window/rules.scm
;;; Adjacent-instruction pair rewrites.

(ruleset peephole.window)

(rule add-then-sub-same
      (isub (iadd ?x (iconst ?k)) (iconst ?k)) ?x
      :bidirectional #f)

(rule sub-then-add-same
      (iadd (isub ?x (iconst ?k)) (iconst ?k)) ?x
      :bidirectional #f)

(rule move-chain
      (imov (imov ?x)) (imov ?x)
      :bidirectional #f)

(rule move-elim
      (imov ?x) ?x
      :bidirectional #f)

(rule neg-then-neg
      (ineg (ineg ?x)) ?x
      :bidirectional #f)

(rule not-then-not
      (not (not ?x)) ?x
      :bidirectional #f)

(rule add-zero-elim
      (iadd ?x (iconst 0)) ?x
      :bidirectional #f)

(rule mul-one-elim
      (imul ?x (iconst 1)) ?x
      :bidirectional #f)

(rule mul-zero-elim
      (imul ?x (iconst 0)) (iconst 0)
      :bidirectional #f)

(rule and-self-elim
      (and ?x ?x) ?x
      :bidirectional #f)

(rule or-self-elim
      (or ?x ?x) ?x
      :bidirectional #f)

(rule xor-self-elim
      (xor ?x ?x) (iconst 0)
      :bidirectional #f)

(rule sub-self-elim
      (isub ?x ?x) (iconst 0)
      :bidirectional #f)

(rule shl-by-zero-elim
      (shl ?x (iconst 0)) ?x
      :bidirectional #f)
