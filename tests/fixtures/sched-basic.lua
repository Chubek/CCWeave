local S = sched.new "basic"
local escape = S:require { kernel = "escape-analysis" }
local gvn = S:require { kernel = "gvn" }
local arith = S:rewrite "arith.*"
S:edge(escape, gvn)
S:edge(gvn, arith)
return S:seal()
