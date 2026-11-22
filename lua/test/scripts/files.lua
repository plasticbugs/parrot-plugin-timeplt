-- files.lua: the card. Opened from the menu, it reads, lists, makes and
-- writes files in its own folder, and counts its runs in a save.
-- test/run.sh runs it three times on a card image and checks the files.

local ok, err = pcall(function()
  local lib = require "lib"                     -- lib.lua, beside this script
  assert(lib.answer == 42, "require")
  assert(file.read("notes.txt") == "one\ntwo\nthree\n", "read")
  assert(file.read("notes.txt", 4, 3) == "two", "read part")
  assert(file.read("/LUA/notes.txt", 8) == "three\n", "read by its whole path")
  assert(file.size("notes.txt") == 14, "size")
  assert(file.size("nothere.txt") == nil, "size of nothing")
  assert(not pcall(file.read, "nothere.txt"), "read of nothing")

  local names = {}
  for _, e in ipairs(file.list()) do names[#names + 1] = e.name .. (e.folder and "/" or "") .. ":" .. e.size end
  table.sort(names)
  local listing = table.concat(names, " ")
  assert(listing:find("lib.lua:", 1, true) and listing:find("notes.txt:14", 1, true) and listing:find("SUB/:", 1, true), "list: " .. listing)
  assert(#file.list("/") >= 1, "list the card's top folder")
  assert(file.read("SUB/deep.txt") == "deep\n", "a folder inside")
  assert(not pcall(file.list, "NOFOLDER"), "list of nothing")

  -- a file of a fixed size, written in place
  local made = file.create("DATA.BIN", 1000)
  assert(file.size("DATA.BIN") == 1000, "create")
  if made then assert(file.read("DATA.BIN") == string.rep("\0", 1000), "a new file is zeros") end
  file.write("DATA.BIN", 510, "ACROSS")         -- over the end of its first 512 bytes
  assert(file.read("DATA.BIN", 508, 10) == "\0\0ACROSS\0\0", "write")
  assert(not pcall(file.write, "DATA.BIN", 998, "TOO LONG"), "a write past the end")
  assert(file.create("DATA.BIN", 1000) == false, "create again")

  -- a save: a string, kept between runs
  local runs = (tonumber(file.load("RUNS.SAV")) or 0) + 1
  file.save("RUNS.SAV", tostring(runs))
  assert(file.load("RUNS.SAV") == tostring(runs), "save")
  assert(file.load("nothere.sav") == nil, "load of nothing")
  assert(not pcall(file.save, "SMALL.SAV", string.rep("x", 200), 100), "a save too big for its room")
  file.save("SMALL.SAV", string.rep("y", 100), 100)
  assert(file.load("SMALL.SAV") == string.rep("y", 100), "a save that just fits")
  menu_note("files run " .. runs)
end)
if not ok then menu_note(tostring(err):sub(1, 32)) print(err) end

function draw() cls() print(ok and "FILES FINE" or "FILES WRONG", 8, 8, WHITE) end
