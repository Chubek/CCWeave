local b = {
	{ 5, 3, 0, 0, 7, 0, 0, 0, 0 },
	{ 6, 0, 0, 1, 9, 5, 0, 0, 0 },
	{ 0, 9, 8, 0, 0, 0, 0, 6, 0 },
	{ 8, 0, 0, 0, 6, 0, 0, 0, 3 },
	{ 4, 0, 0, 8, 0, 3, 0, 0, 1 },
	{ 7, 0, 0, 0, 2, 0, 0, 0, 6 },
	{ 0, 6, 0, 0, 0, 0, 2, 8, 0 },
	{ 0, 0, 0, 4, 1, 9, 0, 0, 5 },
	{ 0, 0, 0, 0, 8, 0, 0, 7, 9 },
}
local function ok(r, c, n)
	for i = 1, 9 do
		if b[r][i] == n or b[i][c] == n then
			return false
		end
	end
	local br = math.floor((r - 1) / 3) * 3
	local bc = math.floor((c - 1) / 3) * 3
	for i = 1, 3 do
		for j = 1, 3 do
			if b[br + i][bc + j] == n then
				return false
			end
		end
	end
	return true
end
local function solve()
	for r = 1, 9 do
		for c = 1, 9 do
			if b[r][c] == 0 then
				for n = 1, 9 do
					if ok(r, c, n) then
						b[r][c] = n
						if solve() then
							return true
						end
						b[r][c] = 0
					end
				end
				return false
			end
		end
	end
	return true
end
solve()
print(b[1][1], b[9][9])
