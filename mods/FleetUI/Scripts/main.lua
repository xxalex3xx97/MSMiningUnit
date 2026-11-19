-- FleetUI: standalone fleet window (WBP_FleetWindow via fleet_win.lua). F5 = toggle, ESC / X = close.
-- Independent of MiningUnitUI (F4). Every bind is guarded.
local TAG = "[FleetUI] "
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local okW, W = pcall(dofile, SRC .. "fleet_win.lua")
if not (okW and type(W) == "table") then
  print(TAG .. "ERR load fleet_win: " .. tostring(W) .. "\n")
  return
end

pcall(function() RegisterKeyBind(Key.F5, function() pcall(ExecuteInGameThread, W.toggle) end) end)
pcall(function()
  RegisterKeyBind(Key.ESCAPE, function()
    if W.isOpen() then pcall(ExecuteInGameThread, W.close) end
  end)
end)

print(TAG .. "loaded. F5 = fleet window\n")
