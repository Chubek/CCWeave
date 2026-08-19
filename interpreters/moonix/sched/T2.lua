-- Moonix optimizing tier plan.  Execution remains a v0.2 feature, but the
-- plan is sealed and CI-checkable in v0.1 as required by MOONIX §8.
local S = sched.new "Moonix-T2"

local closure = S:require {
  capability = "lower.closure-conversion",
  prefer = "closure-convert"
}
local ssa = S:require { capability = "transform.ssa-construct" }
local alias = S:require { capability = "analysis.alias", prefer = "alias" }
local gvn = S:require { capability = "opt.gvn" }
local sccp = S:require { capability = "opt.sccp" }
local dce = S:require { capability = "opt.dead-code-elimination", prefer = "dce" }

-- SIMD tier is safe in the optimizing On1x pipeline: legality remains
-- conservative and rejects dynamic/non-unit-stride loops.
local vec_width      = S:require { capability = "analysis.vector-width" }
local loop_detect    = S:require { capability = "analysis.loops" }
local vec_legality   = S:require { capability = "analysis.vectorizable" }
local loop_vectorize = S:require { capability = "opt.loop-vectorize" }
local vec_reduce     = S:require { capability = "opt.vector-reduction" }
local vec_lower      = S:require { capability = "lower.vector-simde" }

local arith = S:rewrite "arith.*"
local bitwise = S:rewrite "bitwise.*"
local divmod = S:rewrite "divmod.*"
local cmp = S:rewrite "cmp.*"
local float = S:rewrite "float.*"

local ic = S:require { capability = "vm.inline-cache" }
local deopt_points = S:require { capability = "vm.deopt-points" }
local deopt_metadata = S:require { capability = "vm.deopt-metadata" }
local gc = S:require { capability = "vm.gc-barrier-insertion" }
local safepoint = S:require { capability = "vm.safepoint-insertion" }

S:edge(closure, ssa)
S:edge(ssa, alias)
S:edge(alias, gvn)
S:edge(gvn, sccp)
S:edge(sccp, dce)
S:edge(dce, vec_width)
S:edge(dce, loop_detect)
S:edge(loop_detect, vec_legality)
S:edge(vec_width, vec_legality)
S:edge(vec_legality, loop_vectorize)
S:edge(loop_vectorize, vec_reduce)
S:edge(vec_reduce, vec_lower)
S:edge(dce, arith)
S:edge(dce, bitwise)
S:edge(dce, divmod)
S:edge(dce, cmp)
S:edge(dce, float)
S:edge(arith, ic)
S:edge(bitwise, ic)
S:edge(divmod, ic)
S:edge(cmp, ic)
S:edge(float, ic)
S:edge(ic, deopt_points)
S:edge(deopt_points, deopt_metadata)
S:edge(deopt_metadata, gc)
S:edge(gc, safepoint)
S:edge(vec_lower, safepoint)

local on1x_complete = S:barrier "on1x-complete"

local tree_match = S:require { capability = "codegen.isel-tree-match" }
local legalize = S:require { capability = "codegen.isel-legalize" }
local schedule = S:require { capability = "codegen.sched-list" }
local regalloc = S:probe {
  capability = "codegen.regalloc-graph",
  prefer = "regalloc-graph-color"
} or S:require {
  capability = "codegen.regalloc-linear",
  prefer = "regalloc-linear-scan"
}
local codegen = S:require { capability = "codegen.x86-64" }

S:edge(tree_match, legalize)
S:edge(legalize, schedule)
S:edge(schedule, regalloc)
S:edge(regalloc, codegen)

return S:seal()
