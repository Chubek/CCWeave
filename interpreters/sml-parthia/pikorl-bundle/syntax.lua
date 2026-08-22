-- SML syntax highlighting for PikoRL REPL

local keywords = {
  ["abstype"] = true, ["and"] = true, ["andalso"] = true, ["as"] = true,
  ["case"] = true, ["datatype"] = true, ["do"] = true, ["else"] = true,
  ["end"] = true, ["eqtype"] = true, ["exception"] = true, ["fn"] = true,
  ["fun"] = true, ["functor"] = true, ["handle"] = true, ["if"] = true,
  ["in"] = true, ["include"] = true, ["infix"] = true, ["infixr"] = true,
  ["let"] = true, ["local"] = true, ["nonfix"] = true, ["of"] = true,
  ["op"] = true, ["open"] = true, ["orelse"] = true, ["raise"] = true,
  ["rec"] = true, ["sharing"] = true, ["sig"] = true, ["signature"] = true,
  ["struct"] = true, ["structure"] = true, ["then"] = true, ["type"] = true,
  ["val"] = true, ["where"] = true, ["while"] = true, ["with"] = true,
  ["withtype"] = true,
}

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
    if c == '"' then
      local j = i + 1
      while j <= len do
        if line:sub(j, j) == "\\" then
          j = j + 2
        elseif line:sub(j, j) == '"' then
          break
        else
          j = j + 1
        end
      end
      table.insert(result, "\27[33m")
      table.insert(result, line:sub(i, j))
      table.insert(result, "\27[0m")
      i = j + 1

    -- Comments: (* ... *)
    elseif c == "(" and i < len and line:sub(i+1, i+1) == "*" then
      local j = i + 2
      while j < len do
        if line:sub(j, j) == "*" and line:sub(j+1, j+1) == ")" then
          j = j + 2
          break
        end
        j = j + 1
      end
      table.insert(result, "\27[2;37m")
      table.insert(result, line:sub(i, j - 1))
      table.insert(result, "\27[0m")
      i = j

    -- Numbers
    elseif c:match("[0-9~]") then
      local j = i
      if c == "~" then j = j + 1 end
      while j <= len and line:sub(j, j):match("[0-9%.xXa-fA-F_]") do
        j = j + 1
      end
      table.insert(result, "\27[35m")
      table.insert(result, line:sub(i, j - 1))
      table.insert(result, "\27[0m")
      i = j

    -- Identifiers and keywords
    elseif c:match("[a-zA-Z'_]") then
      local j = i
      while j <= len and line:sub(j, j):match("[a-zA-Z0-9'_]") do
        j = j + 1
      end
      local word = line:sub(i, j - 1)
      if keywords[word] then
        table.insert(result, "\27[1;34m")
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
