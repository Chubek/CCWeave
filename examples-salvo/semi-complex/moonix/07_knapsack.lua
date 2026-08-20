local w = { 2, 3, 4, 5 }
local v = { 3, 4, 5, 6 }
local cap = 8
local dp = {}
for c = 0, cap do
	dp[c] = 0
end
for i = 1, #w do
	for c = cap, w[i], -1 do
		if dp[c - w[i]] + v[i] > dp[c] then
			dp[c] = dp[c - w[i]] + v[i]
		end
	end
end
print(dp[cap])
