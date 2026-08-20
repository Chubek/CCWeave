local function loop(n, total)
	if n == 0 then
		return total
	end
	return loop(n - 1, total + n)
end
print(loop(10, 0))
