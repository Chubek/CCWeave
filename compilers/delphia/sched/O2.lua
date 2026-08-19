local S = sched.new "Delphia-O2"
local devirt = S:require { capability = "opt.oop-devirtualization" }
local exceptions = S:require { capability = "lower.exceptions" }
local vtable = S:require { capability = "lower.oop-vtable" }
local nullcheck = S:require { capability = "opt.oop-null-check-elimination" }
local rtti = S:require { capability = "lower.oop-rtti" }
local iface = S:require { capability = "lower.oop-interface" }
local vec_width      = S:require { capability = "analysis.vector-width" }
local loop_detect    = S:require { capability = "analysis.loops" }
local vec_legality   = S:require { capability = "analysis.vectorizable" }
local loop_vectorize = S:require { capability = "opt.loop-vectorize" }
local vec_reduce     = S:require { capability = "opt.vector-reduction" }
local vec_lower      = S:require { capability = "lower.vector-simde" }
local codegen = S:require { capability = "codegen.x86-64" }
S:edge(devirt, vec_width)
S:edge(devirt, loop_detect)
S:edge(loop_detect, vec_legality)
S:edge(vec_width, vec_legality)
S:edge(vec_legality, loop_vectorize)
S:edge(loop_vectorize, vec_reduce)
S:edge(vec_reduce, exceptions)
S:edge(exceptions, vtable)
S:edge(vtable, nullcheck)
S:edge(nullcheck, rtti)
S:edge(rtti, iface)
S:edge(iface, vec_lower)
S:edge(vec_lower, codegen)
return S:seal()
