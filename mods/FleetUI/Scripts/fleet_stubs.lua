-- MiningUnitUI fleet stubs: UI-only placeholders. Every command is a no-op that logs one line.
-- Real fleet logic comes later; keep the names/signatures stable for the window code.

local fleet = {}
local TAG = "[MiningUnitUI] "

fleet.FLEET_DEMO = false

fleet.ROLES = {
  { name = "Mine",   key = "mine",   colour = "#42DA41" },
  { name = "Haul",   key = "haul",   colour = "#66CCDD" },
  { name = "Escort", key = "escort", colour = "#EE66BB" },
  { name = "Defend", key = "defend", colour = "#FFAA66" },
  { name = "Attack", key = "attack", colour = "#FF6666" },
  { name = "None",   key = "none",   colour = "#999999" },
}

-- shallow table dump, nil -> "nil", never errors
local function fmt(v)
  if v == nil then return "nil" end
  if type(v) ~= "table" then return tostring(v) end
  local parts = {}
  for k, x in pairs(v) do
    parts[#parts + 1] = tostring(k) .. "=" .. (type(x) == "table" and "{...}" or tostring(x))
  end
  return "{" .. table.concat(parts, ",") .. "}"
end

local function stub(name, ...)
  local args = table.pack(...)
  local ok, s = pcall(function()
    local t = { "FLEET STUB", name }
    for i = 1, args.n do t[#t + 1] = fmt(args[i]) end
    return table.concat(t, " ")
  end)
  print(TAG .. (ok and s or ("FLEET STUB " .. tostring(name) .. " <log error>")) .. "\n")
end

function fleet.set_role(...) stub("set_role", ...) end
function fleet.recall(...) stub("recall", ...) end
function fleet.hold(...) stub("hold", ...) end
function fleet.start(...) stub("start", ...) end
function fleet.new_group(...) stub("new_group", ...) end
function fleet.escort_add(...) stub("escort_add", ...) end
function fleet.escort_remove(...) stub("escort_remove", ...) end
function fleet.defend_set(...) stub("defend_set", ...) end
function fleet.haul_set(...) stub("haul_set", ...) end
function fleet.attack_set(...) stub("attack_set", ...) end
function fleet.escort_set(...) stub("escort_set", ...) end
function fleet.dock(...) stub("dock", ...) end
function fleet.undock(...) stub("undock", ...) end
function fleet.refuel(...) stub("refuel", ...) end
function fleet.select_ship(...) stub("select_ship", ...) end
function fleet.unselect_all(...) stub("unselect_all", ...) end
function fleet.emergency_stop(...) stub("emergency_stop", ...) end
function fleet.set_filter(...) stub("set_filter", ...) end
function fleet.toggle_group(...) stub("toggle_group", ...) end
function fleet.pick_role(...) stub("pick_role", ...) end

local function demo_groups()
  return {
    { name = "Miners", count = 3 },
    { name = "Haulers", count = 1 },
    { name = "Guards", count = 2 },
    { name = "Unassigned", count = 1 },
  }
end

local function demo_ships()
  return {
    { id = 1, name = "Medium Miner", role = "Mine", status_text = "Mining", cargo = 40, selected = false },
    { id = 2, name = "Miner 2", role = "Mine", status_text = "Mining", cargo = 75, selected = false },
    { id = 3, name = "Miner 3", role = "Mine", status_text = "Mining", cargo = 10, selected = false },
    { id = 4, name = "Hauler 1", role = "Haul", status_text = "Hauling", cargo = 60, selected = false },
    { id = 5, name = "Corvette A", role = "Escort", status_text = "Following you", hull = 100, shield = 80, selected = false },
    { id = 6, name = "Corvette B", role = "Defend", status_text = "Engaging enemy", hull = 65, shield = 30, selected = false },
    { id = 7, name = "Unassigned 1", role = "None", status_text = "Idle", hull = 100, shield = 100, selected = false },
  }
end

function fleet.get_state()
  if not fleet.FLEET_DEMO then return { groups = {}, ships = {} } end
  return { groups = demo_groups(), ships = demo_ships() }
end

return fleet
