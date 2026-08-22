-- Moonix theme for PikoRL REPL

local theme = {
  name = "moonix-default",
  description = "Default Moonix REPL theme",

  -- Prompt colors
  prompt = "\27[1;36m",       -- bold cyan
  continuation = "\27[1;33m", -- bold yellow

  -- Syntax highlighting colors
  keyword = "\27[1;34m",      -- bold blue
  string_literal = "\27[33m", -- yellow
  number = "\27[35m",         -- magenta
  comment = "\27[2;37m",      -- dim white
  identifier = "\27[0m",      -- default

  -- Output colors
  result = "\27[32m",         -- green
  error = "\27[1;31m",        -- bold red
  warning = "\27[33m",        -- yellow

  -- Reset
  reset = "\27[0m",
}

local function apply_theme()
  return theme
end

local function colorize(text, color_name)
  local color = theme[color_name] or ""
  return color .. text .. theme.reset
end

return {
  theme = theme,
  apply = apply_theme,
  colorize = colorize,
}
