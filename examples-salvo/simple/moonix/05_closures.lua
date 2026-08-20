local function make_counter()
	local value = 0
	return function()
		value = value + 1
		return value
	end
end
local next_value = make_counter()
print(next_value(), next_value())
