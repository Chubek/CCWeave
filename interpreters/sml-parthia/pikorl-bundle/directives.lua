-- SML-Parthia REPL directives for PikoRL

local directives = {
  help = function(args)
    print("SML-Parthia REPL directives:")
    print("  #help              Show this help")
    print("  #quit, #q          Exit the REPL")
    print("  #open MODULE       Show a module's structure/signatures")
    print("  #use \"FILE\"        Load and compile SML source")
    print("  #load LIB          Load a native library")
    print("  #version           Show version info")
    print("  #history           Show command history")
    return true
  end,

  quit = function(args)
    return false
  end,

  q = function(args)
    return false
  end,

  version = function(args)
    print("SML-Parthia interpreter")
    return true
  end,

  history = function(args)
    if lpicorl and lpicorl.history then
      lpicorl.history()
    else
      print("History not available")
    end
    return true
  end,
}

local function handle_directive(line)
  if not line or line:sub(1, 1) ~= "#" then
    return nil
  end

  local cmd, args = line:match("^#(%S+)%s*(.*)$")
  if not cmd then
    return nil
  end

  local handler = directives[cmd]
  if handler then
    return handler(args)
  end

  print("Unknown directive: #" .. cmd .. ". Type #help for available directives.")
  return true
end

return {
  handle = handle_directive,
  directives = directives,
}
