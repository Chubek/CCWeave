local inf = 999
local d = { { 0, 3, inf, 7 }, { 8, 0, 2, inf }, { 5, inf, 0, 1 }, { 2, inf, inf, 0 } }
for k = 1, 4 do
	for i = 1, 4 do
		for j = 1, 4 do
			if d[i][k] + d[k][j] < d[i][j] then
				d[i][j] = d[i][k] + d[k][j]
			end
		end
	end
end
print(d[1][4])
