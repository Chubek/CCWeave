;;; sema-salvo/life/ownership/rules.scm
;;; Semantic rules for sema.life.ownership.

(sema-ruleset sema.life.ownership)

(sema-rule owner-move
  :description "Move transfers ownership and kills the source binding"
  :trigger (move $dst $src)
  :target (assert (moved-out $src))
  :gating #t)

(sema-rule drop-point
  :description "Owned value's live-range end is its drop point"
  :trigger (def $name $val)
  :target (assert (drop-at $name (live-to-point $name)))
  :gating (fact (owned $name)))

