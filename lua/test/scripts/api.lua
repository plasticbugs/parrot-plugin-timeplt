-- api.lua: the runtime's functions, checked from a script. It runs as a
-- Lua app with helper.lua and data.txt built in (test/run.sh on the
-- console's model, test/onarm.sh on an emulated Cortex-M33).
-- The note it leaves says how many checks passed.

local passed = 0
local function check(what, ok)
  if not ok then error("check failed: " .. what, 2) end
  passed = passed + 1
end
local function fails(what, f, ...)
  local ok, err = pcall(f, ...)
  check(what .. " should fail", not ok)
  return tostring(err)
end

-- numbers: 32-bit integers and floats
check("maxinteger", math.maxinteger == 2147483647)
check("integer wraps", math.maxinteger + 1 == math.mininteger)
check("float", 1 / 2 == 0.5)
check("integer division", 7 // 2 == 3 and 7 % 2 == 1)
check("float to text", tostring(0.5) == "0.5" and tostring(1e10) == "1e+10")
check("integer to text", tostring(123456789) == "123456789")
check("text to float", tonumber("0.25") == 0.25 and tonumber("1e3") == 1000.0 and tonumber("-12.5") == -12.5)
check("text to float, long", math.abs(tonumber("3.14159265358979") - math.pi) < 1e-6)
check("text to float, small", tonumber("1.5e-10") > 1.4e-10 and tonumber("1.5e-10") < 1.6e-10)
check("hex", tonumber("0x10") == 16 and 0x7fffffff == math.maxinteger and tonumber("0x.8") == 0.5)
check("not a number", tonumber("12abc") == nil and tonumber("") == nil and tonumber("1e") == nil)
check("float exact", 16777216.0 == 2 ^ 24 and 16777217.0 == 16777216.0)
check("format", string.format("%5.2f|%d|%x|%s", 3.14159, 42, 255, "a") == " 3.14|42|ff|a")
check("math", math.floor(2.7) == 2 and math.abs(math.sin(math.pi / 2) - 1) < 1e-6 and math.sqrt(16) == 4.0)
math.randomseed(7)
local r1 = math.random(1, 100)
math.randomseed(7)
check("random", math.random(1, 100) == r1 and r1 >= 1 and r1 <= 100)

-- the libraries the cart has, and the ones it doesn't
check("libraries", string and table and math and coroutine)
check("no io, os, debug", io == nil and os == nil and debug == nil and package == nil)
check("coroutines", coroutine.wrap(function() coroutine.yield(1) end)() == 1)
check("string.pack", string.unpack("<I2", string.pack("<I2", 513)) == 513)

-- drawing
cls()
check("cls", pget(0, 0) == BLACK and pget(255, 191) == BLACK)
pset(10, 10, RED)
check("pset", pget(10, 10) == RED)
pset(11, 10)                                    -- no colour: the last one
check("pen", pget(11, 10) == RED)
pset(-5, 10, WHITE) pset(300, 10, WHITE) pset(10, 5000, WHITE)     -- off the screen: nothing
check("off screen", pget(-5, 10) == 0 and pget(300, 10) == 0)
pset(12.9, 10.2, CYAN)
check("fractions round down", pget(12, 10) == CYAN)
line(0, 20, 30, 20, WHITE)
check("line", pget(0, 20) == WHITE and pget(30, 20) == WHITE and pget(31, 20) == BLACK)
line(40, 0, 50, 10, GREEN)
check("diagonal", pget(40, 0) == GREEN and pget(45, 5) == GREEN and pget(50, 10) == GREEN)
line(-1000, -1000, 99999, 99999, GRAY)          -- far off both ends: ends quickly
rect(60, 30, 10, 5, YELLOW)
check("rect", pget(60, 30) == YELLOW and pget(69, 34) == YELLOW and pget(62, 32) == BLACK and pget(70, 30) == BLACK)
rectfill(80, 30, 10, 5, BLUE)
check("rectfill", pget(80, 30) == BLUE and pget(89, 34) == BLUE and pget(85, 32) == BLUE and pget(90, 32) == BLACK)
rectfill(-10, -10, 15, 15, MAGENTA)
check("rectfill clipped", pget(0, 0) == MAGENTA and pget(4, 4) == MAGENTA and pget(5, 4) == BLACK)
circ(128, 96, 10, WHITE)
check("circ", pget(138, 96) == WHITE and pget(118, 96) == WHITE and pget(128, 86) == WHITE and pget(128, 96) == BLACK)
circfill(200, 96, 10, RED)
check("circfill", pget(200, 96) == RED and pget(210, 96) == RED and pget(211, 96) == BLACK)
cls(DARK_BLUE)
check("cls colour", pget(100, 100) == DARK_BLUE)
cls(0)
check("colour 0 is black", pget(100, 100) == BLACK)
check("print returns x", print("Ab", 16, 40, WHITE) == 28)
local lit = 0
for y = 40, 47 do for x = 16, 27 do if pget(x, y) == WHITE then lit = lit + 1 end end end
check("print drew", lit > 10 and pget(28, 40) == BLACK)
check("print scale", print("A", 0, 60, WHITE, 2) == 12)
check("print of a number", print(42, 0, 80) == 12)
cls()
print("one") print("two")                       -- with no place: down the screen
lit = 0
for y = 8, 15 do for x = 0, 17 do if pget(x, y) ~= BLACK then lit = lit + 1 end end end
check("print down the screen", lit > 10)
check("print off the screen", print("OFF", 300, 300) == 318)
fails("colour 16", pset, 0, 0, 16)
fails("colour as text", pset, 0, 0, "red")

local ship = image{
  ".f.",
  "f8f",
  "...",
}
check("image size", ship.w == 3 and ship.h == 3)
cls()
blit(ship, 100, 100)
check("blit", pget(101, 100) == WHITE and pget(101, 101) == RED and pget(100, 100) == BLACK)
cls()
blit(ship, 100, 100, false, true)
check("blit flipped", pget(101, 102) == WHITE and pget(101, 100) == BLACK)
cls()
blit(ship, 100, 100, false, false, CYAN)
check("blit one colour", pget(101, 100) == CYAN and pget(101, 101) == CYAN)
blit(ship, -2, -2) blit(ship, 254, 190)         -- over the edges
fails("image row", image, {"xyz"})
fails("image empty", image, {})
fails("blit a table", blit, {}, 0, 0)

-- sound: the calls are checked here, the chip's bytes by the test's log
tone(1, 440, 15)
tone(2, 220)
tone(3, 0)
note(1, "A4", 12, 10)
note(2, 60)
note(3, "C#5", 8)
note(1, "Bb3")
noise(1, 10, 5)
noise(8, 15)
mute()
fails("voice 4", tone, 4, 440)
fails("volume 16", tone, 1, 440, 16)
fails("note H", note, 1, "H4")
fails("noise 9", noise, 9)

-- the controller: nothing is held while the file runs
check("btn", btn("up") == false and btnp("fire") == false and btn("fire2") == false)
check("keypad", keypad() == nil and keypadp() == nil)
fails("button name", btn, "jump")

-- time and memory
check("time", time() >= 0 and time() < 5)
check("frame", frame() == 0)
local used, free = mem()
check("mem", used > 1000 and free > 1000)
check("size", WIDTH == 256 and HEIGHT == 192)

-- more scripts and files, built into the plugin
local helper = require "helper"
check("require", helper.double(21) == 42)
check("require once", require("helper") == helper and loaded_helper == 1)
fails("require missing", require, "nothere")
check("file.read", file.read("data.txt") == "line one\nline two\n")
check("file.read part", file.read("data.txt", 5, 3) == "one")
check("file.read past the end", file.read("data.txt", 100) == "")
check("file.size", file.size("data.txt") == 18)
check("loadfile", loadfile("helper.lua")().double(4) == 8)
check("load", load("return 1 + 1")() == 2)
local msg = fails("syntax", function() assert(load("x = = 1", "=chunk")) end)
check("syntax message", msg:find("chunk:1:", 1, true) ~= nil)

-- USB: nothing plugged in while the file runs
check("usb.mode", type(usb.mode()) == "string")
check("usb.status", usb.status().pads == 0)
check("pad", pad(1) == nil and pad(2) == nil)
check("mouse", mouse() == nil)
check("keydown", key("a") == false)
fails("pad 3", pad, 3)

menu_note("api " .. passed .. " passed")

function update() end
function draw() cls() print("API " .. passed, 8, 8, WHITE) end
