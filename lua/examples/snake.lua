-- snake.lua: a small game.
-- The stick steers. Each apple makes
-- the snake longer and faster. The
-- best score is kept on the card.
--
-- Everything is drawn on a grid of
-- 8x8 squares, one colour a square,
-- so the colours never clash.

local COLS, ROWS = 32, 21  -- the field
local TOP = 3      -- its first row
local SAVE = "SNAKE.SAV"

local snake   -- squares, head first
local dir     -- where it's going
local turn    -- the next turn
local apple
local score, best = 0, 0
local state = "title"
local wait = 0  -- updates to a step

local function place_apple()
  repeat
    apple = {
      x = math.random(0, COLS - 1),
      y = math.random(0, ROWS - 1),
    }
    local free = true
    for _, s in ipairs(snake) do
      if s.x == apple.x
          and s.y == apple.y then
        free = false
      end
    end
  until free
end

local function start()
  snake = {}
  for i = 0, 4 do
    snake[i + 1] = {x = 12 - i, y = 10}
  end
  dir = {x = 1, y = 0}
  turn = dir
  score = 0
  place_apple()
  state = "play"
end

local function game_over()
  state = "over"
  noise(3, 12, 20)  -- a low hiss
  if score > best then
    best = score
    -- The card may be missing, so
    -- the save is allowed to fail.
    pcall(file.save, SAVE,
          tostring(best))
  end
  menu_note("Snake: " .. score
            .. " points")
end

local function step()
  dir = turn
  local head = {
    x = snake[1].x + dir.x,
    y = snake[1].y + dir.y,
  }
  if head.x < 0 or head.x >= COLS
      or head.y < 0
      or head.y >= ROWS then
    return game_over()
  end
  for i = 1, #snake - 1 do
    if snake[i].x == head.x
        and snake[i].y == head.y then
      return game_over()
    end
  end
  table.insert(snake, 1, head)
  if head.x == apple.x
      and head.y == apple.y then
    score = score + 1
    note(1, "E5", 12, 3)
    note(2, "B5", 9, 6)
    place_apple()
  else
    snake[#snake] = nil
  end
end

function init()
  best = tonumber(file.load(SAVE)) or 0
end

function update()
  if state ~= "play" then
    if btnp("fire") or btnp("fire2")
    then
      start()
    end
    return
  end
  -- A turn waits for the next step.
  -- The snake can't turn back.
  if btnp("up") and dir.y == 0 then
    turn = {x = 0, y = -1}
  end
  if btnp("down") and dir.y == 0 then
    turn = {x = 0, y = 1}
  end
  if btnp("left") and dir.x == 0 then
    turn = {x = -1, y = 0}
  end
  if btnp("right") and dir.x == 0 then
    turn = {x = 1, y = 0}
  end
  wait = wait - 1
  if wait <= 0 then
    wait = math.max(3, 10 - score // 4)
    step()
  end
end

local function square(x, y, c)
  local py = (y + TOP) * 8
  rectfill(x * 8, py, 8, 8, c)
end

function draw()
  cls()
  print("Snake", 8, 4, YELLOW)
  print("Score " .. score, 96, 4, WHITE)
  print("Best " .. best, 196, 4, GRAY)
  rectfill(0, 16, 256, 8, DARK_BLUE)
  if state == "title" then
    print("Snake", 83, 60, GREEN, 3)
    print("Press fire", 98, 112, WHITE)
    return
  end
  square(apple.x, apple.y, RED)
  for i, s in ipairs(snake) do
    local c = GREEN
    if i == 1 then c = LIGHT_GREEN end
    square(s.x, s.y, c)
  end
  if state == "over" then
    -- a box behind the words, so they
    -- don't clash with the snake
    rectfill(48, 72, 160, 56, BLACK)
    print("Game over", 74, 80, WHITE, 2)
    print("Press fire", 98, 112, WHITE)
  end
end
