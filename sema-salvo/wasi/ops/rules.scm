;;; sema-salvo/wasi/ops/rules.scm
;;; Semantic rules for sema.wasi.ops.

(sema-ruleset sema.wasi.ops)

(sema-rule op-known
  :description "wasi-op name must exist in the frozen preview1 op table"
  :trigger (wasi-op $op $args)
  :target (require (abi-preview1-op $op))
  :gating #t)

(sema-rule fd-rights
  :description "fd operation records the rights it requires"
  :trigger (wasi-op $op $args)
  :target (assert (requires-rights $op (rights-of $op)))
  :gating (fact (fd-op $op)))

(sema-rule errno-domain
  :description "wasi-op result is an errno in the preview1 domain"
  :trigger (wasi-op $op $args)
  :target (assert (range (wasi-op $op $args) 0 76))
  :gating #t)

(sema-rule clock-monotonic
  :description "Monotonic clock reads never decrease across a single run"
  :trigger (wasi-op clock_time_get (clock monotonic))
  :target (assert (monotone (wasi-op clock_time_get (clock monotonic))))
  :gating #t)

(sema-rule random-entropy
  :description "random_get results are opaque and never foldable"
  :trigger (wasi-op random_get $args)
  :target (assert (not-const (wasi-op random_get $args)))
  :gating #t)

