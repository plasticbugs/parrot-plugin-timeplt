-- deep.lua: every way a script can nest C calls, taken until Lua refuses.
-- Each must end as an error the script can catch, never as a fault.
-- test/onarm.py runs this on an emulated Cortex-M33 and measures how deep
-- the C stack went.

local results = {}
local function try(name, f)
  local ok, err = pcall(f)
  results[#results + 1] = name .. (ok and " ok" or " stopped")
  menu_note(name .. (ok and " ok" or " stopped"))
end

-- brackets in an expression: the parser calls itself for each
try("brackets", function()
  for depth = 50, 400, 50 do
    local f = assert(load("return " .. string.rep("(", depth) .. "1" .. string.rep(")", depth)))
    assert(f() == 1)
  end
end)

-- nested function bodies and tables
try("functions", function()
  assert(load(string.rep("return function() ", 300) .. "return 1" .. string.rep(" end", 300)))
end)
try("tables", function()
  assert(load("return " .. string.rep("{", 300) .. string.rep("}", 300)))
end)
try("operators", function()
  assert(load("return " .. string.rep("-", 300) .. "1"))
  assert(load("return " .. string.rep("1 .. ", 300) .. "1"))
  assert(load("return " .. string.rep("2 ^ ", 300) .. "1"))
end)

-- pcall inside pcall
try("pcall", function()
  local function f(n)
    local ok, err = pcall(f, n + 1)
    if not ok then error(err, 0) end
  end
  f(1)
end)

-- a metamethod that calls itself
try("index", function()
  local t = setmetatable({}, {})
  getmetatable(t).__index = function(_, k) return t[k + 1] end
  return t[1]
end)
try("concat", function()
  local mt = {}
  mt.__concat = function(a, b) return a .. b end
  local a = setmetatable({}, mt)
  return a .. a
end)
try("tostring", function()
  local t = setmetatable({}, {})
  getmetatable(t).__tostring = function(v) return tostring(v) end
  return tostring(t)
end)

-- callbacks from the libraries
try("gsub", function()
  local function f(s) return (s:gsub(".", f)) end
  return f("abc")
end)
try("sort", function()
  local function f()
    local t = {3, 1, 2}
    table.sort(t, function(a, b) f() return a < b end)
  end
  f()
end)
try("format", function()
  local t = setmetatable({}, {})
  getmetatable(t).__tostring = function(v) return string.format("%s", v) end
  return string.format("%s", t)
end)

try("load", function()
  local function f(n) return load(function() f(n + 1) return nil end) end
  f(1)
end)
try("pattern", function()
  return string.find(string.rep("a", 300), string.rep("a?", 300) .. "b")
end)
try("gmatch", function()
  local function f(s) for _ in s:gmatch("%a") do s:gsub("%a", f) end end
  f("ab")
end)

-- coroutines inside coroutines
try("coroutines", function()
  local function f(n)
    return coroutine.wrap(function() return f(n + 1) end)()
  end
  return f(1)
end)

-- plain Lua recursion uses Lua's own stack, not C's: it ends as an error too
try("recursion", function()
  local function f(n) return 1 + f(n + 1) end
  return f(1)
end)

-- an error inside the error handler
try("xpcall", function()
  local function f() return xpcall(error, f) end
  return f()
end)

menu_note("deep done " .. #results)
function update() end
function draw() end
