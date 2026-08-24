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

-- Polyhedral loop tier (ISLKERN §3.2, §5).  These are deliberately kept in
-- O2: affine extraction and dependence-aware scheduling are compile-time
-- analyses whose cost is not appropriate for the fast O0/O1 pipelines.
local affine_extract = S:require { capability = "analysis.affine" }
local dep_poly       = S:require { capability = "analysis.dependence" }
local isl_schedule   = S:require { capability = "opt.schedule" }
local tile_plan      = S:require { capability = "opt.tiling" }

-- SIMD loop tier (SIMDKERN): width facts and conservative legality feed the
-- vector plan; lowering remains before the core → Tilly barrier.
local vec_width       = S:require { capability = "analysis.vector-width" }
local loop_detect     = S:require { capability = "analysis.loops" }
local vec_legality    = S:require { capability = "analysis.vectorizable" }
local loop_vectorize  = S:require { capability = "opt.loop-vectorize" }
local vec_reduce      = S:require { capability = "opt.vector-reduction" }
local vec_lower       = S:require { capability = "lower.vector-simde" }

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
S:edge(strength, vec_width)
S:edge(strength, loop_detect)
S:edge(loop_detect, vec_legality)
S:edge(vec_width, vec_legality)
S:edge(vec_legality, loop_vectorize)
S:edge(loop_vectorize, vec_reduce)
S:edge(vec_reduce, vec_lower)

-- ISL's ordering is a strict producer/consumer chain.  Keeping it before
-- pre-tilly ensures schedule-derived facts describe core IR and that any
-- later Tilly lowering observes the same provenance boundary.
S:edge(ssa_construct, affine_extract)
S:edge(affine_extract, dep_poly)
S:edge(dep_poly, isl_schedule)
S:edge(isl_schedule, tile_plan)

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
S:edge(tile_plan, pre_tilly)
S:edge(vec_lower, pre_tilly)

S:edge(pre_tilly, isel)
S:edge(isel, ra)
S:edge(ra, sched_list)

return S:seal()
