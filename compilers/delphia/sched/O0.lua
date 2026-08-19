-- Delphia O0 plan.  The driver records this deterministic composition in
-- .note.ccw; execution is delegated to the host scheduler when available.
local S = sched.new "Delphia-O0"
local exceptions = S:require { capability = "lower.exceptions" }
local vtable = S:require { capability = "lower.oop-vtable" }
local rtti = S:require { capability = "lower.oop-rtti" }
local iface = S:require { capability = "lower.oop-interface" }
local codegen = S:require { capability = "codegen.x86-64" }
S:edge(exceptions, vtable)
S:edge(vtable, rtti)
S:edge(rtti, iface)
S:edge(iface, codegen)
return S:seal()
