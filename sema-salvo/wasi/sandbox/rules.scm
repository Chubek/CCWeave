;;; sema-salvo/wasi/sandbox/rules.scm
;;; Semantic rules for sema.wasi.sandbox.

(sema-ruleset sema.wasi.sandbox)

(sema-rule path-sandbox
  :description "Statically absolute path outside the sandbox is a definite error"
  :trigger (wasi-op path_open $args)
  :target (error (sandbox-escape $args))
  :gating (fact (static-absolute-path $args)))

