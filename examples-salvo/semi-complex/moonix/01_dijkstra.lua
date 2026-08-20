local g = { {}, {} }
g[1] = { { 2, 4 }, { 3, 1 } }
g[2] = { { 4, 2 } }
g[3] = { { 2, 1 }, { 4, 5 } }
g[4] = {}
local d = { 0, math.huge, math.huge, math.huge }
local used = {}
for _ = 1, 4 do
	local u, best = nil, math.huge
	for i = 1, 4 do
		if not used[i] and d[i] < best then
			u, best = i, d[i]
		end
	end
	used[u] = true
	for _, e in ipairs(g[u]) do
		if d[e[1]] > d[u] + e[2] then
			d[e[1]] = d[u] + e[2]
		end
	end
end
print(d[4])
