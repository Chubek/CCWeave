-- Moonix baseline tier — MOONIX §6.
local S = sched.new "Moonix-T1"

local closure = S:require {
  capability = "lower.closure-conversion",
  prefer = "closure-convert"
}
local ssa = S:require { capability = "transform.ssa-construct" }
local ic = S:require { capability = "vm.inline-cache" }
local gc = S:require { capability = "vm.gc-barrier-insertion" }
local safepoint = S:require { capability = "vm.safepoint-insertion" }

S:edge(closure, ssa)
S:edge(ssa, ic)
S:edge(ic, gc)
S:edge(gc, safepoint)

local on1x_complete = S:barrier "on1x-complete"

local tree_match = S:require { capability = "codegen.isel-tree-match" }
local legalize = S:require { capability = "codegen.isel-legalize" }
local schedule = S:require { capability = "codegen.sched-list" }
local regalloc = S:require {
  capability = "codegen.regalloc-linear",
  prefer = "regalloc-linear-scan"
}
local codegen = S:require { capability = "codegen.x86-64" }

S:edge(on1x_complete, tree_match)
S:edge(tree_match, legalize)
S:edge(legalize, schedule)
S:edge(schedule, regalloc)
S:edge(regalloc, codegen)

return S:seal()
