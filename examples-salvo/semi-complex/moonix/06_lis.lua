local a = { 10, 9, 2, 5, 3, 7, 101, 18 }
local dp = {}
local best = 0
for i = 1, #a do
	dp[i] = 1
	for j = 1, i - 1 do
		if a[j] < a[i] and dp[j] + 1 > dp[i] then
			dp[i] = dp[j] + 1
		end
	end
	if dp[i] > best then
		best = dp[i]
	end
end
print(best)
