local edges = { { 1, 2, 1 }, { 2, 3, 2 }, { 1, 3, 3 }, { 3, 4, 1 }, { 2, 4, 4 } }
table.sort(edges, function(a, b)
	return a[3] < b[3]
end)
local p = { 1, 2, 3, 4 }
local function f(x)
	while p[x] ~= x do
		p[x] = p[p[x]]
		x = p[x]
	end
	return x
end
local total = 0
for _, e in ipairs(edges) do
	local a, b = f(e[1]), f(e[2])
	if a ~= b then
		p[a] = b
		total = total + e[3]
	end
end
print(total)
