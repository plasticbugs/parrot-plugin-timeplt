-- error.lua: a script that goes wrong after half a second, three calls deep
local function inner(t) return t.x.y end
local function middle(t) return inner(t) + 1 end
function update()
  if frame() == 30 then middle({}) end
end
function draw() cls() print("FINE SO FAR", 8, 8, WHITE) end
