-- colours.lua: a pattern that grows
-- by itself, in all 15 colours.
--
-- Each step draws one square and its
-- three mirror images. The squares
-- sit on a grid of 8 pixels. That's
-- how to use many colours here: the
-- console shows two colours in each
-- 8 pixels along a row.

local x, y = 8, 6   -- in squares
local colour = 2
local steps = 0

local function square(sx, sy, c)
  rectfill(sx * 8, sy * 8, 8, 8, c)
end

function update()
  -- wander one square, any way
  x = (x + math.random(-1, 1)) % 16
  y = (y + math.random(-1, 1)) % 12
  steps = steps + 1
  if steps % 40 == 0 then
    colour = colour % 15 + 1
  end
end

-- The picture builds up a little at
-- a time, so there's no cls() on
-- every draw. cleared is true when
-- the screen was wiped.
function draw(cleared)
  if cleared then cls() end
  square(x, y, colour)
  square(31 - x, y, colour)
  square(x, 23 - y, colour)
  square(31 - x, 23 - y, colour)
end
