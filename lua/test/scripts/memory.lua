-- memory.lua: a script that keeps everything it makes. Lua runs out of
-- memory and says so. The cart keeps running.
local keep = {}
function update()
  if frame() == 5 then
    local used, free = mem()
    menu_note("free " .. (free > 4000000 and "over 4 MB" or "under 4 MB"))
  end
  if frame() >= 10 then
    for i = 1, 2000 do keep[#keep + 1] = {i, i, i, i, tostring(i) .. "............................................................"} end
  end
end
function draw() cls() print("FILLING UP", 8, 8, WHITE) end
