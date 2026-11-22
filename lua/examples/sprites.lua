-- sprites.lua: bouncing balls.
--
-- A sprite is a small shape in one
-- colour that the console draws on
-- top of the picture. Moving one is
-- quick. There are 32 of them, and
-- the console shows 4 on any one
-- row of the TV.

-- A shape is 8 rows of 8 characters.
-- A dot is see-through.
shape(1, {
  "..####..",
  ".######.",
  "########",
  "########",
  "########",
  "########",
  ".######.",
  "..####..",
})

local colours = {
  RED, YELLOW, LIGHT_GREEN, CYAN,
  MAGENTA, WHITE, LIGHT_RED, BLUE,
}

-- 1 or 2 pixels a step, either way
local function speed()
  local s = math.random(1, 2)
  if math.random(2) == 1 then s = -s end
  return s
end

local balls = {}
for i = 1, 12 do
  balls[i] = {
    x = math.random(8, 240),
    y = math.random(24, 176),
    dx = speed(),
    dy = speed(),
    colour = colours[(i - 1) % 8 + 1],
  }
end

function update()
  for i, b in ipairs(balls) do
    b.x = b.x + b.dx
    b.y = b.y + b.dy
    if b.x < 0 or b.x > 248 then
      b.dx = -b.dx
    end
    if b.y < 16 or b.y > 184 then
      b.dy = -b.dy
      -- the first ball ticks
      if i == 1 then
        note(1, "C5", 8, 2)
      end
    end
    sprite(i, b.x, b.y, 1, b.colour)
  end
end

function draw()
  cls()
  print("Sprites", 107, 4, WHITE)
  line(0, 14, 255, 14, DARK_BLUE)
end
