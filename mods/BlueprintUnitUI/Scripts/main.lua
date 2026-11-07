-- BlueprintUnitUI: Blueprint Unit window (WBP_BlueprintUnit via bpu_window.lua). F / ESC = close, F9 = dev cost test.
-- Opened by post-hooks on WidgetSubsystem:ToggleWindow, WidgetSubsystem:ShowWindow and DMSPlayerController:ToggleWidget
-- (block arg BPO_BlueprintUnit_C). Every game call goes through pcall. Every log line gets TAG.

local TAG = "[BPUnitUI] "
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this script's dir (Mods/BlueprintUnitUI/Scripts/)
local BPU = dofile((SRC or "ue4ss/Mods/BlueprintUnitUI/Scripts/") .. "bpu_window.lua")

local function ok(o) if o == nil or type(o) ~= "userdata" then return false end local s, v = pcall(function() return o:IsValid() end) return s and v == true end
local function nm(o) if not ok(o) then return "?" end local s, v = pcall(function() return o:GetFName():ToString() end) return s and v or "?" end
local function cn(o) if not ok(o) then return "?" end local s, v = pcall(function() return o:GetClass():GetFName():ToString() end) return s and v or "?" end
local function unw(x) if type(x) == "userdata" then local s, r = pcall(function() return x:get() end) if s and r ~= nil then return r end end return x end

-- button mechanism (copied from MiningUnitUI main.lua): ForceLayoutPrepass hook, registered only while the window has buttons
local BTN = {}
-- WHY: the ForceLayoutPrepass hook fires for EVERY widget in the game every frame; keep it registered only while our window has buttons (perf lesson: constant FPS cost + BTN grew forever).
local HOOK = "/Script/UMG.Widget:ForceLayoutPrepass"
-- hookIds = registered pre/post ids (at most one registration at a time).
-- hookOff = removal scheduled by close. Removal never runs inside the hook callback: clearClicks arms a delayed
-- timer, and the timer hands the UnregisterHook call to the game-thread queue. A reopen before it fires cancels it.
local hookIds, hookOff, hookOpens = nil, nil, 0
local function onPrepass(self)
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
    if hookIds and not hookOff then
        hookOff = true
        pcall(ExecuteWithDelay, 100, function() pcall(ExecuteInGameThread, hookRemove) end)
    end
end
BPU.bind({ onClick = onClick, clear = clearClicks })

-- vanilla WBP_StationInventory: BP_BlueprintUnit's import is renamed (clone.js), so it should not open. Belt and braces:
-- close any instance that still shows. Scheduled after our open; a missing class just gives no instances.
local function closeVanilla()
  local s, list = pcall(FindAllOf, "WBP_StationInventory_C")
  if not s or type(list) ~= "table" then return end
  for _, o in ipairs(list) do
    if ok(o) then
      local okr = pcall(function() o:RemoveFromParent() end)
      print(TAG .. "closed vanilla station window ok=" .. tostring(okr) .. "\n")
    end
  end
end

-- opener: any of the three hooks; the block arg decides (a1 = class, may be nil/invalid: never used as the filter). First call per hook prints every arg class (probe P1).
local seen = {}
local function onOpen(src, self, a1, a2, a3, a4, a5)
  pcall(function()
    local args = { unw(a1), unw(a2), unw(a3), unw(a4), unw(a5) }
    if not seen[src] then
      seen[src] = true
      local parts = {}
      for i = 1, 5 do local x = args[i]; parts[#parts + 1] = "a" .. i .. "=" .. (ok(x) and cn(x) or type(x)) end
      print(TAG .. "opener args (" .. src .. "): " .. table.concat(parts, " ") .. "\n")
    end
    local block, cont, player, widget
    for i = 1, 5 do
      local x = args[i]
      if ok(x) then
        local c = cn(x)
        if not block and c == "BPO_BlueprintUnit_C" then block = x end
        if not cont and c == "BP_BPUnitStoreObj_01_C" then cont = x end
        if not player and c == "DMSPlayerObject" then player = x end
        if not widget and c:sub(-2) == "_C" and c ~= "WidgetBlueprintGeneratedClass" and nm(x):find("^WBP_") then widget = x end
      end
    end
    local b = block or cont
    if not ok(b) then return end
    local ctl = (src == "DMSPlayerController:ToggleWidget") and unw(self) or nil
    local wGame = widget
    print(TAG .. "open request via " .. src .. " block=" .. cn(b) .. "\n")
    ExecuteInGameThread(function()
      -- game's own widget for the block (ToggleWidget only): hide it, as the Mining Unit does
      if ok(wGame) and ok(ctl) then pcall(function() ctl:HideWidget(wGame, false) end) end
      BPU.open(b, ok(player) and player or nil, ctl)
      pcall(ExecuteWithDelay, 100, function() pcall(ExecuteInGameThread, closeVanilla) end)
    end)
  end)
end

local function hookOne(path, src)
  local s, pre, post = pcall(RegisterHook, path, function() end, function(self, a1, a2, a3, a4, a5) onOpen(src, self, a1, a2, a3, a4, a5) end)
  print(TAG .. "hook " .. src .. " " .. (s and ("registered ids=" .. tostring(pre) .. "," .. tostring(post)) or "FAILED") .. "\n")
end
hookOne("/Script/MainSequence.WidgetSubsystem:ToggleWindow", "WidgetSubsystem:ToggleWindow")
hookOne("/Script/MainSequence.WidgetSubsystem:ShowWindow", "WidgetSubsystem:ShowWindow")
hookOne("/Script/MainSequence.DMSPlayerController:ToggleWidget", "DMSPlayerController:ToggleWidget")

pcall(function()
  RegisterKeyBind(Key.ESCAPE, function()
    if BPU.isOpen() then pcall(ExecuteInGameThread, BPU.close) end
  end)
end)
pcall(function()
  RegisterKeyBind(Key.F, function()
    if BPU.isOpen() then pcall(ExecuteInGameThread, BPU.fKey) end
  end)
end)
-- DEV only: fills the cost rows with 3 fake items (setCost test)
pcall(function()
  RegisterKeyBind(Key.F9, function()
    if BPU.isOpen() then pcall(ExecuteInGameThread, BPU.devCost) end
  end)
end)

print(TAG .. "loaded. opener hooks on block interact, F / ESC close, F9 dev cost\n")
