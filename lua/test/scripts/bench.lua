-- bench.lua: how fast Lua runs on the cart. test/onarm.py runs this on an
-- emulated Cortex-M33 and counts the instructions between the notes.
-- Each test does work a game does: arithmetic, tables, calls, strings.

local function mark(name) menu_note(name) end

mark("start")

-- 1. a loop of arithmetic on local variables
local x = 0
for i = 1, 200000 do x = x + i % 7 end
mark("loop")

-- 2. floats: a point moved around a circle
local px, py = 100.0, 0.0
for i = 1, 50000 do
  px, py = px * 0.999 - py * 0.04, py * 0.999 + px * 0.04
end
mark("floats")

-- 3. tables of fields, as a game keeps its things
local things = {}
for i = 1, 100 do things[i] = {x = i, y = i * 2, dx = 1, dy = -1, alive = true} end
for n = 1, 200 do
  for i = 1, #things do
    local t = things[i]
    t.x = t.x + t.dx
    t.y = t.y + t.dy
    if t.x > 255 or t.x < 0 then t.dx = -t.dx end
    if t.y > 191 or t.y < 0 then t.dy = -t.dy end
  end
end
mark("fields")

-- 4. calls
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
fib(22)
mark("calls")

-- 5. an array filled, summed and sorted
local a = {}
for i = 1, 5000 do a[i] = (i * 7919) % 1009 end
local sum = 0
for i = 1, #a do sum = sum + a[i] end
table.sort(a)
mark("array")

-- 6. strings made and thrown away, which keeps the garbage collector busy
local parts = {}
for i = 1, 2000 do parts[#parts + 1] = "item" .. i end
local joined = table.concat(parts, ",")
local count = 0
for w in joined:gmatch("item%d+") do count = count + 1 end
mark("strings")

-- 7. what a small game's update() does: 20 things moved and checked against each other
local objs = {}
for i = 1, 20 do objs[i] = {x = i * 12, y = i * 9, dx = (i % 3) - 1, dy = (i % 5) - 2, r = 4} end
local hits = 0
local function game_update()
  for i = 1, #objs do
    local o = objs[i]
    o.x = o.x + o.dx
    o.y = o.y + o.dy
    if o.x < 0 or o.x > 255 then o.dx = -o.dx end
    if o.y < 0 or o.y > 191 then o.dy = -o.dy end
  end
  for i = 1, #objs do
    local p = objs[i]
    for j = i + 1, #objs do
      local q = objs[j]
      local ddx, ddy = p.x - q.x, p.y - q.y
      if ddx * ddx + ddy * ddy < 64 then hits = hits + 1 end
    end
  end
end
for n = 1, 300 do game_update() end
mark("game")

local used, free = mem()
mark("end " .. used .. " " .. free)

function update() end
function draw() end
