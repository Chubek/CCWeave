-- Cephyr O1 pipeline — §8.3.
-- Moderate optimization: key scalar optimizations plus linear-scan RA.

S = sched.new "Cephyr-O1"

-- Analyses.
local def_use  = S:require { capability = "analysis.def-use" }
local purity   = S:require { capability = "analysis.purity" }

-- Normalization.
local norm_cfg = S:require { capability = "normalize.cfg" }
local ssa_construct = S:require { capability = "transform.ssa-construct" }

-- Scalar optimizations.
local const_fold  = S:require { capability = "opt.constant-folding" }
local copy_prop  = S:require { capability = "opt.copy-propagation" }
local dce        = S:require { capability = "opt.dead-code-elimination" }
local strength   = S:require { capability = "opt.strength-reduction" }

-- Oeuph rewriting.
local arith_rules = S:rewrite "arith.*"

-- Code generation backend.
local isel    = S:require { capability = "codegen.x86-64" }
local ra = S:probe   { capability = "codegen.regalloc-graph" }
        or S:require { capability = "codegen.regalloc-linear" }
local sched_list = S:require { capability = "codegen.sched-list" }

-- The core → Tilly barrier (§8.4).
local pre_tilly = S:barrier "pre-tilly"

-- Ordering: analyses → normalization → optimizations → Tilly barrier → codegen.
S:edge(def_use, purity)
S:edge(purity, norm_cfg)
S:edge(norm_cfg, ssa_construct)
S:edge(ssa_construct, const_fold)
S:edge(const_fold, copy_prop)
S:edge(copy_prop, dce)
S:edge(dce, strength)
S:edge(strength, arith_rules)
S:edge(arith_rules, pre_tilly)
S:edge(pre_tilly, isel)
S:edge(isel, ra)
S:edge(ra, sched_list)

return S:seal()
