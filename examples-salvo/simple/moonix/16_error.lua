local function checked(value)
	if value < 0 then
		error("negative value")
	end
	return value
end
print(checked(42))
