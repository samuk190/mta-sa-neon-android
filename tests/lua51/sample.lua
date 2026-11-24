-- Lua 5.1 only: unpack, table.getn, setfenv, # on strings, string methods.
local t = {10, 20, 30}
local a, b, c = unpack(t)
local env = {x = 7}
local f = function() return x end
setfenv(f, env)
return a + b + c + table.getn(t) + f() + #("abc") + ("x"):len() + utf8.len("héllo")
