local a = { 2, 4, 1, 3, 5 }
local function sort(x)
	if #x < 2 then
		return x, 0
	end
	local m = math.floor(#x / 2)
	local l = {}
	local r = {}
	for i = 1, m do
		l[i] = x[i]
	end
	for i = m + 1, #x do
		r[#r + 1] = x[i]
	end
	l, li = sort(l)
	r, ri = sort(r)
	local z = {}
	local i, j, n = 1, 1, li + ri
	while i <= #l and j <= #r do
		if l[i] <= r[j] then
			z[#z + 1] = l[i]
			i = i + 1
		else
			z[#z + 1] = r[j]
			j = j + 1
			n = n + (#l - i + 1)
		end
	end
	while i <= #l do
		z[#z + 1] = l[i]
		i = i + 1
	end
	while j <= #r do
		z[#z + 1] = r[j]
		j = j + 1
	end
	return z, n
end
local _, inv = sort(a)
print(inv)
