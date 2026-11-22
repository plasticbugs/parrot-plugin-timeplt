-- sound.lua: the sound functions, in a fixed order. test/run.sh reads the
-- chip's bytes from the log.

function update()
  local f = frame()
  if f == 10 then tone(1, 440, 15) end                  -- A4, loudest
  if f == 20 then tone(1, 440, 0) end                   -- off
  if f == 30 then note(2, "C4", 13, 6) end              -- middle C, for 6 updates
  if f == 50 then note(3, 69, 10) noise(2, 12, 3) end   -- A4 by its MIDI number, and a hiss
  if f == 70 then noise(8, 15) end                      -- a buzz at voice 3's pitch
  if f == 90 then mute() end
  if f == 100 then tone(1, 50) end                      -- too low for the chip: its lowest note
  if f == 110 then menu_note("sound done") mute() end
end

function draw() end
