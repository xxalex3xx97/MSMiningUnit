-- MiningUnitUI: Mining Unit window (WBP_MiningUnit via wbp_loader.lua). F4 = fleet window, F / ESC = close.
-- Block interact (BPO_MiningMod_C) is redirected here from the game's DMSPlayerController:ToggleWidget.
-- Every game call goes through pcall. Every log line gets TAG.

local TAG = "[MiningUnitUI] "
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this script's dir (Mods/MiningUnitUI/Scripts/)
local W = dofile((SRC or "ue4ss/Mods/MiningUnitUI/Scripts/") .. "wbp_loader.lua")
-- window names live in MSCore (mscore_wbp widgets contract). Old/missing MSCore: those features are off, nothing crashes.
local SRCF = ((SRC or ""):gsub("\\", "/"))
package.path = SRCF .. "../../MSCore/Scripts/?.lua;" .. package.path
local okW, WBP = pcall(require, "mscore_wbp")
if not (okW and type(WBP) == "table" and type(WBP.widget) == "function") then
  WBP = nil
  print(TAG .. "MSCore too old (needs 0.8.9): fleet ship icons and window-name filter are off\n")
end

local function ok(o) if o == nil or type(o) ~= "userdata" then return false end local s, v = pcall(function() return o:IsValid() end) return s and v == true end
local function nm(o) if not ok(o) then return "?" end local s, v = pcall(function() return o:GetFName():ToString() end) return s and v or "?" end
local function cn(o) if not ok(o) then return "?" end local s, v = pcall(function() return o:GetClass():GetFName():ToString() end) return s and v or "?" end
local function unw(x) if type(x) == "userdata" then local s, r = pcall(function() return x:get() end) if s and r ~= nil then return r end end return x end
local function rd(o, p) local s, v = pcall(function() return o[p] end) if s then return v end return nil end
local function ships() return FindAllOf("DMSPlayerShipObject") or {} end
local function shipOf(o2)
  local ids = {}; ids[rd(o2, "ID") or -1] = true; ids[rd(o2, "ParentID") or -1] = true
  for _, sh in ipairs(ships()) do
    for _, prop in ipairs({ "Subobjects", "BuildingBlocks" }) do
      local arr = rd(sh, prop)
      if arr then local s, n = pcall(function() return arr:GetArrayNum() end)
        if s then for i = 1, n do local e = arr[i]; if ok(e) and ids[rd(e, "ID") or -2] then return sh end end end end
    end
  end
end

-- wbp_loader needs only {onClick}: the button mechanism (ForceLayoutPrepass hook) lives here
local BTN = {}
-- WHY: the ForceLayoutPrepass hook fires for EVERY widget in the game every frame; keep it registered only while our window has buttons (perf lesson: constant FPS cost + BTN grew forever).
local HOOK = "/Script/UMG.Widget:ForceLayoutPrepass"
-- hookIds = registered pre/post ids (at most one registration at a time).
-- hookOff = removal scheduled by close. Removal never runs inside the hook callback: clearClicks arms a delayed
-- timer, and the timer hands the UnregisterHook call to the game-thread queue. A reopen before it fires cancels it
-- (the hook is still registered, so no second RegisterHook is made).
local hookIds, hookOff, hookOpens = nil, nil, 0
local function onPrepass(self)
    if next(BTN) == nil then return end   -- no window buttons: leave at once (the hook stays registered for the session)
    local okn, key = pcall(function() return self:get():GetFullName() end)
    local fn = okn and BTN[key]
    if fn then pcall(ExecuteInGameThread, function() pcall(fn) end) end
end
local function hookRemove()
    if hookOff and hookIds then
        local ids = hookIds
        hookIds, hookOff = nil, nil
        local s = pcall(UnregisterHook, HOOK, ids[1], ids[2])
        print(TAG .. "prepass hook removed ids=" .. ids[1] .. "," .. ids[2] .. " ok=" .. tostring(s) .. "\n")
    end
    hookOff = nil
end
local function onClick(btn, fn)
    if not hookIds then
        local s, pre, post = pcall(RegisterHook, HOOK, onPrepass)
        if s then hookIds = { pre, post }; hookOpens = hookOpens + 1; print(TAG .. "prepass hook registered ids=" .. pre .. "," .. post .. " n=" .. hookOpens .. "\n") end
    end
    hookOff = nil   -- window in use again: keep the hook registered
    BTN[btn:GetFullName()] = fn
    btn.OnClicked:Add(btn, FName("ForceLayoutPrepass"))
end
local function clearClicks()
    BTN = {}
    -- WHY no UnregisterHook: registering and removing a hook on the same game function from several Lua states (this mod and the dev menu) crashed the game
    -- (UE4SS access violation in a later tick, seen 2026-11-19). The hook stays registered once made; onPrepass returns at once while BTN is empty.
