;;; stdrewrite/cast/constant-fold/rules.scm
;;; Folds casts of literal operands.

(ruleset cast.constant-fold)

(rule zext-const
      (zext ?t (iconst ?a)) (iconst ?a)
      :bidirectional #f)

(rule sext-const-positive
      (sext ?t (iconst ?a)) (iconst ?a)
      :bidirectional #f)

(rule trunc-const
      (trunc ?t (iconst ?a)) (iconst ?a)
      :bidirectional #f)

(rule sitofp-const
      (sitofp ?t (iconst ?a)) (fconst (exact->inexact ?a))
      :bidirectional #f)

(rule fptosi-const
      (fptosi ?t (fconst ?a)) (iconst (inexact->exact (truncate ?a)))
      :bidirectional #f)

(rule fpext-const
      (fpext ?t (fconst ?a)) (fconst ?a)
      :bidirectional #f)

(rule fptrunc-const
      (fptrunc ?t (fconst ?a)) (fconst ?a)
      :bidirectional #f)

(rule bitcast-const
      (bitcast ?t (iconst ?a)) (iconst ?a)
      :bidirectional #f)
