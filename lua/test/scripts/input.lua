-- input.lua: every input a script can read, written to the menu's note as
-- it changes. test/run.sh plays a script of inputs and reads the notes.

local last = ""
local function say(s)
  if s ~= last then
    last = s
    menu_note(s)
  end
end

local function held()
  local s = ""
  for _, b in ipairs{"up", "down", "left", "right", "fire", "fire2"} do
    if btn(b) then s = s .. b .. " " end
  end
  return s
end

function update()
  for _, b in ipairs{"up", "down", "left", "right", "fire", "fire2"} do
    if btnp(b) then say("pressed " .. b .. " held " .. held()) end
  end
  if keypadp() then say("keypad " .. keypadp() .. " " .. tostring(keypad())) end
  if frame() == 1 then
    say("usb " .. usb.mode())
  end
  local p = pad(1)
  if p and (p.south or p.l) then
    say("pad1 " .. p.kind .. " south " .. tostring(p.south) .. " l " .. tostring(p.l) .. " lx " .. p.lx .. " ly " .. p.ly)
  end
  local p2 = pad(2)
  if p2 and p2.east then say("pad2 east") end
  local m = mouse()
  if m and (m.dx ~= 0 or m.dy ~= 0) then
    say("mouse " .. m.x .. "," .. m.y .. " d " .. m.dx .. "," .. m.dy .. " left " .. tostring(m.left) .. " wheel " .. m.wheel)
  end
end

function keypressed(k, char)
  say("key " .. k .. " char " .. tostring(char) .. " down " .. tostring(key(k)))
end

function keyreleased(k)
  say("up " .. k .. " down " .. tostring(key(k)))
end

function midimessage(status, a, b)
  say("midi " .. status .. " " .. tostring(a) .. " " .. tostring(b))
  midi.send(status, a, b)           -- and back out
end

function draw()
  cls()
  print(last, 0, 8, WHITE)
end
