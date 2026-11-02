-- MiningUnitUI: Mining Unit window (WBP_MiningUnit via wbp_loader.lua). F4 = fleet window, F / ESC = close.
-- Block interact (BPO_MiningMod_C) is redirected here from the game's DMSPlayerController:ToggleWidget.
-- Every game call goes through pcall. Every log line gets TAG.

local TAG = "[MiningUnitUI] "
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this script's dir (Mods/MiningUnitUI/Scripts/)
local W = dofile((SRC or "ue4ss/Mods/MiningUnitUI/Scripts/") .. "wbp_loader.lua")

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
local function onClick(btn, fn)
    BTN[btn:GetFullName()] = fn
    btn.OnClicked:Add(btn, FName("ForceLayoutPrepass"))
end
pcall(RegisterHook, "/Script/UMG.Widget:ForceLayoutPrepass", function(self)
    local okn, key = pcall(function() return self:get():GetFullName() end)
    local fn = okn and BTN[key]
    if fn then pcall(ExecuteInGameThread, function() pcall(fn) end) end
end)
W.bind({ onClick = onClick })

-- block interact: the game opens its own widget for BPO_MiningMod_C; hide it and open ours for that ship
RegisterHook("/Script/MainSequence.DMSPlayerController:ToggleWidget", function() end, function(self, a1, a2, a3, a4, a5)
    pcall(function()
      local ctl = unw(self)
      local args = { unw(a1), unw(a2), unw(a3), unw(a4), unw(a5) }
      -- arg order is not trusted: find each object by its class. Own Mining Unit actor: widget is null, machine still found.
      local widget, machine
      for i = 1, 5 do
        local x = args[i]
        if ok(x) then
          local c, n = cn(x), nm(x)
          if not widget and c:sub(-2) == "_C" and c ~= "WidgetBlueprintGeneratedClass" and n:find("^WBP_") then widget = x end
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

print(TAG .. "loaded. F4 = fleet window, block interact = Mining Unit window\n")
