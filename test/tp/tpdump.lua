-- tpdump.lua: run by test/tp/mamerun.sh in a headless MAME. Each frame (at
-- the VBLANK start, where MAME's frame_done runs) it applies the inputs the
-- script file gives for that frame and appends the board's state to the
-- dump file, the same records tp_host writes:
--   'TPFR', frame (u32), colour RAM 0xa000, video RAM 0xa400 (0x400 each),
--   RAM 0xa800 (0x800), sprite RAM 0xb000 and 0xb400 (0x100 each), then for
--   the main CPU and the sound CPU: PC SP AF BC DE HL IX IY AF2 BC2 DE2 HL2
--   WZ as u16, R I IM IFF1 IFF2 HALT as u8; the sound CPU's RAM (0x400)
-- TP_SCRIPT, TP_DUMP, TP_FRAMES, TP_PIX and TP_PIXEVERY (every Nth frame's
-- screen:pixels(), after a u32 frame number and u16 width and height) come
-- from the environment. Script lines: "frame port hex", 1 for pressed
-- (in0: 0x01 coin, 0x08 start 1; in1: 0x01 left, 0x02 right, 0x04 up,
-- 0x08 down, 0x10 fire), held from that frame on.
if not _G.tp_dump then
  _G.tp_dump = true
  local script = os.getenv("TP_SCRIPT")
  local dumpname = os.getenv("TP_DUMP")
  local pixname = os.getenv("TP_PIX")
  if pixname == "" then pixname = nil end
  local pixevery = tonumber(os.getenv("TP_PIXEVERY") or "0")
  local frames = tonumber(os.getenv("TP_FRAMES") or "600")
  local ports = manager.machine.ioport.ports
  local function fields_of(port, masks)
    local t = {}
    for _, f in pairs(ports[port].fields) do
      for _, m in ipairs(masks) do if f.mask == m then t[m] = f end end
    end
    for _, m in ipairs(masks) do assert(t[m], "no field " .. port .. " " .. m) end
    return t
  end
  local fields = { in0 = fields_of(":IN0", { 0x01, 0x08, 0x10 }),
                   in1 = fields_of(":IN1", { 0x01, 0x02, 0x04, 0x08, 0x10 }) }
  local events = {}
  if script then
    for line in io.lines(script) do
      local f, port, v = line:match("^(%d+)%s+(%a+%d)%s+(%x+)")
      if f then
        f = tonumber(f)
        events[f] = events[f] or {}
        table.insert(events[f], { port, tonumber(v, 16) })
      end
    end
  end
  local main = manager.machine.devices[":maincpu"]
  local snd = manager.machine.devices[":timeplt_audio:tpsound"]
  local mp = main.spaces["program"]
  local sp = snd.spaces["program"]
  local out = dumpname and assert(io.open(dumpname, "wb"))
  local pix = pixname and assert(io.open(pixname, "wb"))
  local screen = manager.machine.screens[":screen"]
  local r16 = { "PC", "SP", "AF", "BC", "DE", "HL", "IX", "IY", "AF2", "BC2", "DE2", "HL2", "WZ" }
  local r8 = { "R", "I", "IM", "IFF1", "IFF2", "HALT" }
  local n = 0
  local function regs(cpu, t)
    for _, r in ipairs(r16) do t[#t + 1] = string.pack("<I2", cpu.state[r].value) end
    for _, r in ipairs(r8) do t[#t + 1] = string.pack("B", cpu.state[r].value) end
  end
  _G.tp_sub = emu.register_frame_done(function()
    n = n + 1
    if pix and pixevery > 0 and n % pixevery == 0 then
      local px, w, h = screen:pixels()
      pix:write(string.pack("<I4I2I2", n, w or 0, h or 0), px)
    end
    for _, e in ipairs(events[n] or {}) do
      for mask, f in pairs(fields[e[1]]) do f:set_value((e[2] & mask) ~= 0 and 1 or 0) end
    end
    if out then
      local t = { "TPFR", string.pack("<I4", n), mp:read_range(0xa000, 0xafff, 8),
                  mp:read_range(0xb000, 0xb0ff, 8), mp:read_range(0xb400, 0xb4ff, 8) }
      regs(main, t)
      regs(snd, t)
      t[#t + 1] = sp:read_range(0x3000, 0x33ff, 8)
      out:write(table.concat(t))
    end
    if n >= frames then
      if out then out:close() end
      if pix then pix:close() end
      manager.machine:exit()
    end
  end)
end
