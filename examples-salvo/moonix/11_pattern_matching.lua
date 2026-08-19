local word = "ccweave-42"
local name, number = string.match(word, "([%a-]+)(%d+)")
print(name, number)
