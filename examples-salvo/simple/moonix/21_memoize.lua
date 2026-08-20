local cache = {}
local function fib(n)
	if n < 2 then
		return n
	end
	if cache[n] then
		return cache[n]
	end
	cache[n] = fib(n - 1) + fib(n - 2)
	return cache[n]
end
print(fib(10))
