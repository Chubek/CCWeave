-- Cephyr O0 pipeline — §8.3.
-- Fast compilation, no optimization, minimal passes.
-- Uses linear-scan register allocation; no Oeuph rewriting.

S = sched.new "Cephyr-O0"

-- Code generation backend (mandatory).
local isel    = S:require { capability = "codegen.x86-64" }
local regalloc = S:require { capability = "codegen.regalloc-linear" }
local sched_list = S:require { capability = "codegen.sched-list" }

-- Minimal normalization: SSA construction is required for correctness.
local ssa_construct = S:require { capability = "transform.ssa-construct" }

-- The core → Tilly barrier (§8.4).
local pre_tilly = S:barrier "pre-tilly"

-- Order: SSA construct first, then codegen passes.
S:edge(ssa_construct, pre_tilly)
S:edge(pre_tilly, isel)
S:edge(isel, regalloc)
S:edge(regalloc, sched_list)

return S:seal()
