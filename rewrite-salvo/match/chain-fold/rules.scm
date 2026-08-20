;;; rewrite-salvo/match/chain-fold/rules.scm
;;; Match-chain fold (D-0051): merge adjacent decision-tree arms with identical
;;; targets.
;;;
;;; Keyed on lower.pattern-match facts. pattern-match-lower rewrites scalar
;;; match-eq/match-ne nodes to icmp.eq/icmp.ne; the elaborator emits
;;; exhaustiveness/redundancy diagnostics before lowering, so the decision tree
;;; reaching this ruleset is already verified. Two adjacent comparisons of the
;;; same scrutinee that branch to the same target are redundant; folding them
;;; is an equivalence over the lowered comparison tree.

(ruleset match.chain-fold)

;; Two equal-arm comparisons of the same scrutinee against the same constant
;; collapse to one (the second arm is unreachable once the first matched).
(rule match-same-arm-fold
      (match-eq ?x (iconst ?k) (match-eq ?x (iconst ?k) ?t)) (match-eq ?x (iconst ?k) ?t)
      :bidirectional #f)

;; Complementary arms on the same scrutinee and constant (eq then ne, or the
;; lowered icmp forms) are exhaustive: the second arm's test always decides,
;; so the pair folds to a single unconditional selection of the shared target.
(rule match-complement-arm-fold
      (match-eq ?x (iconst ?k) (match-ne ?x (iconst ?k) ?t)) ?t
      :bidirectional #f)

;; The same folds hold after pattern-match-lower has rewritten the match nodes
;; to explicit integer comparisons.
(rule icmp-same-arm-fold
      (icmp-eq ?x (iconst ?k) (icmp-eq ?x (iconst ?k) ?t)) (icmp-eq ?x (iconst ?k) ?t)
      :bidirectional #f)

(rule icmp-complement-arm-fold
      (icmp-eq ?x (iconst ?k) (icmp-ne ?x (iconst ?k) ?t)) ?t
      :bidirectional #f)
