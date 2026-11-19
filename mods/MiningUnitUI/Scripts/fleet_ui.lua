-- Fleet menu page (Fleet tab = page 6, Role tab = page 7). Fleet-specific layout and wiring only.
-- Generic helpers: ui_core.lua. Layout/names: fleet-ui-widget-spec.md. Every command calls a named stub in fleet_stubs.lua.
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local UI = dofile(SRC .. "ui_core.lua")
local M = {}
local api, FS
local S
local TAB_FLEET, TAB_ROLE = 6, 7
local CLEAR = { R = 0, G = 0, B = 0, A = 0 }
local GOLD = "#C38800"
-- role colours from spec section 0 (override fleet_stubs ROLES colours)
local ROLE_COL = { Mine = "42DA41", Haul = "5FD0E6", Escort = "EE66BB", Defend = "FFAA55", Attack = "FF6666", None = "A0A0A0" }
local CLASSES = { "All", "Miner", "Hauler", "Fighter", "Frigate", "Capital" }
local CARD_ROLES = { "Defend", "Haul", "Attack", "Escort" }
local FIELD_PREFIX = { Defend = "D", Haul = "H", Attack = "A", Escort = "E" }

-- every find() name used in this file (spec sections 2-4). scripts/*.py must generate each one.
M.NAMES = {
  "Pnl_FleetLeft", "Sel_FlGroup", "Txt_SelFlGroup", "Btn_FlEmergencyStop", "Btn_FlStart", "Btn_FlHold",
  "Btn_FlDock", "Btn_FlUndock", "Btn_FlRefuel", "Btn_FlRecallL",
  "Pnl_PageFleet", "Txt_FlMainTitle", "Btn_FlSubOverview", "Btn_FlSubEscort", "Pnl_FlSubSwitcher",
  "Btn_FlClsAll", "Btn_FlClsMiner", "Btn_FlClsHauler", "Btn_FlClsFighter", "Btn_FlClsFrigate", "Btn_FlClsCapital",
  "Txt_FlClsCntAll", "Txt_FlClsCntMiner", "Txt_FlClsCntHauler", "Txt_FlClsCntFighter", "Txt_FlClsCntFrigate", "Txt_FlClsCntCapital",
  "Bdr_FlClsSelAll", "Bdr_FlClsSelMiner", "Bdr_FlClsSelHauler", "Bdr_FlClsSelFighter", "Bdr_FlClsSelFrigate", "Bdr_FlClsSelCapital",
  "Bdr_FlSelBar", "Txt_FlSelCount", "Sel_FlRole", "Txt_SelFlRole", "Btn_FlApply", "Btn_FlNewGroup",
  "Dyn_FlList", "Pnl_FlEmpty", "Btn_FlSelect", "Btn_FlRecall", "Btn_FlHold2", "Btn_FlDockBtn", "Btn_FlSend", "Btn_FlUnselectAll",
  "Fg_Root", "Btn_FgHdr", "Txt_FgName", "Txt_FgSummary", "Fr_RowFrame", "Bdr_FrSel", "Btn_FrRow", "Chk_FrSel", "Img_FrHolo",
  "Txt_FrName", "Bdr_FrChip", "Img_FrRoleIcon", "Txt_FrRole", "Txt_FrDoing", "PB_FrHull", "Txt_FrHullPct", "PB_FrHull_Cell",
  "PB_FrCargo", "Txt_FrCargoPct", "Txt_FrNoCargo", "Btn_FrRemove", "Btn_FrGoto", "Txt_FrLostHint",
  "Pnl_FlEscort", "Txt_FlFollowName", "Sel_FlFollow", "Txt_SelFlFollow", "Sel_FlDist", "Txt_SelFlDist", "Btn_FlStopAll",
  "Dyn_FlTargets", "Txt_FlAssignedTitle", "Dyn_FlAssigned", "Dyn_FlFree", "Btn_FlESelect", "Btn_FlEDock", "Btn_FlERecall",
  "Btn_FlEHold", "Btn_FlESend", "Btn_FlEUnselect",
  "Img_FeHolo", "Txt_FeName", "Bdr_FeChip", "Img_FeRoleIcon", "Txt_FeRole", "Txt_FeDoing", "PB_FeHull", "Txt_FeHullPct",
  "Txt_FeDist", "Btn_FePin", "Btn_FeAction", "Img_FtHolo", "Txt_FtName", "Btn_FtPick",
  "Pnl_FleetStatus", "Img_FlStatHolo", "Txt_FlStatName", "Bdr_FlStatChip", "Img_FlStatRoleIcon", "Txt_FlStatRole",
  "Txt_FlStatAction", "Img_FlStatState", "Txt_FlStatNext", "Txt_FlStatSpeed", "Txt_FlStatCargoPct", "PB_FlStatCargo",
  "Txt_FlStatHullPct", "PB_FlStatHull", "Txt_FlStatShieldPct", "PB_FlStatShield", "Btn_FlSetRole", "Btn_FlRecallS", "Btn_FlHoldS",
  "Pnl_PageRole", "Pnl_RlDefend", "Pnl_RlHaul", "Pnl_RlAttack", "Pnl_RlEscort", "Btn_RlDefendRecall", "Btn_RlDefendHold",
  "Btn_RlDefendDefault", "Btn_RlHaulRecall", "Btn_RlHaulHold", "Btn_RlHaulDefault", "Btn_RlAttackRecall", "Btn_RlAttackHold",
  "Btn_RlAttackDefault", "Btn_RlEscortRecall", "Btn_RlEscortHold", "Btn_RlEscortDefault", "Btn_RlHAddRow",
  "Bdr_RlDefendChip", "Img_RlDefendRoleIcon", "Txt_RlDefendRole", "Txt_RlDefendStateTxt", "Txt_RlDefendDoing", "Img_RlDefendHolo",
  "Bdr_RlHaulChip", "Img_RlHaulRoleIcon", "Txt_RlHaulRole", "Txt_RlHaulStateTxt", "Txt_RlHaulDoing", "Img_RlHaulHolo",
  "Bdr_RlAttackChip", "Img_RlAttackRoleIcon", "Txt_RlAttackRole", "Txt_RlAttackStateTxt", "Txt_RlAttackDoing", "Img_RlAttackHolo",
  "Bdr_RlEscortChip", "Img_RlEscortRoleIcon", "Txt_RlEscortRole", "Txt_RlEscortStateTxt", "Txt_RlEscortDoing", "Img_RlEscortHolo",
  "Sel_RlDAnchor", "Edt_RlDRadius", "Edt_RlDGiveUp", "Edt_RlDFlee", "Sel_RlDPick", "Chk_RlDReturn",
  "Sel_RlHFrom", "Sel_RlHTo", "Sel_RlHItem", "Edt_RlHAmount", "Chk_RlHRepeat",
  "Sel_RlASearch", "Sel_RlAPick", "Edt_RlAFlee", "Sel_RlAIdle",
  "Sel_RlEFollow", "Sel_RlESlot", "Edt_RlEDist", "Chk_RlEWarp",
  "Btn_TabFleet", "Btn_TabRole", "Btn_FlOpenShip",
}

-- role-page fields (one table drives the four cards). k = sel|edit|check, key = stub arg name, num = number box, def = default
local FIELDS = {
  Defend = {
    { n = "Sel_RlDAnchor", k = "sel", key = "anchor", title = "Anchor" },
    { n = "Edt_RlDRadius", k = "edit", key = "R", num = true, def = 15 },
    { n = "Edt_RlDGiveUp", k = "edit", key = "giveup", num = true, def = 25 },
    { n = "Edt_RlDFlee", k = "edit", key = "flee", num = true, def = 30 },
    { n = "Sel_RlDPick", k = "sel", key = "pick", title = "Pick target", def = "Closest first" },
    { n = "Chk_RlDReturn", k = "check", key = "return" },
  },
  Haul = {
    { n = "Sel_RlHFrom", k = "sel", key = "from", title = "Load at" },
    { n = "Sel_RlHTo", k = "sel", key = "to", title = "Unload at" },
    { n = "Sel_RlHItem", k = "sel", key = "item", title = "Item" },
    { n = "Edt_RlHAmount", k = "edit", key = "amount", num = true, def = 0 },
    { n = "Chk_RlHRepeat", k = "check", key = "repeat" },
  },
  Attack = {
    { n = "Sel_RlASearch", k = "sel", key = "search", title = "Search area" },
    { n = "Sel_RlAPick", k = "sel", key = "pick", title = "Pick target" },
    { n = "Edt_RlAFlee", k = "edit", key = "flee", num = true, def = 30 },
    { n = "Sel_RlAIdle", k = "sel", key = "idle", title = "When no enemies" },
  },
  Escort = {
    { n = "Sel_RlEFollow", k = "sel", key = "follow", title = "Follow" },
    { n = "Sel_RlESlot", k = "sel", key = "slot", title = "Slot" },
    { n = "Edt_RlEDist", k = "edit", key = "dist", num = true, def = 300 },
    { n = "Chk_RlEWarp", k = "check", key = "warp" },
  },
}

local function fresh()
  return { sel = {}, fold = {}, rv = {}, rowsF = {}, rowsA = {}, rowsC = {}, rowsT = {}, icons = {},
    entF = setmetatable({}, { __mode = "k" }), hid = nil, follow = nil, dist = nil,
    page = nil, sub = 0, cls = "All", roleSel = nil, etarget = nil, sig = nil, tAt = 0 }
end
S = fresh()

local function hullColour(p) return (p and p < 30) and "FFCC33" or "6AA8FF" end
local function setBrush(o, hex) local c = UI.col(hex); if c and api.ok(o) then pcall(function() o:SetBrushColor(c) end) end end
local function brushTex(o, tex) if api.ok(o) and tex then pcall(function() o:SetBrushFromTexture(tex, true) end) end end
local function pct(v) return v and (tostring(v) .. "%") or "-" end

-- ship list from the stub provider; without fleet data, every bench ship is listed with role None
local function shipsNow()
  local st = FS.get_state() or {}
  local src = st.ships or {}
  if not FS.FLEET_DEMO and #src == 0 then
    src = {}
    for _, o in ipairs(api.benchShips()) do src[#src + 1] = { id = o.id, name = o.text, role = "None", status_text = "Idle" } end
  end
  local out = {}
  for _, s in ipairs(src) do
    local id = tostring(s.id)
    out[#out + 1] = { id = id, name = s.name or id, role = s.role or "None", status = s.status_text or "",
      cargo = s.cargo, hull = s.hull, shield = s.shield, class = s.class, sel = S.sel[id] == true }
  end
  return out
end

local function selIds()
  local ids = {}
  for _, s in ipairs(shipsNow()) do if s.sel then ids[#ids + 1] = s.id end end
  return ids
end

local function firstSel(ships)
  for _, s in ipairs(ships) do if s.sel then return s end end
end

local function roleOpts()
  local o = {}
  for _, r in ipairs(FS.ROLES) do o[#o + 1] = { id = r.name, text = r.name } end
  return o
end

-- ===== Fleet overview list: group header entries (Fg_Root shown) + ship entries (Fr_RowFrame shown) =====
local function paintRow(r, f)
  local hdr = r.k == "hdr"
  api.setVis(f("Fg_Root"), hdr and 0 or 1)
  api.setVis(f("Fr_RowFrame"), hdr and 1 or 0)
  if hdr then
    UI.text(f("Txt_FgName"), string.format("%s (%d)", r.name, r.count))
    UI.text(f("Txt_FgSummary"), "")
    return
  end
  local s = r.item
  UI.text(f("Txt_FrName"), s.name)
  UI.text(f("Txt_FrDoing"), s.status)
  UI.chip(f, "Bdr_FrChip", "Img_FrRoleIcon", "Txt_FrRole", s.role, ROLE_COL[s.role] or "A0A0A0")
  api.setVis(f("PB_FrHull_Cell"), s.hull and 0 or 1)
  UI.bar(f("PB_FrHull"), (s.hull or 0) / 100, hullColour(s.hull))
  UI.text(f("Txt_FrHullPct"), pct(s.hull))
  api.setVis(f("PB_FrCargo"), s.cargo and 0 or 1)
  api.setVis(f("Txt_FrNoCargo"), s.cargo and 1 or 0)
  UI.bar(f("PB_FrCargo"), (s.cargo or 0) / 100, "42DA41")
  UI.text(f("Txt_FrCargoPct"), pct(s.cargo))
  brushTex(f("Img_FrHolo"), S.icons[s.id])
  UI.check_field(f, "Chk_FrSel", s.sel)
  setBrush(f("Fr_RowFrame"), s.sel and "33291A" or "2B2B2B")
  api.setVis(f("Bdr_FrSel"), s.sel and 0 or 1)
  api.setVis(f("Btn_FrRemove"), 1)
  api.setVis(f("Btn_FrGoto"), 1)
  api.setVis(f("Txt_FrLostHint"), 1)
end

local function toggleSel(id)
  local on = not S.sel[id]
  S.sel[id] = on or nil
  FS.select_ship(id, on)
end

local function bindFleetEntry(e, f, idx)
  UI.bind_button(f, "Btn_FgHdr", function()
    local r = S.rowsF[idx()]
    if r and r.k == "hdr" then UI.fold(S.fold, r.key); FS.toggle_group(r.key); M.repaint() end
  end)
  S.entF[f] = idx
  UI.bind_button(f, "Btn_FrRow", function() local r = S.rowsF[idx()]; if r and r.item then toggleSel(r.item.id); M.repaint() end end)
  UI.bind_button(f, "Btn_FrGoto", function() local r = S.rowsF[idx()]; if r and r.item then FS.select_ship(r.item.id, true) end end)
  UI.bind_button(f, "Btn_FrRemove", function() local r = S.rowsF[idx()]; if r and r.item then FS.set_role(r.item.id, "None") end end)
end

-- ===== Escort page lists (inside Pnl_PageFleet, sub-switcher index 1) =====
local function paintEsc(r, f)
  local s = r.item
  UI.text(f("Txt_FeName"), s.name)
  UI.chip(f, "Bdr_FeChip", "Img_FeRoleIcon", "Txt_FeRole", s.role, ROLE_COL[s.role] or "A0A0A0")
  UI.text(f("Txt_FeDoing"), s.status)
  UI.text(f("Txt_FeDist"), "-")
  UI.bar(f("PB_FeHull"), (s.hull or 0) / 100, hullColour(s.hull))
  UI.text(f("Txt_FeHullPct"), pct(s.hull))
  brushTex(f("Img_FeHolo"), S.icons[s.id])
end

local function bindEsc(assigned, key)
  return function(e, f, idx)
    UI.bind_button(f, "Btn_FePin", function() local r = S[key][idx()]; if r then FS.select_ship(r.item.id, true) end end)
    UI.bind_button(f, "Btn_FeAction", function()
      local r = S[key][idx()]
      if not r then return end
      if assigned then FS.escort_remove(r.item.id) else FS.escort_add(r.item.id, S.etarget) end
      M.repaint()
    end)
  end
end

local function paintTarget(r, f)
  UI.text(f("Txt_FtName"), r.item.name)
  brushTex(f("Img_FtHolo"), S.icons[r.item.id])
end

local function bindTarget(e, f, idx)
  UI.bind_button(f, "Btn_FtPick", function()
    local r = S.rowsT[idx()]
    if r then S.etarget = r.item.id; FS.escort_set(r.item.id); M.repaint() end
  end)
end

local function groupsOf(ships)
  return api.groupList(ships, function(s) return s.role end, function(k) return k end, S.fold)
end

local function items(list)
  local out = {}
  for _, s in ipairs(list) do out[#out + 1] = { k = "item", item = s } end
  return out
end

-- ===== status box (Pnl_FleetStatus) for the first selected ship =====
local function statusPaint(s)
  local p = UI.index("Pnl_FleetStatus")
  UI.text(p("Txt_FlStatName"), s and s.name or "-")
  UI.chip(p, "Bdr_FlStatChip", "Img_FlStatRoleIcon", "Txt_FlStatRole", s and s.role or "-", ROLE_COL[s and s.role or ""] or "A0A0A0")
  UI.text(p("Txt_FlStatAction"), s and s.status or "")
  UI.text(p("Txt_FlStatNext"), "")
  UI.text(p("Txt_FlStatSpeed"), "")
  UI.text(p("Txt_FlStatCargoPct"), pct(s and s.cargo))
  UI.bar(p("PB_FlStatCargo"), s and (s.cargo or 0) / 100 or 0, "42DA41")
  UI.text(p("Txt_FlStatHullPct"), pct(s and s.hull))
  UI.bar(p("PB_FlStatHull"), s and (s.hull or 0) / 100 or 0, "6AA8FF")
  UI.text(p("Txt_FlStatShieldPct"), pct(s and s.shield))
  UI.bar(p("PB_FlStatShield"), s and (s.shield or 0) / 100 or 0, "6AA8FF")
  brushTex(p("Img_FlStatHolo"), s and S.icons[s.id])
end

local function repaintFleet()
  local ships = shipsNow()
  S.icons = api.shipIcons() or {}
  local pf = UI.index("Pnl_PageFleet")
  S.rowsF = groupsOf(ships)
  UI.list{ key = "fleet", anchor = "Pnl_PageFleet", box = "Dyn_FlList", rows = S.rowsF, rowFn = paintRow, bindFn = bindFleetEntry }

  local nSel = 0
  local assigned, free = {}, {}
  for _, s in ipairs(ships) do
    if s.sel then nSel = nSel + 1 end
    if s.role == "Escort" then assigned[#assigned + 1] = s else free[#free + 1] = s end
  end
  S.rowsA, S.rowsC = items(assigned), items(free)
  S.rowsT = items(ships)
  UI.list{ key = "escA", anchor = "Pnl_PageFleet", box = "Dyn_FlAssigned", rows = S.rowsA, rowFn = paintEsc, bindFn = bindEsc(true, "rowsA") }
  UI.list{ key = "escC", anchor = "Pnl_PageFleet", box = "Dyn_FlFree", rows = S.rowsC, rowFn = paintEsc, bindFn = bindEsc(false, "rowsC") }
  UI.list{ key = "escT", anchor = "Pnl_PageFleet", box = "Dyn_FlTargets", rows = S.rowsT, rowFn = paintTarget, bindFn = bindTarget }
  UI.text(pf("Txt_FlAssignedTitle"), string.format("Escorting you - %d ships", #assigned))

  UI.text(pf("Txt_FlMainTitle"), string.format("Fleet Control - %d selected", nSel))
  UI.text(pf("Txt_FlSelCount"), string.format("%d selected", nSel))
  api.setVis(pf("Bdr_FlSelBar"), nSel > 0 and 0 or 1)
  api.setVis(pf("Pnl_FlEmpty"), #ships == 0 and 0 or 1)
  -- class strip: "All" = every ship; other classes count ships whose state carries ship.class, else 0
  local cc = {}
  for _, s in ipairs(ships) do if s.class then cc[s.class] = (cc[s.class] or 0) + 1 end end
  for _, k in ipairs(CLASSES) do
    UI.text(pf("Txt_FlClsCnt" .. k), tostring(k == "All" and #ships or cc[k] or 0))
    api.setVis(pf("Bdr_FlClsSel" .. k), S.cls == k and 0 or 1)
  end
  UI.sel_field(pf, "Sel_FlRole", { title = "Role", options = roleOpts, get = function() return S.roleSel end,
    display = function(v) return v or "-" end, set = function(v) S.roleSel = v; FS.pick_role("role") end })
  UI.sel_field(pf, "Sel_FlFollow", { title = "Follow", get = function() return S.follow end,
    display = function(v) return v or "You (Captain)" end,
    options = function()
      local o = { { id = "You (Captain)", text = "You (Captain)" } }
      for _, s in ipairs(ships) do o[#o + 1] = { id = s.name, text = s.name } end
      return o
    end,
    repaint = function() M.repaint() end,
    set = function(v) S.follow = v; FS.escort_set(selIds(), { follow = v, dist = S.dist }) end })
  UI.sel_field(pf, "Sel_FlDist", { title = "Distance", get = function() return S.dist end,
    display = function(v) return v or "Medium, 500 m" end,
    options = function()
      return { { id = "Close, 200 m", text = "Close, 200 m" }, { id = "Medium, 500 m", text = "Medium, 500 m" }, { id = "Far, 1000 m", text = "Far, 1000 m" } }
    end,
    repaint = function() M.repaint() end,
    set = function(v) S.dist = v; FS.escort_set(selIds(), { follow = S.follow, dist = v }) end })
  statusPaint(firstSel(ships))
  local pl = UI.index("Pnl_FleetLeft")
  UI.text(pl("Txt_SelFlGroup"), "All ships")
end

local function repaintRole()
  local pr = UI.index("Pnl_PageRole")
  local ships = shipsNow()
  local first = firstSel(ships)
  local show = first and FIELD_PREFIX[first.role] and first.role or nil
  for _, role in ipairs(CARD_ROLES) do
    api.setVis(pr("Pnl_Rl" .. role), (not show or show == role) and 0 or 1)
    if first then
      UI.chip(pr, "Bdr_Rl" .. role .. "Chip", "Img_Rl" .. role .. "RoleIcon", "Txt_Rl" .. role .. "Role", first.role, ROLE_COL[first.role] or "A0A0A0")
      UI.text(pr("Txt_Rl" .. role .. "StateTxt"), first.status)
      UI.text(pr("Txt_Rl" .. role .. "Doing"), first.status)
      brushTex(pr("Img_Rl" .. role .. "Holo"), S.icons[first.id])
    end
    local rv = S.rv[role] or {}
    S.rv[role] = rv
    for _, f in ipairs(FIELDS[role]) do
      if f.k == "sel" then
        UI.sel_field(pr, f.n, { title = f.title, options = function() return {} end,
          get = function() return rv[f.key] or f.def end,
          set = function(v) rv[f.key] = v; FS.pick_role(f.key) end })
      elseif f.k == "edit" then
        UI.edit_field(pr, f.n, rv[f.key] or f.def)
      else
        UI.check_field(pr, f.n, rv[f.key] or false)
      end
    end
  end
end

-- values of one role page, read from the boxes (only when api.reading())
local function collect(role)
  local pr = UI.index("Pnl_PageRole")
  local rv = S.rv[role] or {}
  local vals = {}
  for _, f in ipairs(FIELDS[role]) do
    local v
    if f.k == "edit" then v = UI.read_edit(pr, f.n, f.num)
    elseif f.k == "check" then v = UI.read_check(pr, f.n) end
    if v == nil then v = rv[f.key] or f.def end
    vals[f.key] = v
  end
  return vals
end

local function selectAll(ids)
  for _, id in ipairs(ids) do FS.select_ship(id, true) end
end

local function sub(n)
  S.sub = n
  local sw = UI.index("Pnl_PageFleet")("Pnl_FlSubSwitcher")
  if api.ok(sw) then pcall(function() sw:SetActiveWidgetIndex(n) end) end
end

local function pickRoleModal(ids, title)
  FS.pick_role("role")
  api.modalSelect{ title = title, options = roleOpts(), id = nil, onPick = function(id) FS.set_role(ids, id) end }
end

-- ===== bindings (once per window open; the widgets are new each open) =====
function M.bind()
  local pf = UI.index("Pnl_PageFleet")
  local pl = UI.index("Pnl_FleetLeft")
  local ps = UI.index("Pnl_FleetStatus")
  local pr = UI.index("Pnl_PageRole")
  local function cmd(find, n, fn) UI.bind_button(find, n, function() fn(selIds()) end) end
  -- left column
  cmd(pl, "Btn_FlStart", FS.start)
  cmd(pl, "Btn_FlHold", FS.hold)
  cmd(pl, "Btn_FlEmergencyStop", FS.emergency_stop)
  cmd(pl, "Btn_FlDock", FS.dock)
  cmd(pl, "Btn_FlUndock", FS.undock)
  cmd(pl, "Btn_FlRefuel", FS.refuel)
  cmd(pl, "Btn_FlRecallL", FS.recall)
  UI.bind_button(pl, "Sel_FlGroup", function() FS.set_filter("group") end)
  -- fleet page: sub-tabs, class strip, selection bar, action bar
  UI.bind_button(pf, "Btn_FlSubOverview", function() sub(0) end)
  UI.bind_button(pf, "Btn_FlSubEscort", function() sub(1) end)
  for _, k in ipairs(CLASSES) do
    UI.bind_button(pf, "Btn_FlCls" .. k, function() S.cls = k; FS.set_filter(k); M.repaint() end)
  end
  UI.bind_button(pf, "Btn_FlApply", function() if S.roleSel then FS.set_role(selIds(), S.roleSel) end end)
  UI.bind_button(pf, "Btn_FlNewGroup", function() FS.new_group(selIds()) end)
  cmd(pf, "Btn_FlSelect", selectAll)
  cmd(pf, "Btn_FlRecall", FS.recall)
  cmd(pf, "Btn_FlHold2", FS.hold)
  cmd(pf, "Btn_FlDockBtn", FS.dock)
  cmd(pf, "Btn_FlSend", selectAll)
  UI.bind_button(pf, "Btn_FlUnselectAll", function() S.sel = {}; FS.unselect_all(); M.repaint() end)
  -- escort page (inside Pnl_PageFleet)
  UI.bind_button(pf, "Btn_FlStopAll", function()
    for _, r in ipairs(S.rowsA) do FS.escort_remove(r.item.id) end
  end)
  cmd(pf, "Btn_FlESelect", selectAll)
  cmd(pf, "Btn_FlEDock", FS.dock)
  cmd(pf, "Btn_FlERecall", FS.recall)
  cmd(pf, "Btn_FlEHold", FS.hold)
  cmd(pf, "Btn_FlESend", selectAll)
  UI.bind_button(pf, "Btn_FlEUnselect", function() S.sel = {}; FS.unselect_all(); M.repaint() end)
  UI.bind_button(pf, "Btn_FlOpenShip", function() api.setTab(1) end)
  -- status box
  UI.bind_button(ps, "Btn_FlSetRole", function() pickRoleModal(selIds(), "Choose role for selected") end)
  cmd(ps, "Btn_FlRecallS", FS.recall)
  cmd(ps, "Btn_FlHoldS", FS.hold)
  -- role page: default buttons send the whole form (defend_set etc.), per-card recall/hold
  for _, role in ipairs(CARD_ROLES) do
    cmd(pr, "Btn_Rl" .. role .. "Recall", FS.recall)
    cmd(pr, "Btn_Rl" .. role .. "Hold", FS.hold)
  end
  cmd(pr, "Btn_RlDefendDefault", function(ids) FS.defend_set(ids, collect("Defend")) end)
  cmd(pr, "Btn_RlHaulDefault", function(ids) FS.haul_set(ids, collect("Haul")) end)
  cmd(pr, "Btn_RlAttackDefault", function(ids) FS.attack_set(ids, collect("Attack")) end)
  cmd(pr, "Btn_RlEscortDefault", function(ids) FS.escort_set(ids, collect("Escort")) end)
  cmd(pr, "Btn_RlHAddRow", function(ids) FS.haul_set(ids, { add_row = true }) end)
end

-- Tab switch (called for every setTab; i = 1..7). Pages 6 and 7 show the fleet left column and status box.
-- mining windows hidden on Fleet/Role tabs (Pnl_Fleet = Fleet Control; Row_Win<key> = window() roots); previous visibility restored after
local MINING_WIN = { "Pnl_Fleet", "RowWinControl", "RowWinActions", "RowWinStatus" }
local function winName(n) return (n:gsub("RowWin", "Row_Win")) end
local function hideMining(hide)
  if hide and not S.hid then
    S.hid = {}
    for _, n in ipairs(MINING_WIN) do
      local o = api.w(winName(n))
      if api.ok(o) then
        local s, v = pcall(function() return o:GetVisibility() end)
        S.hid[n] = s and v or 0
        api.setVis(o, 1)
      end
    end
  elseif not hide and S.hid then
    for n, v in pairs(S.hid) do
      local o = api.w(winName(n))
      if api.ok(o) then pcall(function() o:SetVisibility(v) end) end
    end
    S.hid = nil
  end
end

function M.onTab(i)
  local on = (i == TAB_FLEET or i == TAB_ROLE)
  api.setVis(api.w("Pnl_FleetLeft"), on and 0 or 1)
  api.setVis(api.w("Pnl_FleetStatus"), on and 0 or 1)
  hideMining(on)
  S.page = on and i or nil
  S.sig = nil
  if i == TAB_FLEET then repaintFleet() elseif i == TAB_ROLE then repaintRole() end
end

-- repaint the page currently shown (after a click)
function M.repaint()
  if S.page == TAB_FLEET then repaintFleet() elseif S.page == TAB_ROLE then repaintRole() end
end

function M.onClose()
  hideMining(false)
  S = fresh()
  UI.reset()
end

-- checkboxes have no click event: poll them (page visible only) and call the stub on change
local CHK_SETTER = { Defend = "defend_set", Haul = "haul_set", Attack = "attack_set", Escort = "escort_set" }
local function pollChecks()
  if S.page == TAB_FLEET then
    local flips = {}
    for f, idx in pairs(S.entF) do
      local r = S.rowsF[idx()]
      local c = r and r.item and UI.read_check(f, "Chk_FrSel")
      if c ~= nil and c ~= (r.item.sel == true) then flips[#flips + 1] = r.item.id end
    end
    for _, id in ipairs(flips) do toggleSel(id) end
    if #flips > 0 then M.repaint() end
  elseif S.page == TAB_ROLE then
    local pr = UI.index("Pnl_PageRole")
    local ids = selIds()
    for _, role in ipairs(CARD_ROLES) do
      local rv = S.rv[role] or {}
      S.rv[role] = rv
      for _, f in ipairs(FIELDS[role]) do
        if f.k == "check" then
          local c = UI.read_check(pr, f.n)
          if c ~= nil and c ~= (rv[f.key] or false) then
            rv[f.key] = c
            FS[CHK_SETTER[role]](ids, { [f.key] = c })
          end
        end
      end
    end
  end
end

-- called from the refresh loop: repaints only when the ship list text changed (page 6 or 7 open)
function M.tick()
  if not S.page then return end
  if os.clock() - S.tAt < 1 then return end
  S.tAt = os.clock()
  pollChecks()
  local ships = shipsNow()
  local parts = { tostring(S.page) }
  for _, s in ipairs(ships) do
    parts[#parts + 1] = s.id .. s.role .. s.status .. tostring(s.cargo) .. tostring(s.hull) .. tostring(s.sel)
  end
  local sig = table.concat(parts, ";")
  if sig ~= S.sig then
    S.sig = sig
    M.repaint()
  end
end

function M.init(a)
  api = a
  UI.init(a)
  FS = dofile(SRC .. "fleet_stubs.lua")
  local pal = { Gold = GOLD }
  for k, v in pairs(ROLE_COL) do pal[k] = v end
  UI.palette(pal)
end

return M
