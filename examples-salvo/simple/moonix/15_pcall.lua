local ok, message = pcall(function()
	error("expected failure")
end)
print(ok, message ~= nil)
