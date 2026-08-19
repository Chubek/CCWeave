local Counter = {}
Counter.__index = Counter
function Counter.new() return setmetatable({value = 0}, Counter) end
function Counter:inc() self.value = self.value + 1 end
local counter = Counter.new()
counter:inc(); counter:inc()
print(counter.value)
