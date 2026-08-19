-- Parthia full AOT pipeline (§SML-PARTHIA §3–§8).
-- Functional lowering is complete before runtime instrumentation; no
-- deoptimization or inline-cache capabilities are used.
local S = sched.new "Parthia-O2"

local pattern = S:require { capability = "lower.pattern-match",
                            prefer = "pattern-match-lower" }
local pipeline = S:require { capability = "lower.functional-pipeline",
                             prefer = "functional-pipeline-lower" }
local inline = S:require { capability = "opt.inline", prefer = "inline" }
local closure = S:require { capability = "lower.closure-conversion",
                            prefer = "closure-convert" }
local lift = S:require { capability = "lower.lambda-lifting",
                         prefer = "lambda-lift" }
local tail = S:require { capability = "opt.tailcall",
                         prefer = "tail-call" }
local exceptions = S:require { capability = "lower.exceptions",
                               prefer = "exception-lower" }

local defuse = S:require { capability = "analysis.def-use",
                           prefer = "def-use" }
local purity = S:require { capability = "analysis.purity",
                           prefer = "purity" }
local alias = S:require { capability = "analysis.alias", prefer = "alias" }
local loops = S:require { capability = "analysis.loops", prefer = "loop-detect" }
local vec_width = S:require { capability = "analysis.vector-width",
                              prefer = "vec-width" }
local vec_legal = S:require { capability = "analysis.vectorizable",
                              prefer = "vec-legality" }

local ssa = S:require { capability = "transform.ssa-construct" }
local fold = S:require { capability = "opt.constant-folding",
                         prefer = "const-fold" }
local copy = S:require { capability = "opt.copy-propagation",
                         prefer = "copy-prop" }
local gvn = S:require { capability = "opt.gvn", prefer = "gvn" }
local sccp = S:require { capability = "opt.sccp", prefer = "sccp" }
local dce = S:require { capability = "opt.dead-code-elimination",
                        prefer = "dce" }
local loop_vec = S:require { capability = "opt.loop-vectorize",
                             prefer = "loop-vectorize" }
local vec_reduce = S:require { capability = "opt.vector-reduction",
                               prefer = "vec-reduce" }
local vec_lower = S:require { capability = "lower.vector-simde",
                              prefer = "vec-lower-simde" }
local gc = S:require { capability = "vm.gc-barrier-insertion",
                       prefer = "gc-barrier-insert" }
local safepoint = S:require { capability = "vm.safepoint-insertion",
                              prefer = "safepoint-insert" }

local arith = S:rewrite "arith.*"
local bitwise = S:rewrite "bitwise.*"
local cmp = S:rewrite "cmp.*"
local bool = S:rewrite "bool.*"
local casts = S:rewrite "cast.*"
local norm = S:rewrite "norm.*"
local memory = S:rewrite "mem.*"

local pre_codegen = S:barrier "parthia-runtime-lowered"
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
local codegen = S:require { capability = "codegen.x86-64",
                            prefer = "codegen-x86-64" }

S:edge(pattern, pipeline)
S:edge(pipeline, inline)
S:edge(inline, closure)
S:edge(closure, lift)
S:edge(lift, tail)
S:edge(tail, exceptions)
S:edge(exceptions, defuse)
S:edge(defuse, purity)
S:edge(purity, alias)
S:edge(alias, ssa)
S:edge(ssa, fold)
S:edge(fold, copy)
S:edge(copy, gvn)
S:edge(gvn, sccp)
S:edge(sccp, dce)

S:edge(ssa, loops)
S:edge(ssa, vec_width)
S:edge(loops, vec_legal)
S:edge(vec_width, vec_legal)
S:edge(vec_legal, loop_vec)
S:edge(loop_vec, vec_reduce)
S:edge(vec_reduce, vec_lower)

S:edge(dce, arith)
S:edge(dce, bitwise)
S:edge(dce, cmp)
S:edge(dce, bool)
S:edge(dce, casts)
S:edge(dce, norm)
S:edge(dce, memory)
S:edge(vec_lower, gc)
S:edge(arith, gc)
S:edge(bitwise, gc)
S:edge(cmp, gc)
S:edge(bool, gc)
S:edge(casts, gc)
S:edge(norm, gc)
S:edge(memory, gc)
S:edge(gc, safepoint)
S:edge(safepoint, pre_codegen)
S:edge(pre_codegen, tree_match)
S:edge(tree_match, legalize)
S:edge(legalize, schedule)
S:edge(schedule, regalloc)
S:edge(regalloc, codegen)

return S:seal()
