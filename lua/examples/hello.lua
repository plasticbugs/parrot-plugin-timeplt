-- hello.lua: a ball you can steer.
-- Press 4 or Ctrl+R to run it.

local x, y = 120, 90   -- the ball
local dx, dy = 1, 1    -- its speed

-- update() runs 60 times a second.
-- Move things here.
function update()
  if btn("left") then dx = -1 end
  if btn("right") then dx = 1 end
  if btn("up") then dy = -1 end
  if btn("down") then dy = 1 end
  x = x + dx
  y = y + dy
  if x < 8 or x > 247 then
    dx = -dx
    -- voice 1 plays C5 at volume 10
    -- for 4 updates
    note(1, "C5", 10, 4)
  end
  if y < 28 or y > 183 then
    dy = -dy
    note(1, "G4", 10, 4)
  end
end

-- draw() runs when the console is
-- ready for a new picture. Draw
-- everything here, every time.
function draw()
  cls()
  print("Hello from Lua", 86, 8, YELLOW)
  circfill(x, y, 6, LIGHT_GREEN)
end
