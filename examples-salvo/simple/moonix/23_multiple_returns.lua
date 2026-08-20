local function split(value)
	return value, value * value
end
local a, b = split(6)
print(a, b)
