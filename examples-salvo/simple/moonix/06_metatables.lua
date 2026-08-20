local point = setmetatable({ x = 3, y = 4 }, {
	__add = function(a, b)
		return { x = a.x + b.x, y = a.y + b.y }
	end,
})
local other = { x = 1, y = 2 }
local result = point + other
print(result.x, result.y)
