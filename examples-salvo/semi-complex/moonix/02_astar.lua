local g = { {}, {} }
g[1] = { { 2, 1 }, { 3, 4 } }
g[2] = { { 4, 3 } }
g[3] = { { 4, 1 } }
g[4] = {}
local h = { 4, 3, 1, 0 }
local open = { 1 }
local cost = { [1] = 0 }
while #open > 0 do
	local k = 1
	for i = 2, #open do
		if cost[open[i]] + h[open[i]] < cost[open[k]] + h[open[k]] then
			k = i
		end
	end
	local u = table.remove(open, k)
	if u == 4 then
		break
	end
	for _, e in ipairs(g[u]) do
		local n = e[1]
		local c = cost[u] + e[2]
		if cost[n] == nil or c < cost[n] then
			cost[n] = c
			open[#open + 1] = n
		end
	end
end
print(cost[4])
