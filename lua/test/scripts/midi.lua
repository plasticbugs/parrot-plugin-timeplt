-- midi.lua: a MIDI keyboard on the hub. What it plays is noted and sent
-- back a note higher, and a string goes out as it is.

function update()
  if frame() == 30 then
    menu_note("usb " .. usb.mode() .. " midi " .. tostring(usb.status().midi))
    midi.send(0x90, 60, 100)
    midi.send("\xC0\x05")
  end
end

function midimessage(status, a, b)
  menu_note("midi " .. status .. " " .. tostring(a) .. " " .. tostring(b))
  if status == 0x90 then midi.send(status, a + 1, b) end
end

function draw() end
