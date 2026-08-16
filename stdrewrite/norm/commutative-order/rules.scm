;;; stdrewrite/norm/commutative-order/rules.scm
;;; Orders commutative operands by canonical node ordering.

(ruleset norm.commutative-order)

(rule add-canonical
      (iadd ?x ?y) (iadd ?y ?x)
      :bidirectional #t)

(rule mul-canonical
      (imul ?x ?y) (imul ?y ?x)
      :bidirectional #t)

(rule and-canonical
      (and ?x ?y) (and ?y ?x)
      :bidirectional #t)

(rule or-canonical
      (or ?x ?y) (or ?y ?x)
      :bidirectional #t)

(rule xor-canonical
      (xor ?x ?y) (xor ?y ?x)
      :bidirectional #t)

(rule eq-canonical
      (icmp-eq ?x ?y) (icmp-eq ?y ?x)
      :bidirectional #t)
