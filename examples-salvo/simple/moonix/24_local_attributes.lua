local answer <const> = 42
local resource <close> = setmetatable({}, { __close = function() end })
print(answer, resource ~= nil)
