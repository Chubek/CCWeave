local text, pat = "ababcabcabababd", "ababd"
local lps = { 0 }
local j = 0
for i = 2, #pat do
	while j > 0 and pat:sub(i, i) ~= pat:sub(j + 1, j + 1) do
		j = lps[j]
	end
	if pat:sub(i, i) == pat:sub(j + 1, j + 1) then
		j = j + 1
	end
	lps[i] = j
end
local i = 1
j = 0
local found = -1
while i <= #text do
	if text:sub(i, i) == pat:sub(j + 1, j + 1) then
		i = i + 1
		j = j + 1
		if j == #pat then
			found = i - #pat
			break
		end
	elseif j > 0 then
		j = lps[j]
	else
		i = i + 1
	end
end
print(found)
