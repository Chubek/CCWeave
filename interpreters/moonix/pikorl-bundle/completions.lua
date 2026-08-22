-- Moonix (Lua) auto-completion for PikoRL REPL

local keywords = {
  "and", "break", "do", "else", "elseif", "end", "false",
  "for", "function", "goto", "if", "in", "local", "nil",
  "not", "or", "repeat", "return", "then", "true", "until", "while",

  -- Lua standard library
  "assert", "collectgarbage", "dofile", "error", "getmetatable",
  "ipairs", "load", "loadfile", "next", "pairs", "pcall",
  "print", "rawequal", "rawget", "rawlen", "rawset", "require",
  "select", "setmetatable", "tonumber", "tostring", "type",
  "xpcall", "_G", "_VERSION",

  -- Math
  "math", "math.abs", "math.acos", "math.asin", "math.atan",
  "math.ceil", "math.cos", "math.deg", "math.exp", "math.floor",
  "math.fmod", "math.huge", "math.log", "math.max", "math.min",
  "math.modf", "math.pi", "math.rad", "math.random", "math.sin",
  "math.sqrt", "math.tan", "math.type", "math.ult",

  -- String
  "string", "string.byte", "string.char", "string.dump",
  "string.find", "string.format", "string.gmatch", "string.gsub",
  "string.len", "string.lower", "string.match", "string.pack",
  "string.packsize", "string.rep", "string.reverse", "string.sub",
  "string.unpack", "string.upper",

  -- Table
  "table", "table.concat", "table.insert", "table.move",
  "table.pack", "table.remove", "table.sort", "table.unpack",

  -- IO
  "io", "io.close", "io.flush", "io.input", "io.lines",
  "io.open", "io.output", "io.read", "io.stderr", "io.stdin",
  "io.stdout", "io.tmpfile", "io.type", "io.write",

  -- OS
  "os", "os.clock", "os.date", "os.difftime", "os.execute",
  "os.exit", "os.getenv", "os.remove", "os.rename",
  "os.setlocale", "os.time", "os.tmpname",

  -- Coroutine
  "coroutine", "coroutine.create", "coroutine.isyieldable",
  "coroutine.resume", "coroutine.running", "coroutine.status",
  "coroutine.wrap", "coroutine.yield",

  -- Debug
  "debug", "debug.debug", "debug.gethook", "debug.getinfo",
  "debug.getlocal", "debug.getmetatable", "debug.getregistry",
  "debug.getupvalue", "debug.getuservalue", "debug.sethook",
  "debug.setlocal", "debug.setmetatable", "debug.setupvalue",
  "debug.setuservalue", "debug.traceback", "debug.upvalueid",
  "debug.upvaluejoin",

  -- Moonix specific
  "moonix", "moonix.version", "moonix.tier",
  "moonix.compile", "moonix.jit",
}

local function complete(prefix)
  if not prefix or prefix == "" then
    return {}
  end

  local results = {}
  for _, kw in ipairs(keywords) do
    if kw:sub(1, #prefix) == prefix then
      table.insert(results, kw)
    end
  end

  table.sort(results)
  return results
end

return {
  complete = complete,
  keywords = keywords,
}
