-- oracle-trace.lua — EKA2L1 instrumentation to capture ground truth for the recomp.
--
-- Drop this in <EKA2L1>/scripts/ (auto-loaded). It logs to EKA2L1's console/log.
-- Three kinds of hook, all usable together:
--
--   1. IPC hooks — capture what the game sends to a server (window server, fbs,
--      file server). Load-address independent. THE rendering ground truth.
--   2. Breakpoint hooks — break at one of the game's OWN functions and dump
--      registers/memory. Address must be REBASED to EKA2L1's load address:
--         rebased = eka2l1_load_base + (ida_addr - 0x10000000)
--      Find the load base from a breakpoint at the entry, or EKA2L1's codeseg log.
--   3. Library hooks — break when a named Symbian export (by ordinal) is called.
--
-- Snakes (engine 6r45_1.app, UID3 0x101fd3db). Key functions (IDA, base 0x10000000):
--   command dispatcher  sub_1002A830   (game-start command 0x43E)
--   engine factory       sub_10068440 / sub_1006845C   (newL 864)
--   game-start           sub_10068E60
--   periodic tick        sub_100697A8 -> sub_10069618 (state machine @ engine+24)
--   control vtable: OfferKeyEventL=slot3, Draw=slot26

local common = require('eka2l1.common')
local evt    = require('eka2l1.events')
local cpu    = require('eka2l1.cpu')
local mem    = require('eka2l1.mem')

local IMAGE   = '6r45_1.app'
local UID3    = 0x101fd3db
local IDABASE = 0x10000000
local LOADBASE = nil               -- set once known (see findBase below)

local function rebase(idaAddr)
  if not LOADBASE then return idaAddr end
  return LOADBASE + (idaAddr - IDABASE)
end

-- 1) Window-server IPC: log every command the game sends to the screen.
--    opcode -1 / nil semantics vary by build; register a few key opcodes or use
--    the broad log-ipc config flag for a first pass, then narrow here.
evt.registerIpcHook('!Windowserver', 0x00, evt.EVENT_IPC_SEND, function(ctx)
  common.log(string.format('[WS] op=%d a0=%08X a1=%08X a2=%08X a3=%08X',
    0, ctx:rawArgument(0), ctx:rawArgument(1), ctx:rawArgument(2), ctx:rawArgument(3)))
end)

-- 2) Breakpoint on the engine factory: dump the new engine object + its layout.
--    Enable once LOADBASE is known.
local function onEngineCreated()
  local r0 = cpu.getReg(0)                       -- returned engine object (on exit hook)
  common.log(string.format('[ENGINE] sub_10068440 hit, r0=%08X sp=%08X lr=%08X',
    r0, cpu.getSp(), cpu.getLr()))
end

-- 3) Breakpoint on the command dispatcher: log every command id the game processes
--    (so we learn the real menu->play command, vs our guessed 0x43E).
local function onCommand()
  local appui = cpu.getReg(0)
  local evObj = cpu.getReg(1)
  local cmd   = mem.readDword(evObj + 28)        -- *(a2+28) = command id
  common.log(string.format('[CMD] sub_1002A830 cmd=0x%X appui=%08X', cmd, appui))
end

-- Register the game-function breakpoints (no-ops until LOADBASE is set).
if LOADBASE then
  evt.registerBreakpointHook(IMAGE, rebase(0x10068440), 0, UID3, onEngineCreated)
  evt.registerBreakpointHook(IMAGE, rebase(0x1002A830), 0, UID3, onCommand)
end

common.log('oracle-trace.lua loaded for ' .. IMAGE)
common.log('NOTE: set LOADBASE (from codeseg load log) to enable game-function hooks.')