end
W.bind({ onClick = onClick, clear = clearClicks, wbp = WBP })

-- block interact: the game opens its own widget for BPO_MiningMod_C; hide it and open ours for that ship
-- a renamed/removed game function must not kill the keybinds below: log one line and go on (ponytail: no MSCore.hook here, this mod has no MSCore Lua state)
local function safeHook(name, path, pre, post)
  local okh, err = pcall(RegisterHook, path, pre, post)
  if not okh then print(TAG .. "hook " .. name .. " off: " .. tostring(err) .. "\n") end
  return okh
end
safeHook("block_interact", "/Script/MainSequence.DMSPlayerController:ToggleWidget", function() end, function(self, a1, a2, a3, a4, a5)
    pcall(function()
      local ctl = unw(self)
      local args = { unw(a1), unw(a2), unw(a3), unw(a4), unw(a5) }
      -- arg order is not trusted: find each object by its class. Own Mining Unit actor: widget is null, machine still found.
      local widget, machine
      for i = 1, 5 do
        local x = args[i]
        if ok(x) then
          local c = cn(x)
          if not widget and WBP and WBP.is_window(x) then widget = x end
          if not machine and c:find("^BPO_") then machine = x end
        end
      end
      if not (ok(machine) and cn(machine) == "BPO_MiningMod_C") then return end
      local sh = shipOf(machine)
      if not sh then
        local pid = rd(machine, "ParentID")
        for _, s2 in ipairs(ships()) do
          if rd(s2, "ID") == pid then sh = s2; break end
        end
      end
      if not sh then print(TAG .. "no ship link for " .. nm(machine) .. "\n") return end
      local shipv, w0 = sh, widget
      ExecuteInGameThread(function()
        if ok(w0) and ok(ctl) then pcall(function() ctl:HideWidget(w0, false) end) end
        W.open(shipv)
      end)
    end)
end)

pcall(function() RegisterKeyBind(Key.F4, function() W.open(nil) end) end)
pcall(function()
  RegisterKeyBind(Key.ESCAPE, function()
    if W.isOpen() then pcall(ExecuteInGameThread, W.close) end
  end)
end)
pcall(function()
  RegisterKeyBind(Key.F, function()
    if W.isOpen() then pcall(ExecuteInGameThread, W.fKey) end
  end)
end)

-- [AUTO-FILTER] applier. MiningBenchMod C++ writes DockFilter.txt "<dock id>|<ITEM_x>" (docks tagged [AUTO-FILTER] + an ore, never [MISC]).
-- Each (dock, item) pair is applied ONCE via MSCore factory_native.set_dock_filter(id, slot, item, 0) on every slot (overrides the old filter); a changed ore applies again.
-- ponytail: the slot count is unknown here (BPO_Dock 2, BPO_Dock_2 8): tries slots 0..7 and stops at the first refusal. Ask MSCore for the slot count and drop the cap.
local DF = { done = {}, logged = {}, mscore = nil, warned = false }
local DF_SLOTS = 8
local function dfApply()
  local fh = io.open(W.DATA .. "DockFilter.txt", "r")
  if not fh then return end
  local want = {}
  for line in fh:lines() do
    local id, item = line:match("^%s*(%d+)|(%S+)%s*$")
    if id then want[id] = item end
  end
  fh:close()
  for id in pairs(DF.done) do if not want[id] then DF.done[id] = nil end end
  local pending = false
  for id, item in pairs(want) do if DF.done[id] ~= item then pending = true end end
  if not pending then return end
  if DF.mscore == nil then
    local okM, MS = pcall(require, "mscore")
    DF.mscore = (okM and type(MS) == "table") and MS or false
  end
  local FN = DF.mscore and DF.mscore.factory_native
  if not (FN and FN.set_dock_filter) then
    if not DF.warned then DF.warned = true; print(TAG .. "MSCore too old (needs 0.8.8): [AUTO-FILTER] docks are not filtered\n") end
    return
  end
  for id, item in pairs(want) do
    if DF.done[id] ~= item then
      local n, why = 0, nil
      for ix = 0, DF_SLOTS - 1 do
        local okc, r, err = pcall(FN.set_dock_filter, tonumber(id), ix, item, 0)
        if okc and r then n = n + 1 else why = tostring(okc and err or r); break end
      end
      if n > 0 then DF.done[id] = item end
      local key = id .. "|" .. item .. "|" .. n
      if not DF.logged[key] then DF.logged[key] = true; print(TAG .. "auto-filter dock=" .. id .. " item=" .. item .. " slots set=" .. n .. (why and (" stopped: " .. why) or "") .. "\n") end
    end
  end
end
pcall(LoopAsync, 5000, function() pcall(ExecuteInGameThread, function() pcall(dfApply) end) return false end)

print(TAG .. "loaded. F4 = fleet window, block interact = Mining Unit window\n")
