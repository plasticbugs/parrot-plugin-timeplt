-- helper.lua: a module for api.lua's require test
local M = {}
M.loads = (M.loads or 0) + 1
function M.double(n) return n * 2 end
loaded_helper = (loaded_helper or 0) + 1
return M
