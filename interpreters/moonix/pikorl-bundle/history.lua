-- Moonix history feature for PikoRL REPL

local history = {}
local max_size = 1000

local function load_history()
  return history
end

local function add_to_history(line)
  if not line or line == "" then
    return
  end
  -- Avoid duplicate consecutive entries
  if #history > 0 and history[#history] == line then
    return
  end
  table.insert(history, line)
  if #history > max_size then
    table.remove(history, 1)
  end
end

local function show_history()
  for i, entry in ipairs(history) do
    print(string.format("%4d  %s", i, entry))
  end
end

local function clear_history()
  history = {}
end

local function get_history(index)
  if index and index >= 1 and index <= #history then
    return history[index]
  end
  return nil
end

return {
  load = load_history,
  add = add_to_history,
  show = show_history,
  clear = clear_history,
  get = get_history,
  size = function() return #history end,
}
