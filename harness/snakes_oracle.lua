-- snakes_oracle.lua — trace Snakes (6r45_1.app, UID3 0x101fd3db) in EKA2L1.
-- IDA addresses work directly (EKA2L1 resolves vs the image codeseg; no rebase).
local common = require('eka2l1.common')
local evt    = require('eka2l1.events')
local cpu    = require('eka2l1.cpu')
local mem    = require('eka2l1.mem')

local IMG  = '6r45_1.app'
local UID3 = 0x101fd3db

local function regs(tag)
  common.log(string.format('[ORACLE] %s r0=%08X r1=%08X r2=%08X r3=%08X sp=%08X lr=%08X',
    tag, cpu.getReg(0), cpu.getReg(1), cpu.getReg(2), cpu.getReg(3), cpu.getSp(), cpu.getLr()))
end

-- mem.readDword returns a Lua double; %08X throws for values >= 2^31. Format via halves.
local function hex(v)
  v = v % 4294967296
  return string.format('%04X%04X', math.floor(v / 65536) % 65536, v % 65536)
end
local function rd(addr)
  local ok, v = pcall(mem.readDword, addr)
  if ok then return v end
  return 0
end

local function dumpObj(tag, base, words)
  if base == 0 then common.log('[ORACLE] '..tag..' = NULL'); return end
  common.log(string.format('[ORACLE] %s @%08X (%d words):', tag, base, words))
  local i = 0
  while i < words do
    local s = string.format('   +%03X:', i*4)
    for j = 0, 7 do
      if i+j < words then s = s .. ' ' .. hex(rd(base + (i+j)*4)) end
    end
    common.log(s)
    i = i + 8
  end
end

local hits = {}
local function once(key) if hits[key] then return false end hits[key] = true return true end

-- boot litmus + capture appui layout
evt.registerBreakpointHook(IMG, 0x1008A560, 0, UID3, function()
  regs('ConstructL@1008A560')
  if once('appui') then
    local base = cpu.getReg(0)
    common.log('[PROBE] step1: about to readDword @'..string.format('%08X', base))
    local ok, v = pcall(mem.readDword, base)
    common.log('[PROBE] step2: pcall ok='..tostring(ok)..' v='..tostring(v))
    common.log('[PROBE] step3: type(v)='..type(v))
    dumpObj('appui(this)@entry', base, 40)
  end
end)

-- the data-init functions the recomp got a1=0 on (soft-guarded). Capture REAL args.
evt.registerBreakpointHook(IMG, 0x1000EF1C, 0, UID3, function()
  if once('ef1c') then regs('sub_1000EF1C') ; dumpObj('1000EF1C a1', cpu.getReg(0), 12) end
end)
evt.registerBreakpointHook(IMG, 0x10050364, 0, UID3, function()
  if once('364') then regs('sub_10050364') ; dumpObj('10050364 a1', cpu.getReg(0), 12) end
end)
evt.registerBreakpointHook(IMG, 0x100507B4, 0, UID3, function()
  if once('7b4') then regs('sub_100507B4 (recomp had a1=0!)') ; dumpObj('100507B4 a1', cpu.getReg(0), 12) end
end)

-- window-server setup: where the recomp's fake WS objects diverge from reality.
evt.registerBreakpointHook(IMG, 0x10023930, 0, UID3, function()
  if once('23930') then regs('sub_10023930 (stores WS obj @a1[24])'); dumpObj('23930 a1', cpu.getReg(0), 30) end
end)
evt.registerBreakpointHook(IMG, 0x10023DC0, 0, UID3, function()
  if once('23dc0') then regs('sub_10023DC0 (derefs WS obj)'); dumpObj('23DC0 a1', cpu.getReg(0), 30) end
end)

-- dispatcher: log each DISTINCT command once; dump the game-state object on first hit.
local seenCmd = {}
evt.registerBreakpointHook(IMG, 0x1002A830, 0, UID3, function()
  local this = cpu.getReg(0)
  local cmd  = rd(cpu.getReg(1) + 28)
  if not seenCmd[cmd] then
    seenCmd[cmd] = 0
    common.log(string.format('[ORACLE] dispatcher NEW cmd=0x%X this=%08X', cmd, this))
    if once('stateobj') then
      dumpObj('game-state(this)', this, 40)
      -- follow the WS object stored in the window holder (a1[24] seen = 0x00700720)
    end
  end
  seenCmd[cmd] = seenCmd[cmd] + 1
end)
evt.registerBreakpointHook(IMG, 0x10068440, 0, UID3, function() regs('engineFactory@10068440 (GAME START!)') end)

common.log('[ORACLE] snakes_oracle.lua loaded')
