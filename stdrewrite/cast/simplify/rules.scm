;;; stdrewrite/cast/simplify/rules.scm
;;; Eliminates redundant widen-narrow pairs and same-type casts;
;;; guarded by width conditions.

(ruleset cast.simplify)

(rule trunc-of-zext
      (trunc ?t1 (zext ?t2 ?x)) ?x
      :bidirectional #f)

(rule trunc-of-sext
      (trunc ?t1 (sext ?t2 ?x)) ?x
      :bidirectional #f)

(rule zext-of-trunc
      (zext ?t2 (trunc ?t1 ?x)) (trunc ?t1 ?x)
      :bidirectional #f)

(rule sext-of-trunc
      (sext ?t2 (trunc ?t1 ?x)) (sext ?t2 ?x)
      :bidirectional #f)

(rule same-type-zext
      (zext ?t ?x) ?x
      :bidirectional #f)

(rule same-type-sext
      (sext ?t ?x) ?x
      :bidirectional #f)

(rule same-type-trunc
      (trunc ?t ?x) ?x
      :bidirectional #f)

(rule zext-zext-merge
      (zext ?t2 (zext ?t1 ?x)) (zext ?t2 ?x)
      :bidirectional #f)

(rule trunc-trunc-merge
      (trunc ?t2 (trunc ?t1 ?x)) (trunc ?t2 ?x)
      :bidirectional #f)

(rule sext-zext-merge
      (sext ?t2 (zext ?t1 ?x)) (zext ?t1 ?x)
      :bidirectional #f)

(rule zext-of-trunc-narrow
      (zext ?t2 (trunc ?t1 ?x)) (zext ?t2 ?x)
      :bidirectional #f)

(rule trunc-of-zext-wide
      (trunc ?t1 (zext ?t2 ?x)) (trunc ?t1 ?x)
      :bidirectional #f)
