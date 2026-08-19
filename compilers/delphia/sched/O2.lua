local S = sched.new "Delphia-O2"
local devirt = S:require { capability = "opt.oop-devirtualization" }
local exceptions = S:require { capability = "lower.exceptions" }
local vtable = S:require { capability = "lower.oop-vtable" }
local nullcheck = S:require { capability = "opt.oop-null-check-elimination" }
local rtti = S:require { capability = "lower.oop-rtti" }
local iface = S:require { capability = "lower.oop-interface" }
local codegen = S:require { capability = "codegen.x86-64" }
S:edge(devirt, exceptions)
S:edge(exceptions, vtable)
S:edge(vtable, nullcheck)
S:edge(nullcheck, rtti)
S:edge(rtti, iface)
S:edge(iface, codegen)
return S:seal()
