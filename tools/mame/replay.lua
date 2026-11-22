-- replay.lua: MAME's ColecoVision runs one of the SDK's two Z80 kernels, and
-- this script is the cart behind it: it serves the lists a plugin sent
-- (PC_LISTS: the lists.bin that test/host/host.c records), one after
-- another, the way the cart's core 1 does, then takes a snapshot and stops.
--
--   the screen kernel ('K' records)  a VDP write list at $A000 and the
--     mailbox at $FF00; the next list once the kernel reports the last one
--     run (ACK, or HELLO before its first: a read of $FF80 / $FFA0 | seq)
--   the frame kernel ('F' records)   a list at $C000, a new one for each
--     read of $FFF0 | seq (one a TV frame)
--
-- tools/mame-replay.sh runs it; MAME never opens a window.

-- MAME runs an autoboot script again at each reset; once is enough
if replay_loaded then return end
replay_loaded = true

local f = assert(io.open(assert(os.getenv("PC_LISTS"), "PC_LISTS"), "rb"))
local data = f:read("a")
f:close()
local lists, p = {}, 1
while p + 8 <= #data do
  local kind, full, seq, len = string.unpack("<BBBI2", data, p)
  lists[#lists + 1] = { kind = string.char(kind), full = full, seq = seq, bytes = data:sub(p + 9, p + 8 + len) }
  p = p + 9 + len
end
assert(#lists > 0, "no lists in PC_LISTS")
local frames_kernel = lists[1].kind == "F"
print(string.format("replay: %d lists for the %s kernel", #lists, frames_kernel and "frame" or "screen"))

local prog = manager.machine.devices[":maincpu"].spaces["program"]
local at, cur = 0, nil          -- lists served so far, and the one being served
local LINGER = 20                -- TV frames between the last list's end and the snapshot: MAME
                                -- draws a frame as it goes, and skips some when it runs flat out
local linger = nil              -- what is left of them
local finished = false

local function finish()
  if finished then return end
  finished = true
  manager.machine.video:snapshot()
  print(string.format("replay: %d of %d lists run", at, #lists))
  manager.machine:exit()
end

if frames_kernel then
  local EMPTY = "\0\0"
  local bytes = EMPTY
  -- The kernel's NMI handler writes $7000 at each vertical blank, and the
  -- kernel asks for its list ~500 Z80 cycles later. Asked much later than
  -- that, the list before was still running: it outlasted its frame.
  local t_blank, late = 0, 0
  replay_nmi = prog:install_write_tap(0x7000, 0x7000, "blank", function(offset, d, mask)
    t_blank = manager.machine.time:as_double()
  end)
  replay_tap = prog:install_read_tap(0xC000, 0xFFFF, "cart", function(offset, d, mask)
    if offset >= 0xFFF0 then    -- a frame starts: its list
      if at >= 1 and at <= #lists and (manager.machine.time:as_double() - t_blank) * 3579545 > 2000 then late = late + 1 end
      if at < #lists then
        at = at + 1
        bytes = lists[at].bytes
      else
        if not linger then print(string.format("replay: %d lists took longer than their TV frame", late)) end
        bytes = EMPTY
        linger = (linger or LINGER) - 1
        if linger == 0 then finish() end
      end
      return 0xFF
    elseif offset >= 0xFFC0 then
      return 0xFF               -- the controller's reports
    end
    return bytes:byte(offset - 0xC000 + 1) or 0
  end)
else
  -- the mailbox: silence (the PSG's own latch bytes), no list yet
  local mb = {}
  for i = 0, 0x7F do mb[i] = 0 end
  local silence = { 0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF }
  for i = 1, 10 do mb[i] = silence[i] end
  replay_tap = prog:install_read_tap(0xA000, 0xFFFF, "cart", function(offset, d, mask)
    if offset >= 0xFF80 then
      local kind, seq = (offset >> 5) & 3, offset & 31
      if kind <= 1 and (cur == nil or seq == (cur.seq & 31)) then   -- ACK or HELLO: the last list is done with
        if at < #lists then
          at = at + 1
          cur = lists[at]
          mb[0x11] = cur.full   -- LIST_FULL, then LIST_SEQ
          mb[0x10] = cur.seq
        else
          linger = (linger or 2 * LINGER) - 1   -- (each report is read twice)
          if linger == 0 then finish() end
        end
      end
      return 0xFF
    elseif offset >= 0xFF00 then
      return mb[offset - 0xFF00]
    end
    return cur and cur.bytes:byte(offset - 0xA000 + 1) or 0xFF
  end)
end
