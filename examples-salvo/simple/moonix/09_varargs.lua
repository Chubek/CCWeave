local function total(...)
	local sum = 0
	for _, value in ipairs({ ... }) do
		sum = sum + value
	end
	return sum
end
print(total(1, 2, 3, 4))
