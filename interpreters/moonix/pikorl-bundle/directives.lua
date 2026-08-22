-- Moonix REPL directives for PikoRL

local directives = {
  help = function(args)
    print("Moonix REPL directives:")
    print("  #help              Show this help")
    print("  #quit, #q          Exit the REPL")
    print("  #tier <t0|t1|t2>   Set compilation tier")
    print("  #version           Show Moonix version")
    print("  #history           Show command history")
    return true
  end,

  quit = function(args)
    return false  -- signal to exit
  end,

  q = function(args)
    return false
  end,

  tier = function(args)
    if not args or args == "" then
      print("Current tier: " .. (moonix_get_tier and moonix_get_tier() or "unknown"))
      return true
    end
    local tier = args:match("^%s*(%S+)%s*$")
    if tier == "t0" or tier == "t1" or tier == "t2" then
      if moonix_set_tier then
        moonix_set_tier(tier)
        print("Tier set to: " .. tier)
      else
        print("Tier selection not available")
      end
    else
      print("Invalid tier: " .. tier .. ". Use t0, t1, or t2.")
    end
    return true
  end,

  version = function(args)
    if moonix_version then
      print(moonix_version())
    else
      print("Moonix REPL")
    end
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
