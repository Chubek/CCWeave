-- SML auto-completion for PikoRL REPL

local keywords = {
  "abstype", "and", "andalso", "as", "case", "datatype", "do",
  "else", "end", "eqtype", "exception", "fn", "fun", "functor",
  "handle", "if", "in", "include", "infix", "infixr", "let",
  "local", "nonfix", "of", "op", "open", "orelse", "raise",
  "rec", "sharing", "sig", "signature", "struct", "structure",
  "then", "type", "val", "where", "while", "with", "withtype",

  -- Basis types
  "int", "real", "string", "char", "bool", "unit", "list",
  "option", "vector", "array", "ref", "exn", "word",

  -- Basis functions
  "print", "map", "foldl", "foldr", "app", "rev", "length",
  "hd", "tl", "null", "implode", "explode", "chr", "ord",
  "str", "size", "substring", "concat", "Int.toString",
  "Real.toString", "Bool.toString", "String.concat",
  "List.map", "List.filter", "List.foldl", "List.foldr",
  "Option.map", "Option.getOpt", "Option.valOf",
  "Vector.tabulate", "Vector.length", "Vector.sub",
  "Array.array", "Array.tabulate", "Array.length", "Array.sub",
  "Array.update",
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
