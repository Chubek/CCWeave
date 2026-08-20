local g = { { 2, 3 }, { 4 }, { 4 }, {} }
local indeg = { 0, 1, 1, 2 }
local q = { 1 }
local out = {}
while #q > 0 do
	local u = table.remove(q, 1)
	out[#out + 1] = u
	for _, v in ipairs(g[u]) do
		indeg[v] = indeg[v] - 1
		if indeg[v] == 0 then
			q[#q + 1] = v
		end
	end
end
print(table.concat(out, ","))
