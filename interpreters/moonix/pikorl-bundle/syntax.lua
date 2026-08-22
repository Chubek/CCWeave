-- Moonix (Lua) syntax highlighting for PikoRL REPL
-- Provides a simple token-based colorizer for Lua code.

local keywords = {
  ["and"] = true, ["break"] = true, ["do"] = true, ["else"] = true,
  ["elseif"] = true, ["end"] = true, ["false"] = true, ["for"] = true,
  ["function"] = true, ["goto"] = true, ["if"] = true, ["in"] = true,
  ["local"] = true, ["nil"] = true, ["not"] = true, ["or"] = true,
  ["repeat"] = true, ["return"] = true, ["then"] = true, ["true"] = true,
  ["until"] = true, ["while"] = true,

  -- Moonix extensions
  ["moonix"] = true, ["jit"] = true, ["tier"] = true,
}

local function is_keyword(word)
  return keywords[word] or false
end

local function highlight_line(line)
  if not line or line == "" then
    return line
  end

  local result = {}
  local i = 1
  local len = #line

  while i <= len do
    local c = line:sub(i, i)

    -- String literals
    if c == '"' or c == "'" then
      local quote = c
      local j = i + 1
      while j <= len do
        if line:sub(j, j) == "\\" then
          j = j + 2
        elseif line:sub(j, j) == quote then
          break
        else
          j = j + 1
        end
      end
      table.insert(result, "\27[33m") -- yellow
      table.insert(result, line:sub(i, j))
      table.insert(result, "\27[0m")
      i = j + 1

    -- Comments
    elseif c == "-" and i < len and line:sub(i+1, i+1) == "-" then
      local j = i + 2
      if j <= len and line:sub(j, j) == "[" then
        -- Long comment
        local k = j + 1
        while k <= len do
          if line:sub(k, k) == "]" then
            break
          end
          k = k + 1
        end
        j = k
      end
      table.insert(result, "\27[2;37m") -- dim white
      table.insert(result, line:sub(i, len))
      table.insert(result, "\27[0m")
      i = len + 1

    -- Numbers
    elseif c:match("[0-9]") then
      local j = i
      while j <= len and line:sub(j, j):match("[0-9%.xXa-fA-F_]") do
        j = j + 1
      end
      table.insert(result, "\27[35m") -- magenta
      table.insert(result, line:sub(i, j - 1))
      table.insert(result, "\27[0m")
      i = j

    -- Identifiers and keywords
    elseif c:match("[a-zA-Z_]") then
      local j = i
      while j <= len and line:sub(j, j):match("[a-zA-Z0-9_]") do
        j = j + 1
      end
      local word = line:sub(i, j - 1)
      if is_keyword(word) then
        table.insert(result, "\27[1;34m") -- bold blue
      else
        table.insert(result, "\27[0m")
      end
      table.insert(result, word)
      table.insert(result, "\27[0m")
      i = j

    else
      table.insert(result, c)
      i = i + 1
    end
  end

  return table.concat(result)
end

return {
  highlight = highlight_line,
  keywords = keywords,
}
