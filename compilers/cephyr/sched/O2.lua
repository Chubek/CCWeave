-- Cephyr O2 pipeline — §8.3.
-- Aggressive optimization: full suite of analyses, optimizations,
-- Oeuph rewriting, and graph-coloring register allocation.

S = sched.new "Cephyr-O2"

-- Analyses.
local def_use  = S:require { capability = "analysis.def-use" }
local purity   = S:require { capability = "analysis.purity" }
local range    = S:require {
  capability = "analysis.range",
  prefer = "range-analysis"
}
local alias    = S:require { capability = "analysis.alias" }

-- Normalization.
local norm_cfg       = S:require { capability = "normalize.cfg" }
local norm_instr     = S:require { capability = "normalize.instructions" }
local ssa_construct  = S:require { capability = "transform.ssa-construct" }

-- Scalar optimizations.
local const_fold  = S:require { capability = "opt.constant-folding" }
local copy_prop  = S:require { capability = "opt.copy-propagation" }
local dce        = S:require { capability = "opt.dead-code-elimination" }
local strength   = S:require { capability = "opt.strength-reduction" }
local gvn        = S:require { capability = "opt.gvn" }
local sccp       = S:require { capability = "opt.sccp" }

-- Oeuph rewriting.
local arith_rules    = S:rewrite "arith.*"
local bitwise_rules  = S:rewrite "bitwise.*"
local cmp_rules      = S:rewrite "cmp.*"
local bool_rules     = S:rewrite "bool.*"
local cast_rules     = S:rewrite "cast.*"
local norm_rules     = S:rewrite "norm.*"
local mem_rules      = S:rewrite "mem.*"

-- The core → Tilly barrier (§8.4).
local pre_tilly = S:barrier "pre-tilly"

-- Code generation backend.
local isel    = S:require { capability = "codegen.x86-64" }
local ra = S:probe {
  capability = "codegen.regalloc-graph",
  prefer = "regalloc-graph-color"
} or S:require {
  capability = "codegen.regalloc-linear",
  prefer = "regalloc-linear-scan"
}
local sched_list = S:require { capability = "codegen.sched-list" }

-- Ordering.
S:edge(def_use, purity)
S:edge(purity, range)
S:edge(range, alias)
S:edge(alias, norm_cfg)
S:edge(norm_cfg, norm_instr)
S:edge(norm_instr, ssa_construct)

S:edge(ssa_construct, const_fold)
S:edge(const_fold, copy_prop)
S:edge(copy_prop, gvn)
S:edge(gvn, sccp)
S:edge(sccp, dce)
S:edge(dce, strength)

-- Oeuph rewrites run in parallel where unordered.
S:edge(strength, arith_rules)
S:edge(strength, bitwise_rules)
S:edge(strength, cmp_rules)
S:edge(strength, bool_rules)
S:edge(strength, cast_rules)
S:edge(strength, norm_rules)
S:edge(strength, mem_rules)

S:edge(arith_rules, pre_tilly)
S:edge(bitwise_rules, pre_tilly)
S:edge(cmp_rules, pre_tilly)
S:edge(bool_rules, pre_tilly)
S:edge(cast_rules, pre_tilly)
S:edge(norm_rules, pre_tilly)
S:edge(mem_rules, pre_tilly)

S:edge(pre_tilly, isel)
S:edge(isel, ra)
S:edge(ra, sched_list)

return S:seal()
