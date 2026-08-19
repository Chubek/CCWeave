-- Parthia AOT optimizing pipeline.
local S = sched.new "Parthia-O1"

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
local ssa = S:require { capability = "transform.ssa-construct" }
local fold = S:require { capability = "opt.constant-folding",
                         prefer = "const-fold" }
local copy = S:require { capability = "opt.copy-propagation",
                         prefer = "copy-prop" }
local dce = S:require { capability = "opt.dead-code-elimination",
                        prefer = "dce" }
local gc = S:require { capability = "vm.gc-barrier-insertion",
                       prefer = "gc-barrier-insert" }
local safepoint = S:require { capability = "vm.safepoint-insertion",
                              prefer = "safepoint-insert" }

local arith = S:rewrite "arith.*"
local cmp = S:rewrite "cmp.*"
local bool = S:rewrite "bool.*"

local pre_codegen = S:barrier "parthia-runtime-lowered"
local tree_match = S:require { capability = "codegen.isel-tree-match" }
local legalize = S:require { capability = "codegen.isel-legalize" }
local schedule = S:require { capability = "codegen.sched-list" }
local regalloc = S:require { capability = "codegen.regalloc-linear",
                             prefer = "regalloc-linear-scan" }
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
S:edge(purity, ssa)
S:edge(ssa, fold)
S:edge(fold, copy)
S:edge(copy, dce)
S:edge(dce, arith)
S:edge(dce, cmp)
S:edge(dce, bool)
S:edge(arith, gc)
S:edge(cmp, gc)
S:edge(bool, gc)
S:edge(gc, safepoint)
S:edge(safepoint, pre_codegen)
S:edge(pre_codegen, tree_match)
S:edge(tree_match, legalize)
S:edge(legalize, schedule)
S:edge(schedule, regalloc)
S:edge(regalloc, codegen)

return S:seal()
