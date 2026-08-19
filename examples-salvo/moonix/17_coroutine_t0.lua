local co = coroutine.create(function()
  coroutine.yield("paused")
  return "done"
end)
local _, first = coroutine.resume(co)
local _, second = coroutine.resume(co)
print(first, second)
