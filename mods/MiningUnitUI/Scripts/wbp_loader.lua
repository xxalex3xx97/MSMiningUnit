-- Mining Unit WBP loader (UE4SS Lua). API: bind / open(ship) / close / isOpen / fKey / setTab.
-- Window = the WBP_MiningUnit instance itself, added to viewport, 70% of viewport, centred (no SizeBox wrapper).
-- Loaded by MiningUnitUI main.lua via dofile. Class load is lazy, retried on each open.
-- Forward slashes only in paths (backslash "\U" breaks the Lua load).
local M = {}

local function L(m) print("[WBP] " .. m .. "\n") end
local function ok(o) if o == nil or type(o) ~= "userdata" then return false end local s, v = pcall(function() return o:IsValid() end) return s and v == true end
local function rd(o, p) local s, v = pcall(function() return o[p] end) if s then return v end return nil end
local function inVp(o) local s, v = pcall(function() return o:IsInViewport() end) return s and v == true end
local function try(tag, f, ...) local s, r = pcall(f, ...) if not s then L("FAIL " .. tag .. ": " .. tostring(r)) end return s end
local USE_SLOT_SETTERS = false
local function nm(o, cls) if not ok(o) then return "invalid" end local s, r = pcall(function() if cls then return o:GetClass():GetFName():ToString() end return o:GetFName():ToString() end) return s and tostring(r) or "?" end
local function v2(v) if type(v) ~= "userdata" then return tostring(v) end return tostring(rd(v, "X")) .. "x" .. tostring(rd(v, "Y")) end

local LIB = "/Script/UMG.Default__WidgetBlueprintLibrary"
local LIB_LAYOUT = "/Script/UMG.Default__WidgetLayoutLibrary"
local WBP_CLS = "/Game/MiningUnitUI/WBP_MiningUnit.WBP_MiningUnit_C"  -- StaticFindObject takes the class path
local Z = 49                                           -- below dev menu 50
local TABS = { "Btn_TabShip", "Btn_TabItems", "Btn_TabOperation", "Btn_TabLog" }  -- PageSwitcher index 0..3
-- linear colors = gen_paste.py lin(): #D49A00 gold (active), #1E1E1E .95 (off), #1A1A1A text on active, white off
local TAB_ON = { R = 0.658375, G = 0.323143, B = 0.0, A = 1.0 }
local TAB_OFF = { R = 0.012983, G = 0.012983, B = 0.012983, A = 0.95 }
local TXT_ON = { R = 0.010330, G = 0.010330, B = 0.010330, A = 1.0 }
local TXT_OFF = { R = 1.0, G = 1.0, B = 1.0, A = 1.0 }
local EST_RED_TXT = { R = 1.0, G = 0.25, B = 0.2, A = 1.0 }   -- Txt_StatusAction tint on an ERROR status
local UDN = 20 -- max docks per unload-ore list and per refuel list (= gen_paste UDOCK_N)
-- ore key k = widget part (Pnl_UDockCard_<k>, modal key UDock_<k>_<n>); ini = INI key part (unload_docks_<ini>, ore_<ini>)
local UD_ORES = { { k = "Cobalt", ini = "cobalt" }, { k = "Graphite", ini = "graphite" }, { k = "Iridium", ini = "iridium" },
  { k = "Nickel", ini = "nickel" }, { k = "Silica", ini = "silica" }, { k = "Titanium", ini = "titanium" }, { k = "WaterIce", ini = "waterice" } }
-- every name Lua touches; missing ones are logged after CreateWidget
local NAMES = { "Panel", "PageSwitcher", "Page_Ship", "Page_Items", "Page_Operation", "Txt_Title", "Pnl_Fleet", "Sel_Remote", "Pnl_Modal", "Btn_ModalOutside", "Btn_ModalClose",
  "Txt_ModalTitle", "Edt_ModalSearch", "Pnl_ModalColHead", "Btn_Close", "Btn_ModalDone",
  "Pnl_ModalHeadG", "Pnl_ModalHeadP", "Txt_ModalTitleG", "Btn_ModalCloseG", "Row_ModalCardScroll", "Row_ModalScroll",
  "Btn_TabShip", "Btn_TabItems", "Btn_TabOperation", "Btn_LoopStart", "Btn_LoopStop",
  "Txt_StatusAction", "PB_Cargo", "Txt_CargoPct", "Txt_Speed", "Txt_NextState", "Txt_Status",
  "Txt_Eta", "Btn_EmergencyStop", "Txt_EmergencyStop", "Btn_Dock", "Btn_Undock", "Btn_Refuel", "Btn_Unload", "Btn_Collect", "Txt_Collect", "Btn_RemoteAll", "Pnl_AllWarn", "Txt_AllWarn",
  "Txt_TabLog", "Pnl_PageLog", "Txt_LogHint", "Row_LogScroll", "Row_StatusMine", "Txt_StatusMine", "Img_StatusOre", "Pnl_LogAll", "Row_LogAllScroll" }
-- Mining line of every card (Card/Fleet): label + ore icon + ore name; log card pool (Pnl_LogCard_<f>, LOGCARD_ROWS lines each)
for i = 1, 20 do
  for _, P in ipairs({ "Card", "Fleet" }) do
    NAMES[#NAMES + 1] = "Txt_" .. P .. "MineLbl_" .. i; NAMES[#NAMES + 1] = "Img_" .. P .. "Ore_" .. i
  end
  NAMES[#NAMES + 1] = "Pnl_LogCard_" .. i; NAMES[#NAMES + 1] = "Txt_LogCardName_" .. i; NAMES[#NAMES + 1] = "Txt_LogCardStatus_" .. i
  for r = 1, 10 do NAMES[#NAMES + 1] = "Txt_LogCard_" .. i .. "_" .. r end
end
-- modal row ore icons; literal 40 = MN (MN is defined below, at modal section). Row_* layout widgets must be variables to be found.
for i = 1, 40 do
  NAMES[#NAMES + 1] = "Row_ModalOres_" .. i
  for k = 1, 6 do NAMES[#NAMES + 1] = "Img_ModalOre_" .. i .. "_" .. k end
end
-- Ship tab dock lists: per ore card + rows (select = Sel_UDock_<k>_<n>, trash) + add; refuel list; warning
for _, u in ipairs(UD_ORES) do
  NAMES[#NAMES + 1] = "Img_UDockOre_" .. u.k; NAMES[#NAMES + 1] = "Pnl_UDockCard_" .. u.k; NAMES[#NAMES + 1] = "Btn_UDockAdd_" .. u.k
  for n = 1, UDN do NAMES[#NAMES + 1] = "Pnl_UDockRow_" .. u.k .. "_" .. n; NAMES[#NAMES + 1] = "Btn_UDockDel_" .. u.k .. "_" .. n end
end
for n = 1, UDN do NAMES[#NAMES + 1] = "Pnl_RDockRow_" .. n; NAMES[#NAMES + 1] = "Btn_RDockDel_" .. n end
for _, f in ipairs({ "Deuterium", "Hydrogen", "Warp" }) do NAMES[#NAMES + 1] = "Img_RDockFuel" .. f end
NAMES[#NAMES + 1] = "Pnl_RDockCard"; NAMES[#NAMES + 1] = "Btn_RDockAdd"; NAMES[#NAMES + 1] = "Txt_ShipWarn"

-- S.w: WBP_MiningUnit instance = the viewport widget (names resolve here). S.root = same object. S.p: controller
local S = { root = nil, w = nil, p = nil, cls = nil, ship = nil, sid = nil, gen = 0, seq = 0, tick = 0 }
local BIND -- {onClick} from main.lua
function M.bind(api) BIND = api end
-- GetText on an edit box right after our SetText / tab or ship switch crashed UE4SS (FText access violation). Skip edit reads for 0.6 s after those.
local function holdReads() S.noRead = os.clock() + 0.6 end
local function reading() return os.clock() >= (S.noRead or 0) end

-- LoadAsset is registry-gated (and null on 5.7 here). BPModLoader method: FAssetData table -> AssetRegistryHelpers:GetAsset (game thread).
local ARH
local function getAsset(pkg, name)
  if not ok(ARH) then ARH = StaticFindObject("/Script/AssetRegistry.Default__AssetRegistryHelpers") end
  if not ok(ARH) then L("ARH invalid"); return nil end
  local sf, r = pcall(function() return ARH:GetAsset({ PackageName = FName(pkg), AssetName = FName(name) }) end)
  L("GetAsset " .. pkg .. "." .. name .. " ok=" .. tostring(sf) .. " v=" .. tostring(r) .. " valid=" .. tostring(sf and ok(r)))
  if sf and ok(r) then return r end
end

function M.load()
  local c = StaticFindObject(WBP_CLS)
  if not ok(c) then c = getAsset("/Game/MiningUnitUI/WBP_MiningUnit", "WBP_MiningUnit_C") or StaticFindObject(WBP_CLS) end
  if not ok(c) then L("WBP class NOT FOUND " .. WBP_CLS); return false end
  S.cls = c
  L("WBP class found " .. WBP_CLS)
  return true
end

-- viewport in slate units: WidgetLayoutLibrary:GetViewportSize(p) returns FVector2D (pixels), divided by GetViewportScale
local function viewport(p)
  local vw, vh = nil, nil
  local okv, v = pcall(function() return StaticFindObject(LIB_LAYOUT):GetViewportSize(p) end)
  if okv and type(v) == "userdata" then
    local x, y = rd(v, "X"), rd(v, "Y")
    if type(x) == "number" and type(y) == "number" and x > 0 and y > 0 then vw, vh = x, y end
  end
  L("GetViewportSize ok=" .. tostring(okv) .. " type(v)=" .. type(v) .. " -> " .. tostring(vw) .. "x" .. tostring(vh))
  local sc = 1
  local oks, sv = pcall(function() return StaticFindObject(LIB_LAYOUT):GetViewportScale(p) end)
  if oks and type(sv) == "number" and sv > 0 then sc = sv end
  if not vw then vw, vh = 3440, 1440; L("viewport fallback 3440x1440") end
  L("viewport px " .. tostring(vw) .. "x" .. tostring(vh) .. " scale=" .. tostring(sc))
  return vw / sc, vh / sc
end

-- widget by name: GetWidgetFromName first, then the is-variable UProperty on the instance
local function w(name)
  if not ok(S.w) then return nil end
  local s, x = pcall(function() return S.w:GetWidgetFromName(FName(name)) end)
  if s and ok(x) then return x end
  local s2, y = pcall(function() return S.w[name] end)
  if s2 and ok(y) then return y end
  return nil
end

-- walk the real widget tree from RootWidget (GetChildrenCount/GetChildAt are UFUNCTIONs on panels)
local function walk(o, depth, out)
  if depth > 6 or #out >= 60 then return end
  out[#out + 1] = string.rep(" ", depth) .. nm(o) .. " [" .. nm(o, true) .. "]"
  local s, c = pcall(function() return o:GetChildrenCount() end)
  if s and type(c) == "number" then
    for i = 0, c - 1 do
      local s2, ch = pcall(function() return o:GetChildAt(i) end)
      if s2 and ok(ch) then walk(ch, depth + 1, out) end
    end
  end
end

local function dumpTree()
  L("instance " .. nm(S.w) .. " [" .. nm(S.w, true) .. "] class=" .. nm(S.w, true))
  local tree = rd(S.w, "WidgetTree")
  L("WidgetTree valid=" .. tostring(ok(tree)))
  if not ok(tree) then return end
  local root = rd(tree, "RootWidget")
  L("RootWidget valid=" .. tostring(ok(root)))
  if not ok(root) then return end
  local out = {}
  walk(root, 0, out)
  L("tree (" .. #out .. " shown):")
  for _, line in ipairs(out) do L(line) end
end

-- SetText(FText) on the real TextBlock; fallback .Text property; log widget class if both fail
function M.setText(name, s)
  -- WHY: unchanged writes invalidate layout every refresh; S.tc (name -> last text) is dropped on close.
  S.tc = S.tc or {}
  if S.tc[name] == s then return end
  local tb = w(name); if not tb then return end
  if try("SetText " .. name, function() tb:SetText(FText(s)) end) then S.tc[name] = s; return end
  if try("Text " .. name, function() tb.Text = FText(s) end) then S.tc[name] = s; return end
  L("setText " .. name .. " both failed, class=" .. nm(tb, true))
end

-- Button label: the pak still says "Warp to belt"; set the button's text child (TextBlock) at runtime instead of a pak rebuild
function M.setBtnText(name, s)
  local b = w(name); if not ok(b) then return end
  local s1, c = pcall(function() return b:GetChildAt(0) end)
  if not (s1 and ok(c)) then L("setBtnText " .. name .. " no child"); return end
  if not nm(c, true):find("TextBlock") then L("setBtnText " .. name .. " child is " .. nm(c, true)); return end
  try("setBtnText " .. name, function() c:SetText(FText(s)) end)
end

local function setPercent(name, v)
  local pb = w(name); if pb then try("SetPercent " .. name, function() pb:SetPercent(v) end) end
end

-- MiningBench.ini / MiningCmd.txt / MiningStatus.txt / MiningStatusX.txt live under this dir (main.lua MD)
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this script's dir (Mods/MiningUnitUI/Scripts/)
M.DATA = SRC and (SRC .. "../../MiningBenchMod/dlls/") or "ue4ss/Mods/MiningBenchMod/dlls/"

-- ===== Control + Status (M2) =====
-- UMG linear = (sRGB channel / 255) ^ 2.2
local function lin(c) return (c / 255) ^ 2.2 end
local function rgb(r, g, b) return { R = lin(r), G = lin(g), B = lin(b), A = 1.0 } end
local BTN_GREEN = rgb(0x42, 0xDA, 0x41)  -- Start button when loop running
local BTN_RED = rgb(0x9C, 0x22, 0x22)    -- Stop button when loop stopped
local BTN_NORMAL = rgb(0x4A, 0x4A, 0x4A)
local BTN_AMBER = rgb(0xE6, 0x9A, 0x00)
local WARN_YEL = rgb(0xF2, 0xD2, 0x1E)     -- Txt_ShipWarn + Txt_StatusAction tint when a loaded ore has no unload dock
local ACTION = { mining = "Mining", traveling = "Docking", refueling = "Refueling", unloading = "Unloading", collecting = "Collecting ore", idle = "Idle", waiting = "Waiting for dock", error = "ERROR" }

local function rdlines(path)
  local fh = io.open(path, "r")
  if not fh then return nil end
  local out = {}
  for line in fh:lines() do out[#out + 1] = (line:gsub("\r$", "")) end
  fh:close()
  return out
end

-- WHY: refresh() runs every 500 ms and re-read the same status files once per ship; share one read for 0.3 s (perf).
local rcache = {}
local function rdcached(path)
  local c, now = rcache[path], os.clock()
  if c and now - c.t < 0.3 then return c.lines end
  local lines = rdlines(path)
  rcache[path] = { t = now, lines = lines }
  return lines
end

local function split(s)
  local out, i = {}, 1
  while true do
    local j = string.find(s, "|", i, true)
    if not j then out[#out + 1] = string.sub(s, i); break end
    out[#out + 1] = string.sub(s, i, j - 1)
    i = j + 1
  end
  return out
end

local function trim(s) return (s:match("^%s*(.-)%s*$")) end

-- section-aware INI: section = nil is the top level (global defaults), else "ship.<id>".
-- Only key lines inside the target section are touched; comments/other sections stay as-is.
local function iniHeader(l) return l:match("^%s*%[(.-)%]%s*$") end

-- body of a section: header at s0 (0 = top level), body = s0+1 .. e0-1. nil if section absent.
local function iniRange(lines, section)
    local s0 = 0
    if section then
        s0 = nil
        for i, l in ipairs(lines) do
            if iniHeader(l) == section then s0 = i break end
        end
        if not s0 then return nil end
    end
    local e0 = #lines + 1
    for i = s0 + 1, #lines do
        if iniHeader(lines[i]) then e0 = i break end
    end
    return s0, e0
end

local function iniGet(lines, section)
    local kv = {}
    local s0, e0 = iniRange(lines, section)
    if not s0 then return kv end
    for i = s0 + 1, e0 - 1 do
        local k, v = lines[i]:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
        if k then kv[k] = v end
    end
    return kv
end

-- replace keys in place inside the section; append missing ones after its last non-blank line;
-- create the section at end of file if absent
local function iniPatch(lines, section, vals, keys)
    local s0, e0 = iniRange(lines, section)
    if not s0 then
        if #lines > 0 and trim(lines[#lines]) ~= "" then lines[#lines + 1] = "" end
        lines[#lines + 1] = "[" .. section .. "]"
        s0 = #lines
        e0 = #lines + 1
    end
    local done = {}
    for i = s0 + 1, e0 - 1 do
        local k = lines[i]:match("^%s*([%w_]+)%s*=")
        if k and vals[k] ~= nil then
            lines[i] = k .. "=" .. vals[k]
            done[k] = true
        end
    end
    local ins = s0
    for i = s0 + 1, e0 - 1 do
        if trim(lines[i]) ~= "" then ins = i end
    end
    local add = {}
    for _, k in ipairs(keys) do
        if not done[k] and vals[k] ~= nil then add[#add + 1] = k .. "=" .. vals[k] end
    end
    for j = #add, 1, -1 do table.insert(lines, ins + 1, add[j]) end
    return lines
end

local function fstr(v) if type(v) == "userdata" then local s, r = pcall(function() return v:ToString() end) return s and r or "" end return tostring(v or "") end
-- ===== modal select: ONE system for every select. WBP: Pnl_Modal (Btn_ModalOutside, Txt_ModalTitle, Btn_ModalClose, Edt_ModalSearch, Row_ModalScroll, Opt_Modal_<n> + Img_ModalIcon_<n> + Txt_ModalC1..4_<n>) =====
-- Fields: Sel_<key> button + Txt_Sel<key>. selSet(key, opts, id) fills + paints, selInit(key, bind, onPick, title[, rowRender[, build]]) binds the click.
local MN = 40  -- must equal gen_paste SEL_N; ponytail: more than 40 matches hidden until search narrows
local CN = 40  -- must equal gen_paste CN (card-style select pool / multi-select slots); ponytail: more than 40 matches hidden until search narrows
local FN = 20  -- must equal gen_paste FN; ponytail: >20 ships hidden
local SEL_GOLD, SEL_GREY = rgb(0xC3, 0x88, 0x00), rgb(0xD9, 0xD9, 0xD9)
local SEL_TINT = { R = SEL_GOLD.R, G = SEL_GOLD.G, B = SEL_GOLD.B, A = 0.25 }  -- selected card wash
local CLEAR = { R = 0, G = 0, B = 0, A = 0 }
local function setVis(o, v) if o then try("vis", function() o:SetVisibility(v) end) end end
local function modalClose() holdReads(); S.modal = nil; setVis(w("Pnl_Modal"), 1) end
-- card row i. P = "Card" (modal, Opt_Card_<i>) or "Fleet" (side list, Pnl_Fleet_<i>). r = rowRender(o) = {name,status,skey,cargo(0..100|nil),dist(string|nil),icon,mineIcon,mine,next}. sel = selected (Card only)
local function cardRender(P, i, r, sel)
  M.setText("Txt_" .. P .. "Name_" .. i, r.name or "")
  M.setText("Txt_" .. P .. "Stat_" .. i, r.status or "")
  M.setText("Txt_" .. P .. "Cargo_" .. i, r.cargo and string.format("%.0f%%", r.cargo) or "-")
  setPercent("PB_" .. P .. "_" .. i, (r.cargo or 0) / 100)
  setVis(w("Txt_" .. P .. "Dist_" .. i), r.dist and 0 or 1)
  setVis(w("Img_" .. P .. "Pin_" .. i), r.dist and 0 or 1)
  if r.dist then M.setText("Txt_" .. P .. "Dist_" .. i, r.dist) end
  -- Mining: label + ore icon (hidden when none) + ore name ("-" when none)
  M.setText("Txt_" .. P .. "MineLbl_" .. i, "Mining:")
  M.setText("Txt_" .. P .. "Mine_" .. i, r.mine or "-")
  setVis(w("Txt_" .. P .. "MineLbl_" .. i), 0); setVis(w("Txt_" .. P .. "Mine_" .. i), 0)   -- designer default is Collapsed
  local oi = w("Img_" .. P .. "Ore_" .. i)
  if oi then
    if r.mineIcon then try("card ore", function() oi:SetBrushFromTexture(r.mineIcon, false) end); setVis(oi, 0)
    else setVis(oi, 2) end
  end
  local nx = "Txt_" .. P .. "Next_" .. i
  if r.next then M.setText(nx, r.next) end
  setVis(w(nx), r.next and 0 or 1)
  local im = w("Img_" .. P .. "Holo_" .. i)
  if im then
    if r.icon then try("card holo", function() im:SetBrushFromTexture(r.icon, true) end); setVis(im, 0)
    else setVis(im, 1) end
  end
  -- status glyph: texture read off hidden Img_StatSrc_<key> (no soft-path load)
  local s, t = pcall(function() return w("Img_StatSrc_" .. (r.skey or "idle")).Brush.ResourceObject end)
  if s and ok(t) then try("card stat", function() w("Img_" .. P .. "Stat_" .. i):SetBrushFromTexture(t, false) end) end
  if P == "Card" then
    local bd = w("Bdr_CardSel_" .. i)
    if bd then try("card sel", function() bd:SetBrushColor(sel and SEL_TINT or CLEAR) end) end
  end
end
-- multi-select card modal: flat slot list (All row, fleet headers, ship cards) over the CN card slots
local function multiRender(m)
  local g, q, flat = m.multi, m.q:lower(), {}
  local function match(o) return q == "" or (o.text .. " " .. (o.key or "")):lower():find(q, 1, true) end
  if q == "" then flat[1] = { k = "all" } end
  for _, f in ipairs(g.groups) do
    local list = {}
    for _, o in ipairs(f.ships) do if match(o) then list[#list + 1] = o end end
    if #list > 0 then
      flat[#flat + 1] = { k = "hdr", f = f }
      if not g.fold[f.key] then for _, o in ipairs(list) do flat[#flat + 1] = { k = "ship", o = o, f = f } end end
    end
  end
  m.flat = flat
  for i = 1, CN do
    local it = flat[i]; local k = it and it.k
    setVis(w("Opt_Card_" .. i), k == "ship" and 0 or 1)
    setVis(w("Row_CardHdr_" .. i), (k == "all" or k == "hdr") and 0 or 1)
    setVis(w("Btn_CardFold_" .. i), k == "hdr" and 0 or 1)
    if k == "ship" then
      cardRender("Card", i, m.rowRender(it.o), g.sel.k == "ship" and g.sel.id == it.o.id)
    elseif it then
      local on = (k == "all" and g.sel.k == "all") or (k == "hdr" and g.sel.k == "fleet" and g.sel.key == it.f.key)
      M.setText("Txt_CardHdr_" .. i, k == "all" and "All ships" or (it.f.name .. " (" .. #it.f.ships .. ")"))
      if k == "hdr" then M.setText("Txt_CardFold_" .. i, g.fold[it.f.key] and ">" or "v") end
      local b = w("Btn_CardHdr_" .. i); if b then try("hdr bg", function() b:SetBackgroundColor(on and SEL_GOLD or SEL_GREY) end) end
    end
  end
  setVis(w("Btn_ModalDone"), 0)
end
local setImgIcon  -- defined after modalRender (line ~591)
local function modalRender()
  local m = S.modal; if not m then return end
  local card = m.style == "card" or m.style == "multi"
  setVis(w("Pnl_ModalHeadG"), card and 0 or 1)
  setVis(w("Pnl_ModalHeadP"), card and 1 or 0)
  setVis(w("Row_ModalScroll"), card and 1 or 0)
  setVis(w("Row_ModalCardScroll"), card and 0 or 1)
  local hd = (not card) and m.heads and m.heads()
  setVis(w("Pnl_ModalColHead"), hd and 0 or 1)
  for c = 1, 4 do
    local nh = "Txt_ModalH" .. c
    setVis(w(nh), hd and hd[c] and 0 or 1)
    if hd and hd[c] then M.setText(nh, hd[c]) end
  end
  setVis(w("Btn_ModalDone"), 1)   -- hidden here; only the ship picker (multiRender) shows Done
  if m.style == "multi" then return multiRender(m) end
  local q, n, cap = m.q:lower(), 0, card and CN or MN
  m.shown = {}
  for _, o in ipairs(m.opts) do
    if q == "" or (o.text .. " " .. (o.key or "")):lower():find(q, 1, true) then
      n = n + 1; if n <= cap then m.shown[n] = o end
    end
  end
  for i = 1, CN do
    local o = card and m.shown[i]
    setVis(w("Opt_Card_" .. i), o and 0 or 1)
    setVis(w("Row_CardHdr_" .. i), 1)   -- multi-only widgets stay collapsed here
    if o then cardRender("Card", i, (m.rowRender and m.rowRender(o)) or {}, o.id == m.id) end
  end
  local anyIcon = false
  for i = 1, MN do
    local b, o = w("Opt_Modal_" .. i), (not card) and m.shown[i] or nil
    if b then
      setVis(b, o and 0 or 1)
      if o then
        local r = (m.rowRender and m.rowRender(o)) or {}
        local cols, icon = r.cols or { o.text }, r.icon or o.icon
        m.iconSize = 8 + (r.iconSize or 20)
        if icon then anyIcon = true end
        for c = 1, 4 do
          local nmc = "Txt_ModalC" .. c .. "_" .. i
          setVis(w(nmc), cols[c] and 0 or 1)
          if cols[c] then M.setText(nmc, cols[c]) end
        end
        local ores = r.ores or {}
        setVis(w("Row_ModalOres_" .. i), #ores > 0 and 0 or 1)
        for k = 1, 6 do
          local img = w("Img_ModalOre_" .. i .. "_" .. k)
          if #ores > 0 then setImgIcon(img, ores[k]) else setVis(img, 1) end
        end
        local im = w("Img_ModalIcon_" .. i)
        if im then
          local sz = r.iconSize or 20; try("opt icon size", function() im:SetDesiredSizeOverride({ X = sz, Y = sz }) end)
          if icon then try("opt icon", function() im:SetBrushFromTexture(icon, false) end); setVis(im, 0)
          else setVis(im, 1) end
        end
        try("opt bg", function() b:SetBackgroundColor(o.id == m.id and SEL_GOLD or SEL_GREY) end)
      end
    end
  end
  local hp = w("Pnl_ModalColHead")  -- header text must start where row text starts: 22 + icon cell (8 pad + size)
  if hp and not card then try("head pad", function() hp.Slot:SetPadding({ Left = 22 + (anyIcon and m.iconSize or 0), Top = 0, Right = 50, Bottom = 4 }) end) end
end
-- a = {title=, options={{id=,text=,key=(extra search text),icon=(texture),...}}, id=(selected id), onPick=function(id, opt), rowRender=function(opt) -> {cols={"a","b",..max 4}, icon=texture}}
local function modalSelect(a)
  holdReads()
  S.modal ={ opts = a.options or {}, id = a.id, onPick = a.onPick, rowRender = a.rowRender, heads = a.heads, build = a.build, t0 = os.clock(), style = a.style, q = "", multi = a.multi }
  M.setText("Txt_ModalTitle", a.title or "")
  M.setText("Txt_ModalTitleG", a.title or "")
  local e = w("Edt_ModalSearch")
  if e then try("search clear", function() e:SetText(FText("")) end) end
  for _, sn in ipairs({ "Row_ModalScroll", "Row_ModalCardScroll" }) do
    local sc = w(sn)
    if sc then try("scroll start", function() sc:ScrollToStart() end) end
  end
  setVis(w("Pnl_Modal"), 0)
  modalRender()
  if e then try("search focus", function() e:SetKeyboardFocus() end) end
end
M.modalSelect = modalSelect
local function modalTick()  -- search is view-only, never saved
  local m = S.modal; if not m then return end
  if m.build and #m.opts == 0 and os.clock() - m.t0 > 1 then  -- ships.txt is empty for a few seconds after a save load: refetch until it fills
    m.t0 = os.clock(); m.opts = m.build() or {}; modalRender()
  end
  local e = w("Edt_ModalSearch"); if not e or not reading() then return end
  local s, v = pcall(function() return e:GetText() end); if not s then return end
  local q = trim(fstr(v))
  if q ~= m.q then m.q = q; modalRender() end
end
local multiDone  -- defined below
local function multiPick(m, i, what)  -- what: "card" ship row i | "hdr" fleet header (All row = every ship) | "fold". Only marks the choice; Done applies it
  local it = m and m.flat and m.flat[i]; if not it then return end
  local g = m.multi
  if what == "fold" then g.fold[it.f.key] = not g.fold[it.f.key]
  elseif what == "card" then g.sel = { k = "ship", id = it.o.id }
  elseif it.k == "all" then g.sel = { k = "all" }
  else g.sel = { k = "fleet", key = it.f.key, name = it.f.name, ships = it.f.ships } end
  if what ~= "fold" then return multiDone() end   -- single click: the click picks and closes
  modalRender()
end
multiDone = function()
  local m = S.modal; if not m or m.style ~= "multi" then return end
  local g, sel = m.multi, m.multi.sel
  modalClose()
  if sel.k == "ship" then g.onPick({ sel.id }, nil)
  elseif sel.k == "all" then g.onPick(nil, "All ships")
  else
    local ids = {}; for _, o in ipairs(sel.ships) do ids[#ids + 1] = o.id end
    g.onPick(ids, sel.name)
  end
end
local function modalInit(bind)  -- once per window open, after widgets exist
  S.modal = nil
  setVis(w("Pnl_Modal"), 1)
  local function pick(i)
    if S.modal and S.modal.style == "multi" then return multiPick(S.modal, i, "card") end
    local m = S.modal; local o = m and m.shown and m.shown[i]
    L("modal pick " .. i .. " id=" .. tostring(o and o.id))
    if not o then return end
    modalClose()
    if m.onPick then m.onPick(o.id, o, m.opts) end
  end
  bind("Btn_ModalOutside", modalClose)
  bind("Btn_ModalClose", modalClose)
  bind("Btn_ModalCloseG", modalClose)
  for i = 1, MN do bind("Opt_Modal_" .. i, function() pick(i) end) end
  for i = 1, CN do bind("Opt_Card_" .. i, function() pick(i) end) end
  bind("Btn_ModalDone", multiDone)
  for i = 1, CN do
    bind("Btn_CardHdr_" .. i, function() multiPick(S.modal, i, "hdr") end)
    bind("Btn_CardFold_" .. i, function() multiPick(S.modal, i, "fold") end)
  end
end
local function selField(key) S.sel = S.sel or {}; S.sel[key] = S.sel[key] or { opts = {} }; return S.sel[key] end
local function selPaint(key)
  local f = selField(key)
  local cur = (#f.opts == 0 and f.empty) or ""   -- f.empty = text for an empty list (Remote: "No ship")
  for _, o in ipairs(f.opts) do if o.id == f.id then cur = o.text end end
  M.setText("Txt_Sel" .. key, cur)
end
local function selSet(key, opts, id) local f = selField(key); f.opts, f.id = opts, id; selPaint(key) end
local function selInit(key, bind, onPick, title, rowRender, build, heads, style)  -- build() (optional) makes fresh options on every click; style "card" = card pool
  local f = selField(key)
  bind("Sel_" .. key, function()
    modalSelect{ title = title, options = build and build() or f.opts, build = build, id = f.id, rowRender = rowRender, heads = heads, style = style,
      onPick = function(id, _, opts) f.id = id; if opts and build then f.opts = opts end; selPaint(key); if onPick then onPick(id) end end }
  end)
end
local INI = M.DATA .. "MiningBench.ini"
local function wrlines(path, lines)
  local fh = io.open(path, "w"); if not fh then return false end
  for _, l in ipairs(lines) do fh:write(l .. "\n") end
  fh:close(); return true
end
local SHIP_SAVE_KEYS = { "dock_name_filter", "refuel_dock_id", "refuel_docks", "unload_docks_cobalt", "unload_docks_graphite", "unload_docks_iridium",
  "unload_docks_nickel", "unload_docks_silica", "unload_docks_titanium", "unload_docks_waterice", "resource", "amount", "items", "belt_sector" }
local targets  -- ship ids a command/save goes to (defined after benchShips)
local function iniSave(vals)
  if not S.sid then return end
  local lines = rdlines(INI) or {}
  for _, id in ipairs(targets()) do iniPatch(lines, "ship." .. id, vals, SHIP_SAVE_KEYS) end
  if not wrlines(INI, lines) then L("FAIL ini write") end
end
local benchShips  -- defined below
-- "a, b, , c" -> {"a","b","c"} (comma list of dock ids; empty ids dropped)
local function idList(s)
  local out = {}
  for id in (s or ""):gmatch("[^,]+") do id = trim(id); if id ~= "" then out[#out + 1] = id end end
  return out
end
-- dock id -> " [Unload: A, B] [Refuel: C]" from every bench ship's unload_docks_<ini> lists, refuel_docks, refuel_dock_id (ship section, else global); our UI only
local function dockTags()
  local lines = rdlines(INI) or {}
  local glob = iniGet(lines, nil)
  local by = {}
  for _, o in ipairs(benchShips()) do
    local sec = iniGet(lines, "ship." .. o.id)
    local seen = {}
    local function add(dk, role)
      if dk == "" or seen[role .. dk] then return end
      seen[role .. dk] = true
      by[dk] = by[dk] or { Unload = {}, Refuel = {} }
      table.insert(by[dk][role], o.text)
    end
    for _, u in ipairs(UD_ORES) do for _, dk in ipairs(idList(sec["unload_docks_" .. u.ini])) do add(dk, "Unload") end end
    for _, dk in ipairs(idList(sec.refuel_docks)) do add(dk, "Refuel") end
    local rid = sec.refuel_dock_id; if rid == nil then rid = glob.refuel_dock_id or "" end
    add(trim(rid), "Refuel")
  end
  local out = {}
  for dk, r in pairs(by) do
    local s = ""
    for _, role in ipairs({ "Unload", "Refuel" }) do
      if #r[role] > 0 then s = s .. " [" .. role .. ": " .. table.concat(r[role], ", ") .. "]" end
    end
    out[dk] = s
  end
  return out
end
local function loadDocks()
  local tags = dockTags()
  local out = {}
  for _, l in ipairs(rdlines(M.DATA .. "docks.txt") or {}) do
    local p = split(l)
    local cls = p[4] or ""
    if p[1] and p[1] ~= "" and cls:find("Dock", 1, true) and not cls:find("Abandoned", 1, true) and not cls:find("NPC", 1, true) and not cls:find("ShipBlockDock", 1, true) then   -- ShipBlockDock = dock block on a ship: a ship cannot dock to a ship
      local name = trim(p[2] or ""); if name == "" then name = trim(p[5] or "") end; if name == "" then name = trim(p[3] or "") end
      out[#out + 1] = { id = trim(p[1]), text = name .. " (" .. trim(p[1]) .. ")" .. (tags[trim(p[1])] or "") }
    end
  end
  table.sort(out, function(a, b) return a.text:lower() < b.text:lower() end)
  return out
end
-- dock id -> true when another ship occupies it or heads there (DockBusy.txt "<dock id>|<occupant ship id>", written by C++)
local function dockBusyMap(sid)
  local busy = {}
  for _, l in ipairs(rdcached(M.DATA .. "DockBusy.txt") or {}) do
    local p = split(l)
    local dk, occ = trim(p[1] or ""), trim(p[2] or "")
    if dk ~= "" and occ ~= tostring(sid) then busy[dk] = true end
  end
  return busy
end
-- Dock picker options: real docks, busy ones marked
local function dockPickOpts()
  local busy = dockBusyMap(S.sid)
  local out = {}
  for _, d in ipairs(loadDocks()) do
    out[#out + 1] = { id = d.id, text = d.text .. (busy[d.id] and " (busy)" or "") }
  end
  return out
end
-- ship system + chunk from MiningStatusX field 10 "sys,x,y,z" (nil when absent or "-")
local function shipChunk(sid)
  for _, l in ipairs(rdcached(M.DATA .. "MiningStatusX.txt") or {}) do
    local p = split(l)
    if trim(p[1] or "") == tostring(sid) then
      local c = {}
      for v in (p[10] or ""):gmatch("[^,]+") do c[#c + 1] = tonumber(trim(v)) end
      return #c == 4 and c or nil
    end
  end
end
-- Sectors.txt lines "id|name|ores|ore ids|sys,x,y,z" (C++). Rows: (none), AUTO, then sectors by name.
-- Distance col = km from the selected ship's chunk (same system only): sqrt(dx^2+dy^2+dz^2) * 2.048 (chunk = 2048 m), "-" when unknown.
local function loadSectors()
  local out = { { id = "none", text = "(none - never warp)", cols = { "(none - never warp)" } },
                { id = "auto", text = "AUTO - closest sector with the ore", cols = { "AUTO - closest sector with the ore" } } }
  local me = S.sid and shipChunk(S.sid)
  local secs = {}
  for _, l in ipairs(rdlines(M.DATA .. "Sectors.txt") or {}) do
    local p = split(l)
    local id, name, ores = trim(p[1] or ""), trim(p[2] or ""), trim(p[3] or "")
    local ids = {}  -- 4th field: ore item ids, comma separated
    for oid in (p[4] or ""):gmatch("[^,]+") do ids[#ids + 1] = trim(oid) end
    local at = {}  -- 5th field: sys,x,y,z of the sector
    for v in (p[5] or ""):gmatch("[^,]+") do at[#at + 1] = tonumber(trim(v)) end
    local km = "-"
    if me and #at == 4 and at[1] == me[1] then
      local dx, dy, dz = at[2] - me[2], at[3] - me[3], at[4] - me[4]
      km = math.floor(math.sqrt(dx * dx + dy * dy + dz * dz) * 2.048 + 0.5) .. " km"
    end
    if id ~= "" then
      if name == "" then name = id end
      secs[#secs + 1] = { id = id, text = name .. " (" .. id .. ")", cols = { name, id, km }, key = ores, ores = ids }
    end
  end
  table.sort(secs, function(a, b) return a.cols[1]:lower() < b.cols[1]:lower() end)
  for _, s in ipairs(secs) do out[#out + 1] = s end
  return out
end
local function shipFill()
  -- belt field text: AUTO / (none) / "<name> (<id>)"; unknown id shows the id
  local B, known = {}, false
  for _, o in ipairs(loadSectors()) do
    if o.id == S.last.belt then known = true end
    B[#B + 1] = { id = o.id, text = o.id == "auto" and "AUTO" or (o.id == "none" and "(none)" or o.text) }
  end
  if not known then B[#B + 1] = { id = S.last.belt, text = S.last.belt } end
  selSet("Belt", B, S.last.belt)
end
-- ===== generic dynamic list: a DynamicEntryBox of a Python-built row WBP. Python widgets are not Is Variable, so boxes and entry children are found by walking
-- from a variable anchor / the entry root (deepFind). spec: key, anchor (variable widget above the box), box (name), n (row count), bind(e, find, idx) once per new entry,
-- paint(e, find, i) for every row on every call. Entries live in S.dyn[key].ents[i] = { w = entry, find = fn(name) }.
local function deepFind(node, name, depth)
  depth = depth or 0
  if depth > 12 or not ok(node) then return nil end
  if nm(node) == name then return node end
  local okc, cnt = pcall(function() return node:GetChildrenCount() end)
  if okc and type(cnt) == "number" then
    for i = 0, cnt - 1 do
      local okk, c = pcall(function() return node:GetChildAt(i) end)
      if okk then local r = deepFind(c, name, depth + 1); if r then return r end end
    end
  end
  local okq, c = pcall(function() return node:GetContent() end)
  if okq and ok(c) then return deepFind(c, name, depth + 1) end
  return nil
end
local function dynList(sp)
  local anchor = w(sp.anchor)
  if not anchor then S.dynWarn = S.dynWarn or {}; if not S.dynWarn[sp.key] then S.dynWarn[sp.key] = true; L("dynList " .. sp.key .. ": anchor " .. sp.anchor .. " not found") end; return nil end
  S.dyn = S.dyn or {}
  local st = S.dyn[sp.key]
  if not (st and ok(st.box)) then
    local box = deepFind(anchor, sp.box)
    if not ok(box) then L("dynList " .. sp.key .. ": box " .. sp.box .. " not found"); return nil end
    st = { box = box, ents = {} }; S.dyn[sp.key] = st
    try("dyn reset", function() box:Reset(true) end)
  end
  local function idxOf(e) for i, r in ipairs(st.ents) do if r.w == e then return i end end end
  while #st.ents < sp.n do
    local s2, e = pcall(function() return st.box:BP_CreateEntry() end)
    if not (s2 and ok(e)) then L("dynList " .. sp.key .. " CreateEntry failed: " .. tostring(e)); break end
    local cache = {}
    local function find(name)
      if cache[name] == nil then
        local s3, root = pcall(function() return e.WidgetTree.RootWidget end)
        cache[name] = (s3 and deepFind(root, name)) or false
      end
      return cache[name] or nil
    end
    st.ents[#st.ents + 1] = { w = e, find = find }
    if sp.bind then sp.bind(e, find, function() return idxOf(e) end) end
  end
  while #st.ents > sp.n do
    local r = table.remove(st.ents); try("RemoveEntry", function() st.box:RemoveEntry(r.w) end)
  end
  if sp.paint then for i, r in ipairs(st.ents) do sp.paint(r.w, r.find, i) end end
  return st
end
local udSave, dynDocks   -- defined below
-- Dock list = dynList of WBP_DockRow (Btn_Sel + Txt_Sel + Btn_Del). getList() = current ids table (S.rd / S.ud[x] are replaced on reload: never hold one).
function dynDocks(name, getList, save, title, docks, cardName)
  dynList{ key = name, anchor = cardName or ("Pnl_" .. name .. "Card"), box = "Dyn_" .. name, n = #getList(),
    bind = function(_, find, idx)
      if not (BIND and BIND.onClick) then return end
      local bs, bd = find("Btn_Sel"), find("Btn_Del")
      if bs then BIND.onClick(bs, function()
        local i = idx(); if not i then return end
        modalSelect{ title = title, options = S.docks or {}, id = getList()[i], onPick = function(id) getList()[i] = id; save() end }
      end) end
      if bd then BIND.onClick(bd, function() local i = idx(); if i then table.remove(getList(), i); save() end end) end
    end,
    paint = function(_, find, i)
      local id = getList()[i]; local cur = id
      for _, o in ipairs(docks) do if o.id == id then cur = o.text end end
      local t = find("Txt_Sel"); if t then try("dock text", function() t:SetText(FText(cur or "")) end) end
    end }
end
-- Ship tab dock lists. S.ud[<ini>] = unload dock ids of the ore, S.rd = refuel dock ids (this ship; All ships = first ship of the set).
-- Ore card shown only while the global ore_<ini> flag is on (default on). Warning = ore on + empty list (not in All ships mode).
local function udRender()
  if not S.ud then return end
  local glob = iniGet(rdlines(INI) or {}, nil)
  local docks, warn = S.docks or {}, {}
  for _, u in ipairs(UD_ORES) do
    local on = (glob["ore_" .. u.ini] or "1") == "1"
    local list = S.ud[u.ini]
    setVis(w("Pnl_UDockCard_" .. u.k), on and 0 or 1)
    if on then dynDocks("UDock_" .. u.k, function() return S.ud and S.ud[u.ini] or {} end, udSave, "Unload dock", docks, "Pnl_UDockCard_" .. u.k) end
    if on and #list == 0 then warn[#warn + 1] = u.k end
  end
  setVis(w("Pnl_RDockCard"), 0)
  dynDocks("RDock", function() return S.rd end, udSave, "Refuel dock", docks)
  S.warn = (not S.all and #warn > 0) and ("No unload dock for: " .. table.concat(warn, ", ")) or ""
  M.setText("Txt_ShipWarn", S.warn)
  setVis(w("Txt_ShipWarn"), S.warn ~= "" and 0 or 1)
end
-- writes every list of the window's ship(s) to INI (empty ids never written), then reloads the dock names/tags
function udSave()
  local vals = {}
  for _, u in ipairs(UD_ORES) do vals["unload_docks_" .. u.ini] = table.concat(S.ud[u.ini], ",") end
  vals.refuel_docks = table.concat(S.rd, ",")
  iniSave(vals)
  S.docks = loadDocks(); shipFill(); udRender()
end

local function shipLoad()
  S.last = {}
  if not S.sid then return end
  local lines = rdlines(INI) or {}
  local sec, glob = iniGet(lines, "ship." .. S.sid), iniGet(lines, nil)
  local function pick(k) if sec[k] ~= nil then return sec[k] end return glob[k] or "" end
  S.last.refuel = pick("refuel_dock_id")
  S.last.belt = pick("belt_sector"); if S.last.belt == "" then S.last.belt = "auto" end
  S.ud = {}
  for _, u in ipairs(UD_ORES) do S.ud[u.ini] = idList(sec["unload_docks_" .. u.ini]) end
  S.rd = idList(sec.refuel_docks)
  S.docks = loadDocks()
  shipFill()
  udRender()
end

-- ===== Header ship selector (key "Ship"): bench ships (ships.txt last column = 1) sorted by name =====
function benchShips()
  local out = {}
  for _, l in ipairs(rdlines(M.DATA .. "ships.txt") or {}) do
    local p = split(l)
    if #p >= 3 and trim(p[#p]) == "1" then
      local name = trim(table.concat(p, "|", 2, #p - 1))
      local id = trim(p[1])
      if id ~= "" then out[#out + 1] = { id = id, text = name ~= "" and name or id } end
    end
  end
  table.sort(out, function(a, b) return a.text:lower() < b.text:lower() end)
  return out
end
function targets()  -- All ships mode: every bench ship; else the single S.sid
  if not S.all then return S.sid and { S.sid } or {} end
  if S.set then local t = {}; for _, o in ipairs(benchShips()) do if S.set[o.id] then t[#t + 1] = o.id end end return t end
  local t = {}
  for _, o in ipairs(benchShips()) do t[#t + 1] = o.id end
  return t
end

-- ===== Items tab: ordered resource queue (QN rows = pool, same as gen_paste ROW_N). Saved as items=ID:AMT,... in [ship.<id>] =====
local QN = 20
local MINEABLE = {  -- tractor-beamable ores (exact ids from items.txt)
  ITEM_CobaltOre = 1, ITEM_Graphite = 1, ITEM_IridiumOre = 1, ITEM_NickelOre = 1, ITEM_Silica = 1,
  ITEM_TitaniumOre = 1, ITEM_WaterIce = 1, ITEM_EnrichedCobaltOre = 1, ITEM_EnrichedNickelOre = 1, ITEM_EnrichedTitaniumOre = 1,
}
local function isMineable(obj) return MINEABLE[obj] == 1 end
local function loadItems()  -- same source as the old menu: all Item objects
  local out = {}
  local s, list = pcall(FindAllOf, "Item")
  for _, it in ipairs(s and list or {}) do
    if ok(it) then
      local s1, obj = pcall(function() return it:GetFName():ToString() end)
      if s1 and obj and not obj:find("Default__", 1, true) and not obj:find("Filter", 1, true) then
        local s2, label = pcall(function() return it.DisplayName:ToString() end)
        if not (s2 and label and label ~= "") then label = (obj:gsub("^ITEM_", "")) end
        -- icon: bundled Img_ItemSrc_<id> texture first, game Item.Icon only as fallback
        local sb, ic = pcall(function() local t = w("Img_ItemSrc_" .. obj).Brush.ResourceObject; return t and t:IsValid() and t or nil end)
        if not (sb and ok(ic)) then
          local s3, fb = pcall(function() return it.Icon end)
          ic = (s3 and ok(fb)) and fb or nil
        end
        out[#out + 1] = { id = obj, text = label, key = obj, icon = ic }
      end
    end
  end
  table.sort(out, function(a, b) return a.text:lower() < b.text:lower() end)
  return out
end
local function qSerialize()
  local t = {}
  for _, r in ipairs(S.q.rows) do t[#t + 1] = r.id .. ":" .. r.amt end
  return table.concat(t, ",")
end
local function qSave() iniSave({ items = qSerialize() }) end
-- dropdown options for row r: mineable items, plus the row's current item when it is not mineable
local function qOpts(r)
  local o = {}
  for _, it in ipairs(S.items.min) do o[#o + 1] = it end
  local cur = S.q.rows[r].id
  if cur ~= "" then
    local found = false
    for _, it in ipairs(o) do if it.id == cur then found = true break end end
    if not found then o[#o + 1] = { id = cur, text = cur, key = cur, icon = S.items.byId[cur] and S.items.byId[cur].icon } end
  end
  return o
end
function setImgIcon(img, itemId)
  if not img then return end
  local it = S.items and S.items.byId and itemId and S.items.byId[itemId]
  if it and it.icon then try("item icon", function() img:SetBrushFromTexture(it.icon, false) end); setVis(img, 0)
  else setVis(img, 2) end
end
local function qRender()
  holdReads()
  local rows = S.q.rows
  dynList{ key = "Items", anchor = "Page_Items", box = "Dyn_Items", n = #rows,
    bind = function(_, find, idx)
      local function row() local i = idx(); return i and S.q.rows[i], i end
      local bs, bu, bd, bx = find("Btn_Sel"), find("Btn_Up"), find("Btn_Down"), find("Btn_Del")
      if BIND and BIND.onClick then
        if bs then BIND.onClick(bs, function()
          local rw, i = row(); if not rw then return end
          modalSelect{ title = "Resource", options = qOpts(i), id = rw.id, onPick = function(id) rw.id = id; qSave(); qRender() end }
        end) end
        if bu then BIND.onClick(bu, function() local _, i = row(); local rs = S.q.rows
          if i and i > 1 then rs[i - 1], rs[i] = rs[i], rs[i - 1]; qSave(); qRender() end end) end
        if bd then BIND.onClick(bd, function() local _, i = row(); local rs = S.q.rows
          if i and i < #rs then rs[i], rs[i + 1] = rs[i + 1], rs[i]; qSave(); qRender() end end) end
        if bx then BIND.onClick(bx, function() local _, i = row(); if i then table.remove(S.q.rows, i); qSave(); qRender() end end) end
      end
    end,
    paint = function(_, find, i)
      local rw = rows[i]
      local cur = rw.id
      for _, o in ipairs(qOpts(i)) do if o.id == rw.id then cur = o.text end end
      local t = find("Txt_Sel"); if t then try("item text", function() t:SetText(FText(cur or "")) end) end
      setImgIcon(find("Img_Icon"), rw.id)
      local e = find("Edt_Amount")
      if e and S.q.shown[i] ~= rw.amt then try("amount set", function() e:SetText(FText(rw.amt)) end) end
      S.q.shown[i] = rw.amt
    end }
end
local function itemsLoad()
  if not S.sid or not S.q then return end
  local lines = rdlines(INI) or {}
  local sec, glob = iniGet(lines, "ship." .. S.sid), iniGet(lines, nil)
  local rows = {}
  if sec.items ~= nil then
    for entry in (sec.items .. ","):gmatch("([^,]*),") do
      local id, amt = entry:match("^(.*):([^:]*)$")
      if id then rows[#rows + 1] = { id = trim(id), amt = trim(amt) } end
    end
  else  -- migration: old single resource/amount becomes row 1
    local res = sec.resource or glob.resource or ""
    if res ~= "" then rows[1] = { id = res, amt = sec.amount or glob.amount or "" } end
  end
  S.q.rows, S.q.shown = rows, {}
  if sec.items == nil and #rows > 0 then qSave() end
  qRender()
end
local function itemsTick()
  if not S.sid or not S.q or M.lastTab ~= 2 then return end
  local rows = S.q.rows
  for r = 1, #rows do
    local rec = S.dyn and S.dyn.Items and S.dyn.Items.ents[r]
    local e = rec and rec.find("Edt_Amount")
    local s, v = false, nil
    if e and reading() then s, v = pcall(function() return e:GetText() end) end
    if s then
      local t = trim(fstr(v))
      if t ~= S.q.shown[r] then
        S.q.shown[r] = t
        local n = tonumber(t)
        if n and n == math.floor(n) and n >= 0 then
          rows[r].amt = t
          qSave()
          if S.q.err then S.q.err = nil; M.setText("Txt_Status", "") end
        else
          S.q.err = true; M.setText("Txt_Status", "error: amount must be an integer >= 0")
        end
      end
    end
  end
end

-- ===== Operation tab: instant save per key. sc "ship" -> [ship.<id>]; sc "global" -> top level (C++ reads these only globally) =====
-- kind: int (>=0 integer), dist (5km / 2500m / plain / empty, as old menu radiusVal), chk. empty=true: empty int allowed.
local OPS = {
  { w = "Edt_Radius", k = "radius", sc = "ship", kind = "dist", def = "50km", msg = "radius must be like 5km, 2500m or plain meters (or empty)" },
  { w = "Edt_MiningDistance", k = "mining_distance", sc = "ship", kind = "dist", def = "50m", msg = "mining distance must be like 200m, 0.2km or plain meters (or empty)" },
  { w = "Edt_WarpRange", k = "warp_range", sc = "ship", kind = "dist", def = "50km", msg = "warp range must be like 50km, 20000m or plain meters (or empty = 50km)" },
  { w = "Edt_MaxRadius", k = "max_radius", sc = "global", kind = "dist", def = "50km", msg = "max_radius must be like 10km, 8000m or plain (or empty)" },
  { w = "Edt_IdleSeconds", k = "idle_seconds", sc = "ship", kind = "int", msg = "idle seconds must be an integer >= 0" },
  { w = "Edt_WaitSeconds", k = "wait_seconds", sc = "global", kind = "int", def = "10", msg = "wait must be an integer >= 0" },
  { w = "Edt_DockHoldLimit", k = "dock_hold_limit", sc = "ship", kind = "int", def = "300", msg = "dock time limit must be an integer >= 0" },
  { w = "Chk_Enabled", k = "enabled", sc = "ship", kind = "chk", def = "1" },
  { w = "Chk_Repeat", k = "repeat", sc = "ship", kind = "chk", def = "0" },
}
for _, f in ipairs({ { "Deuterium", "deuterium", "0" }, { "Hydrogen", "hydrogen", "1" }, { "Warp", "warp", "1" } }) do
  OPS[#OPS + 1] = { w = "Chk_Fuel" .. f[1] .. "On", k = "fuel_" .. f[2] .. "_on", sc = "global", kind = "chk", def = f[3] }
  OPS[#OPS + 1] = { w = "Edt_Fuel" .. f[1] .. "Threshold", k = "fuel_" .. f[2] .. "_threshold", sc = "global", kind = "int", def = "300", msg = f[1] .. " threshold must be an integer >= 0" }
  OPS[#OPS + 1] = { w = "Edt_Fuel" .. f[1] .. "Amount", k = "fuel_" .. f[2] .. "_amount", sc = "global", kind = "int", def = "300", msg = f[1] .. " amount must be an integer >= 0" }
end
for _, o in ipairs({ { "Cobalt", "cobalt" }, { "Graphite", "graphite" }, { "Iridium", "iridium" }, { "Nickel", "nickel" },
                     { "Silica", "silica" }, { "Titanium", "titanium" }, { "WaterIce", "waterice" } }) do
  OPS[#OPS + 1] = { w = "Chk_Unload" .. o[1], k = "ore_" .. o[2], sc = "global", kind = "chk", def = "1" }
end
-- Operation item icons (Image widgets; brush from bundled Img_ItemSrc_<id> texture, game Item.Icon only as fallback; hidden when no icon)
local OPICONS = { Img_FuelDeuterium = "ITEM_EnrichedFuel", Img_FuelHydrogen = "ITEM_HydrogenFuel", Img_FuelWarp = "ITEM_WarpFuel",
  Img_UnloadCobalt = "ITEM_CobaltOre", Img_UnloadGraphite = "ITEM_Graphite", Img_UnloadIridium = "ITEM_IridiumOre",
  Img_UnloadNickel = "ITEM_NickelOre", Img_UnloadSilica = "ITEM_Silica", Img_UnloadTitanium = "ITEM_TitaniumOre",
  Img_UnloadWaterIce = "ITEM_WaterIce" }
local extra = {}  -- dock card header icons reuse the same items
for n, id in pairs(OPICONS) do
  local k = n:match("^Img_Unload(.+)$")
  extra[k and ("Img_UDockOre_" .. k) or ("Img_RDockFuel" .. n:match("^Img_Fuel(.+)$"))] = id
end
for n, id in pairs(extra) do OPICONS[n] = id end
-- normalized string, or nil when invalid (rules = old menu int0 / radiusVal)
local function opVal(e, t)
  if e.kind == "int" then
    if t == "" and e.empty then return "" end
    local n = tonumber(t)
    if n and n == math.floor(n) and n >= 0 then return string.format("%d", n) end
    return nil
  end
  t = t:lower()
  if t == "" or t:match("^%d+%.?%d*%s*km$") or t:match("^%d+%.?%d*%s*m$") or t:match("^%d+%.?%d*%s*$") then return t end
  return nil
end
-- effective value like old pick(): ship value (if non-empty), else global, else default
local function opPick(e, sec, glob)
  if e.sc == "ship" and sec[e.k] ~= nil and sec[e.k] ~= "" then return sec[e.k] end
  return glob[e.k] or e.def or ""
end
local function opSave(e, v)
  local lines = rdlines(INI) or {}
  if e.sc == "ship" then
    for _, id in ipairs(targets()) do iniPatch(lines, "ship." .. id, { [e.k] = v }, { e.k }) end
  else
    iniPatch(lines, nil, { [e.k] = v }, { e.k })
  end
  if not wrlines(INI, lines) then L("FAIL ini write") end
end
local function opsLoad()
  holdReads()
  if not S.sid or not S.op then return end
  for n, id in pairs(OPICONS) do setImgIcon(w(n), id) end
  local lines = rdlines(INI) or {}
  local sec, glob = iniGet(lines, "ship." .. S.sid), iniGet(lines, nil)
  if glob.radius == nil then glob.radius = glob.range_raw end  -- old global key (as old menu)
  S.op.shown = {}
  for _, e in ipairs(OPS) do
    local o, v = w(e.w), opPick(e, sec, glob)
    if e.kind == "dist" and v:match("^%d+%.?%d*$") then v = v .. "m" end   -- plain number = meters: show the unit
    if o then
      if e.kind == "chk" then
        v = (v == "1") and "1" or "0"
        try("chk set", function() o:SetIsChecked(v == "1") end)
      else
        try("op set", function() o:SetText(FText(v)) end)
      end
      S.op.shown[e.w] = v
    end
  end
end
local function opsTick()
  if not S.sid or not S.op or not S.op.shown or M.lastTab ~= 3 then return end   -- edit boxes on hidden pages are never read (FText crash)
  for _, e in ipairs(OPS) do
    local o = w(e.w)
    if o and S.op.shown[e.w] ~= nil then
      local s, t
      if e.kind == "chk" then
        s, t = pcall(function() return o:IsChecked() and "1" or "0" end)
      else
        local s1, x = false, nil
        if reading() then s1, x = pcall(function() return o:GetText() end) end
        s, t = s1, s1 and trim(fstr(x)) or nil
      end
      if s and t ~= S.op.shown[e.w] then
        S.op.shown[e.w] = t
        local v = (e.kind == "chk") and t or opVal(e, t)
        if v then
          opSave(e, v)
          if e.k:find("^ore_") then udRender() end   -- ore on/off shows or hides its dock card + warning
          if S.op.err == e.w then S.op.err = nil; M.setText("Txt_Status", "") end
        else
          S.op.err = e.w; M.setText("Txt_Status", "error: " .. e.msg)
        end
      end
    end
  end
end
-- "Set as default": this ship's effective per-ship Operation values -> top-level defaults (global-only keys already live there)
local function opDefault()
  if not S.sid or not S.op then return end
  local lines = rdlines(INI) or {}
  local sec, glob = iniGet(lines, "ship." .. S.sid), iniGet(lines, nil)
  if glob.radius == nil then glob.radius = glob.range_raw end
  local vals, keys = {}, {}
  for _, e in ipairs(OPS) do
    if e.sc == "ship" then vals[e.k] = opPick(e, sec, glob); keys[#keys + 1] = e.k end
  end
  iniPatch(lines, nil, vals, keys)
  if wrlines(INI, lines) then M.setText("Txt_Status", "Saved global defaults " .. os.date("%H:%M:%S"))
  else L("FAIL ini write"); M.setText("Txt_Status", "error: INI write failed") end
end

-- row of '|' fields whose first field is the ship id
local function findRow(lines, sid)
  for _, l in ipairs(lines or {}) do
    local p = split(l)
    if trim(p[1]) == sid then return p end
  end
end

-- MiningStatus.txt: id|name|simple|state: result   MiningStatusX.txt: id|run(0/1)|cargo_pct|speed_m_s|next|left(items or -1)|job|jobphase
-- job = none|dock|unload|refuel|undock (7th col, nil when absent = old DLL); jobphase = short text (8th col, optional); item = current mining item id (9th col, nil when absent)
local function readStatus(sid)
  local st = {}
  local p = findRow(rdcached(M.DATA .. "MiningStatus.txt"), sid)
  if p then st.simple = trim(p[3] or "") end
  local x = findRow(rdcached(M.DATA .. "MiningStatusX.txt"), sid)
  if x then
    st.run = trim(x[2] or "") == "1"
    st.cargo = tonumber(x[3])
    st.speed = tonumber(x[4])
    st.next = trim(x[5] or "")
    st.left = tonumber(x[6])
    st.job = trim(x[7] or "")
    if st.job == "" then st.job = nil end
    st.jobphase = trim(x[8] or "")
    if st.jobphase == "" then st.jobphase = nil end
    st.item = trim(x[9] or "")
    if st.item == "" then st.item = nil end
    st.have = tonumber(x[11])
    st.want = tonumber(x[12])
  elseif st.simple and st.simple ~= "" then
    st.run = st.simple ~= "idle"  -- fallback until MiningStatusX.txt exists
  end
  return st
end

-- run: true -> Start green, false -> Stop red, nil -> both normal. pend "start"/"stop" -> that button amber, the other normal.
local function setRun(run, pend)
  local c1, c2 = BTN_NORMAL, BTN_NORMAL
  if pend == "start" then c1 = BTN_AMBER
  elseif pend == "stop" then c2 = BTN_AMBER
  elseif run == true then c1 = BTN_GREEN
  elseif run == false then c2 = BTN_RED end
  local b1, b2 = w("Btn_LoopStart"), w("Btn_LoopStop")
  if b1 then try("bg LoopStart", function() b1:SetBackgroundColor(c1) end) end
  if b2 then try("bg LoopStop", function() b2:SetBackgroundColor(c2) end) end
end

-- Btn_EmergencyStop colour and Txt_EmergencyStop label are set in the WBP (hazard frame, red style,
-- "EMERGENCY STOP"). Lua does not repaint them (paintEstop removed).

-- MiningCmd.txt: ONE line "<seq>|<ship_id>|<cmd>", seq always increasing (same as main.lua MT.sendTo)
local function sendCmd(cmd, sid)
  local id = (sid ~= nil) and sid or S.sid
  if id == nil then L("sendCmd: no ship id"); return end
  local base = os.time() * 1000
  S.seq = (base > S.seq) and base or (S.seq + 1)
  local fh, err = io.open(M.DATA .. "MiningCmd.txt", "w")
  if not fh then L("MiningCmd.txt open failed: " .. tostring(err)); return end
  fh:write(S.seq .. "|" .. id .. "|" .. cmd .. "\n")
  fh:close()
  L("cmd " .. S.seq .. "|" .. id .. "|" .. cmd)
end
-- every command to every target ship; 400 ms apart (MiningCmd.txt holds one line, polled 0.25 s). First command immediate.
local function sendSeq(cmds)
  local k = 0
  for _, id in ipairs(targets()) do
    for _, c in ipairs(cmds) do
      local d = k * 400; k = k + 1
      if d == 0 then sendCmd(c, id) else ExecuteWithDelay(d, function() pcall(sendCmd, c, id) end) end
    end
  end
end

local function fmtEta(sec)
  sec = math.floor(sec + 0.5)
  if sec >= 60 then return string.format("%dm %02ds", math.floor(sec / 60), sec % 60) end
  return sec .. "s"
end

-- Txt_Eta: visible (SetVisibility 0) only while unloading/refueling, else Collapsed (1). Rate from MiningStatusX 'left' samples (wall clock, 15 s window, need >= 5 s span).
local function updateEta(st)
  local e = w("Txt_Eta"); if not e then return end
  local active = st.simple == "unloading" or st.simple == "refueling"
  try("eta vis", function() e:SetVisibility(active and 0 or 1) end)
  if not active then S.etaS = nil; S.etaKind = nil; return end
  if st.simple ~= S.etaKind then S.etaKind = st.simple; S.etaS = nil end
  local txt = "ETA: --"
  if st.left and st.left >= 0 then
    local now = os.time()
    local a = S.etaS or {}
    S.etaS = a
    if #a > 0 and st.left > a[#a].n then for i = #a, 1, -1 do a[i] = nil end end   -- left rose = new trip, restart
    if #a == 0 or a[#a].t ~= now then a[#a + 1] = { t = now, n = st.left } end
    while #a > 1 and now - a[1].t > 15 do table.remove(a, 1) end
    local f = a[1]
    local dt = now - f.t
    if st.left == 0 then txt = "ETA: 0s"
    elseif dt >= 5 and f.n > st.left then txt = "ETA: " .. fmtEta(st.left * dt / (f.n - st.left)) end
  end
  M.setText("Txt_Eta", txt)
end

-- One-shot action buttons -> job commands for THIS ship (C++ run_cmd / job_start, mb_testcmd.hpp):
--   Dock   = dock_auto: C++ auto_rule picks refuel dock if an enabled fuel is below its threshold, else mining dock (same rule as the loop).
--   Undock = undock: leave the current dock for a second configured dock (refuel dock first).
--   Refuel = dock_refuel: dock at the refuel dock and fill the enabled fuel items.
--   Unload = dock_mining: dock at the mining dock and unload the enabled ore (job_start: not refuel -> ob_plan_unload).
-- These are overrides: stop_all for this ship first (halts the loop), then the action 400 ms later (user decision).
local ACT_BTNS = {
  Btn_Dock = { cmd = "dock_auto", label = "dock" },
  Btn_Undock = { cmd = "undock", label = "undock" },
  Btn_Refuel = { cmd = "dock_refuel", label = "refuel" },
  Btn_Unload = { cmd = "dock_mining", label = "unload" },
  Btn_Collect = { cmd = "collect_ore", label = "collect ore" },
}
local ACT_ORDER = { "Btn_Dock", "Btn_Undock", "Btn_Refuel", "Btn_Unload", "Btn_Collect" }
-- MiningStatusX 'job' value -> button to keep amber + status text (job none/absent/unknown = no entry)
local JOB = {
  dock = { btn = "Btn_Dock", txt = "Docking" },
  undock = { btn = "Btn_Undock", txt = "Undocking" },
  refuel = { btn = "Btn_Refuel", txt = "Refueling" },
  unload = { btn = "Btn_Unload", txt = "Unloading" },
}

-- lit = button name in amber (active job, else the pressed one in requested phase); every other action button normal
local function paintAct(lit)
  for _, n in ipairs(ACT_ORDER) do
    local b = w(n)
    if b then
      local c = (lit == n) and BTN_AMBER or BTN_NORMAL
      try("bg " .. n, function() b:SetBackgroundColor(c) end)
    end
  end
end

-- ===== Log tab: MiningLog.txt lines "<ship id>|<hh:mm:ss>|<text>" (oldest first per ship, C++ keeps 20 per ship) =====
local LOGN = 60  -- must equal the Txt_Log_1..N rows in the WBP (gen_paste)
local function logLoad()
  local out = {}
  for _, l in ipairs(rdcached(M.DATA .. "MiningLog.txt") or {}) do
    local p = split(l)
    if p[2] then out[#out + 1] = { sid = trim(p[1]), t = trim(p[2]), text = p[3] or "", i = #out + 1 } end
  end
  return out
end
-- Single ship: Row_LogScroll list, that ship's lines newest first. All ships: Pnl_LogAll = one card per fleet ship (S.fleet.rows, filled by fleetPaint
-- before this): name + status, then its newest LOGCARD_ROWS lines "hh:mm:ss text". Repaints only when the shown text changes.
-- ponytail: time sort is hh:mm:ss text, so it is wrong across midnight (lines from before 00:00 sort as newest).
local LOGCARD_ROWS = 10  -- must equal LOG_ROWS in gen_paste.py (Txt_LogCard_<f>_<r>)
local function logNewest(lines, sid)  -- this ship's lines, newest first
  local sel = {}
  for _, e in ipairs(lines) do if e.sid == sid then sel[#sel + 1] = e end end
  table.sort(sel, function(a, b) if a.t ~= b.t then return a.t > b.t end return a.i > b.i end)
  return sel
end
local function logPaintAll(lines)
  local rows, cards, key = S.fleet and S.fleet.rows or {}, {}, {}
  for f = 1, FN do
    local o = rows[f]
    local c = { on = o ~= nil, name = o and o.text or "", status = o and o.status or "", lines = {} }
    if o then
      local sel = logNewest(lines, tostring(o.id))
      for r = 1, math.min(#sel, LOGCARD_ROWS) do c.lines[r] = sel[r].t .. " " .. sel[r].text end
    end
    cards[f] = c
    key[#key + 1] = c.name .. "|" .. c.status .. "\n" .. table.concat(c.lines, "\n")
  end
  key = "A" .. table.concat(key, "\n#\n")
  if key == S.logKey then return end
  S.logKey = key
  for f = 1, FN do
    local c = cards[f]
    setVis(w("Pnl_LogCard_" .. f), c.on and 0 or 1)
    if c.on then M.setText("Txt_LogCardName_" .. f, c.name); M.setText("Txt_LogCardStatus_" .. f, c.status) end
    for r = 1, LOGCARD_ROWS do
      local t = c.lines[r]
      setVis(w("Txt_LogCard_" .. f .. "_" .. r), t and 0 or 1)
      if t then M.setText("Txt_LogCard_" .. f .. "_" .. r, t) end
    end
  end
end
local function logPaint()
  local all = S.all and true or false
  setVis(w("Row_LogScroll"), all and 1 or 0)
  setVis(w("Pnl_LogAll"), all and 0 or 1)
  local lines = logLoad()
  if all then setVis(w("Txt_LogHint"), 1); return logPaintAll(lines) end
  local rows = {}
  for k, e in ipairs(logNewest(lines, tostring(S.sid))) do
    if k > LOGN then break end
    rows[k] = e.t .. "  " .. e.text
  end
  local key = "S" .. table.concat(rows, "\n")
  if key == S.logKey then return end
  S.logKey = key
  L("logPaint rows=" .. #rows .. " sid=" .. tostring(S.sid))
  dynList{ key = "Log", anchor = "Row_LogScroll", box = "Dyn_Log", n = #rows, paint = function(e, _, i)
    local s2, t = pcall(function() return e.WidgetTree.RootWidget end)   -- the row's root widget is the TextBlock itself
    if s2 and ok(t) then try("log text", function() t:SetText(FText(rows[i])) end) end
  end }
  M.setText("Txt_LogHint", "No log yet")
  setVis(w("Txt_LogHint"), #rows == 0 and 0 or 1)
end

local fleetPaint  -- defined after shipRowRender (needs shipRows)
local function refresh()
  if S.sid and S.last and os.clock() - (S.docksAt or 0) > 5 then   -- dock renames in game show up without reopening the window
    S.docksAt = os.clock(); S.docks = loadDocks(); shipFill(); udRender()
  end
  if not S.sid then return end
  S.tick = S.tick + 1
  modalTick()
  local st = readStatus(S.sid)
  local allCargo, allRun
  if S.all then  -- All ships: average cargo, run only when every ship runs (no summary text: the fleet cards show status)
    local n, cs, runs = 0, 0, 0
    for _, id in ipairs(targets()) do
      local s = readStatus(id)
      n = n + 1; cs = cs + (s.cargo or 0); if s.run then runs = runs + 1 end
    end
    allCargo = (n > 0) and cs / n or 0
    if n == 0 then allRun = nil
    elseif runs == n then allRun = true
    elseif runs == 0 then allRun = false
    else allRun = nil end
  end
  fleetPaint()
  local cargo = math.max(0, math.min(100, st.cargo or 0))
  if S.all then cargo = math.max(0, math.min(100, allCargo)) end
  if S.pend then   -- real state confirms, or 6 s timeout -> show the real state
    if st.run ~= nil and st.run == (S.pend.k == "start") then S.pend = nil
    elseif S.tick >= S.pend.dl then
      L("pending " .. S.pend.k .. " timeout, real run=" .. tostring(st.run) .. " simple=" .. tostring(st.simple)); S.pend = nil
    end
  end
  -- action: job column active = job running (button amber, job text). Requested phase (after press) ends when a job is seen or 6 s
  local job = (not S.all) and st.job and JOB[st.job] or nil
  if job then S.req = nil end
  if S.req and S.tick >= S.req.dl then S.req = nil end
  if S.arm and S.tick >= S.arm then S.arm = nil end   -- 3 s disarm
  local act = ACTION[st.simple] or "-"
  if S.all then act = "" end
  if job then act = job.txt .. (st.jobphase and (" (" .. st.jobphase .. ")") or "") .. "..." end
  if S.req then act = "Requested " .. S.req.label .. "..." end
  if S.pend then act = (S.pend.k == "start") and "Requested start..." or "Requested stop..." end
  if S.estop and S.tick < S.estop then act = "Emergency stop requested" else S.estop = nil end
  M.setText("Txt_StatusAction", act)
  local sa = w("Txt_StatusAction")
  local tint = TXT_OFF   -- error (red) beats the missing-unload-dock warning (yellow)
  if st.simple == "error" and not S.all then tint = EST_RED_TXT
  elseif S.warn and S.warn ~= "" then tint = WARN_YEL end
  if sa then try("status action tint", function() sa:SetColorAndOpacity({ SpecifiedColor = tint }) end) end
  setPercent("PB_Cargo", cargo / 100)
  M.setText("Txt_CargoPct", string.format("%.0f%%", cargo))
  M.setText("Txt_Speed", (st.speed and not S.all) and string.format("Speed: %.1f m/s", st.speed) or "Speed: -")
  M.setText("Txt_NextState", (not S.all) and ("Next: " .. ((st.next and st.next ~= "") and st.next or "-")) or "Next: -")
  -- Mining line (single ship only): current queue ore icon + name, "-" and no icon when none
  setVis(w("Row_StatusMine"), S.all and 1 or 0)
  if not S.all then
    local it = st.item and S.items and S.items.byId and S.items.byId[st.item]
    local nm = st.item and ((it and it.text) or st.item) or "-"
    if st.item and st.have then nm = nm .. " " .. st.have .. ((st.want and st.want > 0) and ("/" .. st.want) or "") end
    M.setText("Txt_StatusMine", nm)
    setImgIcon(w("Img_StatusOre"), st.item)
  end
  local run = st.run
  if S.all then run = allRun end
  setRun(run, S.pend and S.pend.k or nil)
  paintAct(job and job.btn or (S.req and S.req.n) or ((not S.all) and st.simple == "collecting" and "Btn_Collect") or nil)
  -- Collect button text: floating ore count from MiningOre.txt ("count|unix", written by the mod when the shared scan refreshes)
  do
    local ol = rdcached(M.DATA .. "MiningOre.txt")
    local on = ol and ol[1] and tonumber(ol[1]:match("^(%d+)")) or 0
    M.setText("Txt_Collect", on > 0 and ("Collect " .. on .. " ores") or "Collect ore")
  end
  S.simple = st.simple
  try("itemsTick", itemsTick)
  try("opsTick", opsTick)
  try("logPaint", logPaint)
  updateEta(S.all and {} or st)
end

-- one-shot action press: send the job command for this ship; requested phase (amber + "Requested ...") until job column shows a job or 6 s
local function doAction(n)
  -- single ship Dock: pick the dock first (modal "Dock at"), then dock_to:<id> = Dock button flow at that dock
  if n == "Btn_Dock" and not S.all then
    return modalSelect{ title = "Dock at", options = dockPickOpts(), build = dockPickOpts, onPick = function(id)
      sendSeq({ "stop_all", "dock_to:" .. id }); S.pend = nil; S.req = { n = "Btn_Dock", label = "dock", dl = S.tick + 12 }; refresh()
    end }
  end
  local a = ACT_BTNS[n]
  -- override: hard stop for this ship first (loop halted, jobs cancelled), then the action. MiningCmd.txt holds ONE line polled every
  -- 0.25 s, so the second command goes out after 400 ms (file I/O only in the delayed callback, no UObject access).
  sendSeq({ "stop_all", a.cmd })
  S.pend = nil
  S.req = { n = n, label = a.label, dl = S.tick + 12 }
  refresh()
end

-- one loop per open: stops when S.gen changes (M.close / next open). Work runs on the game thread.
local function startLoop()
  local g = S.gen
  LoopAsync(500, function()
    if g ~= S.gen then return true end
    ExecuteInGameThread(function()
      if g == S.gen and ok(S.root) then try("refresh", refresh) end
    end)
    return false
  end)
end

-- S.frame = the WBP instance in the viewport (isOpen checks it).
function M.isOpen() return ok(S.frame) and inVp(S.frame) end

function M.close()
  S.gen = S.gen + 1  -- stops the status loop
  if BIND and BIND.clear then BIND.clear() end  -- drop button callbacks + global layout hook
  S.sid = nil
  S.mode = nil
  S.all = false
  S.last, S.docks, S.sel, S.modal, S.items, S.q, S.op = nil, nil, nil, nil, nil, nil, nil
  S.pend, S.arm, S.estop, S.etaS, S.etaKind = nil, nil, nil, nil, nil
  S.req, S.simple = nil, nil
  S.fleet, S.holoOut, S.tc = nil, nil, nil
  S.ud, S.rd, S.warn, S.dyn, S.dynDiag, S.dynWarn = nil, nil, nil, nil, nil, nil
  if not (S.root or S.p or S.frame) then return end
  local root, p = S.root, S.p
  S.root, S.p, S.w, S.frame = nil, nil, nil, nil
  if ok(root) then try("RemoveFromParent", function() root:RemoveFromParent() end) end
  if ok(p) then
    try("look unignore", function() p:SetIgnoreLookInput(false) end)
    try("move unignore", function() p:SetIgnoreMoveInput(false) end)
    try("mouse off", function() p.bShowMouseCursor = false end)
    try("input GameOnly", function() StaticFindObject(LIB):SetInputMode_GameOnly(p, false) end)
  end
  L("closed")
end

-- i = 1..3 (Ship, Items, Operation) -> PageSwitcher index 0..2
function M.setTab(i)
  M.lastTab = i
  holdReads()
  local sw = w("PageSwitcher")
  if sw then try("SetActiveWidgetIndex", function() sw:SetActiveWidgetIndex(i - 1) end) end
  for k, n in ipairs(TABS) do
    local on = (k == i)
    local b = w(n)
    if b then try("tab bg " .. n, function() b:SetBackgroundColor(on and TAB_ON or TAB_OFF) end)
    else L("FAIL tab button missing " .. n) end
    local tn = "Txt_" .. n:sub(5)  -- Btn_TabShip -> Txt_TabShip
    local t = w(tn)
    if t then try("tab txt " .. tn, function() t:SetColorAndOpacity({ SpecifiedColor = on and TXT_ON or TXT_OFF }) end)
    else L("FAIL tab text missing " .. tn) end
  end
  if i == 1 then try("shipLoad", shipLoad) end
  if i == 2 then try("itemsLoad", itemsLoad) end
  if i == 3 then try("opsLoad", opsLoad) end
  L("tab " .. i)
end

-- ===== ship modal rows: name | status | distance | cargo. Status/cargo = MiningStatus(X).txt (all bench ships). Distance = player pawn to ship actor (best effort, unverified; column dropped if no ship has one). "Docked" is not published: docked ships read Idle. =====
local function xyz(v) return rd(v, "X"), rd(v, "Y"), rd(v, "Z") end
local function actorLoc(a)
  if not ok(a) then return nil end
  local s, v = pcall(function() return a:K2_GetActorLocation() end)
  if s and v ~= nil and type(rd(v, "X")) == "number" then return v end
  L("actorLoc fail: s=" .. tostring(s) .. " type=" .. type(v) .. " v=" .. tostring(v) .. " X=" .. tostring(s and type(v) ~= "nil" and rd(v, "X")))
end
local function shipDists()  -- ship id -> metres (K2_GetActorLocation difference / 100)
  local out, me = {}, actorLoc(rd(S.p, "Pawn"))
  if not me then L("dist: no player location"); return out end
  local mx, my, mz = xyz(me)
  local s, ships = pcall(FindAllOf, "DMSPlayerShipObject")
  for _, o in ipairs(s and ships or {}) do
    if ok(o) then
      local id, l = rd(o, "ID"), actorLoc(rd(o, "Actor"))
      if id ~= nil and l then
        local x, y, z = xyz(l)
        out[tostring(id)] = math.sqrt((x - mx) ^ 2 + (y - my) ^ 2 + (z - mz) ^ 2) / 100
      end
    end
  end
  return out
end
local function fmtDist(m) if m >= 1000 then return string.format("%.1f km", m / 1000) end return string.format("%.0f m", m) end
local ENTRY_CLS = "/Game/Blueprints/Widgets/Display/WBP_FleetShipEntry.WBP_FleetShipEntry_C"
local function holoTex(e)
  local s, t = pcall(function() local r = e.ShipHologram.Brush.ResourceObject; return r and r:IsValid() and r or nil end)
  return s and t or nil
end
local function shipIcons()  -- ship id -> hologram texture. Own off-screen WBP_FleetShipEntry per ship + SetShip (game builds the screenshot texture), no Ships panel needed
  -- ponytail: FindAllOf every 3 s crashed UE4SS once (auto_construct_object, null) right after a warp: reuse the last result for 30 s while every texture is still valid
  if S.holoOut then   -- holograms are built once per window
    local good = true
    for _, t in pairs(S.holoOut) do if not ok(t) then good = false break end end
    if good then return S.holoOut end
  end
  local out = {}
  local sc, cls = pcall(StaticFindObject, ENTRY_CLS)
  local sf, ships = pcall(FindAllOf, "DMSPlayerShipObject")
  S.holo = S.holo or {}
  local seen = {}
  for _, o in ipairs(sf and ships or {}) do
    if ok(o) and sc and ok(cls) then
      local id = rd(o, "ID")
      if id ~= nil then
        seen[tostring(id)] = true
        local e = S.holo[tostring(id)]
        if not ok(e) then
          pcall(function() e = StaticFindObject(LIB):Create(S.p, cls, S.p); e:SetShip(o) end)
          S.holo[tostring(id)] = e
        end
        local tex = ok(e) and holoTex(e)
        if tex then out[tostring(id)] = tex end
      end
    end
  end
  if sf then for k in pairs(S.holo) do if not seen[k] then S.holo[k] = nil end end end   -- drop widgets of ships that are gone
  S.holoOut, S.holoT = out, os.clock()
  return out
end
local function shipStatus(o)  -- fills status/cargo/skey/mine/next from MiningStatus(X) (cheap, no icon work)
  local st = readStatus(o.id)
  local job = st.job and JOB[st.job]
  o.status = job and job.txt or ACTION[st.simple] or "Idle"
  o.cargo = st.cargo and string.format("%.0f%%", math.max(0, math.min(100, st.cargo))) or "-"
  o.cargoN = st.cargo and math.max(0, math.min(100, st.cargo))
  o.skey = o.status:lower()  -- Idle/Mining/Docking/Undocking/Refueling/Unloading = Img_StatSrc_<key>
  local it = st.item and S.items and S.items.byId and S.items.byId[st.item]
  o.mine = (it and it.text) or st.item or "-"   -- ore name; label "Mining:" + icon are drawn by cardRender
  o.mineIcon = it and it.icon
  o.next = "Next: " .. ((st.next and st.next ~= "") and st.next or "-")
  return st
end
local function shipRows()
  local rows, dist, icons, any = benchShips(), shipDists(), shipIcons(), false
  for _, o in ipairs(rows) do
    shipStatus(o)
    o.dist = dist[o.id]; if o.dist then any = true end
    o.icon = icons[o.id]
  end
  S.distOk = any
  L("shipRows n=" .. #rows .. " dist=" .. tostring(any) .. " icons=" .. tostring(next(icons) ~= nil))
  return rows
end
local function shipRowRender(o)  -- plain: cols/iconSize; card: name/status/skey/cargo(number)/dist(text)/icon/mine/next
  local c = { o.text, o.status or "", o.dist and fmtDist(o.dist) or "-", o.cargo or "" }
  if not S.distOk then table.remove(c, 3) end
  return { cols = c, iconSize = 56, name = o.text, status = o.status, skey = o.skey, cargo = o.cargoN,
    dist = o.dist and fmtDist(o.dist) or nil, icon = o.icon, mine = o.mine, mineIcon = o.mineIcon, next = o.next }
end
-- All ships: side list Pnl_Fleet_<i>. Full rebuild (shipRows: dist; holograms are built once, see shipIcons) every 3 s; between, status/cargo/mine/next only.
function fleetPaint()
  if not S.all then return end
  if not S.fleet or os.clock() - S.fleet.t > 3 then
    local rows = shipRows()
    if S.set then local t = {}; for _, o in ipairs(rows) do if S.set[o.id] then t[#t + 1] = o end end rows = t end
    S.fleet = { t = os.clock(), rows = rows }
  else
    for _, o in ipairs(S.fleet.rows) do shipStatus(o) end
  end
  for i = 1, FN do
    local o = S.fleet.rows[i]
    setVis(w("Pnl_Fleet_" .. i), o and 0 or 1)
    if o then cardRender("Fleet", i, shipRowRender(o), false) end
  end
end
local function titleFor(id)
  for _, o in ipairs(selField("Remote").opts) do if o.id == id then return "Mining Unit - " .. o.text end end
  return "Mining Unit - " .. tostring(id or "?")
end
-- header dropdown pick: the window now controls that ship (tabs, status, actions follow S.sid). Keeps the Ship select's own state.
local function paintAllUi()  -- warning strip + Remote X button amber while All ships
  setVis(w("Pnl_AllWarn"), S.all and 0 or 1)
  if S.all then
    M.setText("Txt_AllWarn", S.set and ("Fleet " .. tostring(S.setName) .. ": any change overrides the settings of all selected ships.")
      or "All ships mode: any change overrides the settings of every ship.")
  end
  udRender()   -- warning off in All ships mode (S.all already set when this runs)
  setVis(w("Pnl_FleetList"), S.all and 0 or 1)
  local b = w("Btn_RemoteAll")
  setVis(b, S.all and 1 or 0)  -- X is only for leaving single-ship mode; pick a ship in the modal to go back
  if b then try("bg RemoteAll", function() b:SetBackgroundColor(S.all and BTN_AMBER or BTN_NORMAL) end) end
end
local function onShipPick(id)
  holdReads()
  S.all = false
  S.set, S.setName = nil, nil
  S.sid = id
  M.lastSid = id
  M.lastAll, M.lastSet, M.lastSetName = false, nil, nil
  M.setText("Txt_Title", titleFor(id))
  S.pend, S.arm, S.estop, S.etaS, S.etaKind = nil, nil, nil, nil, nil
  S.req, S.simple, S.last = nil, nil, nil
  S.q = { rows = {}, shown = {} }
  S.op = { shown = {} }
  shipLoad()
  itemsLoad()
  opsLoad()
  M.setTab(1)
  paintAllUi()
  refresh()
end
-- window controls a ship set (set nil = every bench ship); name = title + Remote field text. First bench ship of the set shows on the pages.
local function applyAll(set, name)
  name = name or "All ships"
  local sid
  for _, o in ipairs(benchShips()) do if not set or set[o.id] then sid = o.id; break end end
  if not sid then return end
  onShipPick(sid)
  S.all, S.set, S.fleet, S.setName = true, set, nil, set and name or nil
  M.lastAll, M.lastSet, M.lastSetName = true, set, name
  M.setText("Txt_Title", "Mining Unit - " .. name)
  M.setText("Txt_SelRemote", name)
  paintAllUi()
  refresh()
end
-- Remote X button: all bench ships
local function setAll() applyAll(nil, "All ships") end

-- multi-select ship picker (Sel_Remote): fleets by FleetTag name, then "No fleet"; a ship in 2 fleets shows in both
local function fleetGroups(rows)
  local names = {}
  local okF, tags = pcall(FindAllOf, "FleetTag")
  for _, t in ipairs(okF and tags or {}) do
    if ok(t) then
      local id = rd(t, "ID")
      local s, n = pcall(function() return t.DisplayName:ToString() end)
      if id ~= nil then names[tostring(id)] = s and n or tostring(id) end
    end
  end
  local byShip = {}
  local okS, ships = pcall(FindAllOf, "DMSPlayerShipObject")
  for _, sh in ipairs(okS and ships or {}) do
    if ok(sh) then
      local id, arr, list = rd(sh, "ID"), rd(sh, "FleetTagIDs"), {}
      if id ~= nil then
        if arr ~= nil then
          local function val(v) if type(v) == "userdata" then local s2, g = pcall(function() return v:get() end) if s2 then v = g end end return tostring(v) end
          pcall(function() arr:ForEach(function(_, e) list[#list + 1] = val(e) end) end)
          if not list[1] then pcall(function() for k = 1, arr:GetArrayNum() do list[#list + 1] = val(arr[k]) end end) end
          print("[MiningUnitUI] fleet ship " .. tostring(id) .. " tags=" .. table.concat(list, ","))
        end
        byShip[tostring(id)] = list
      end
    end
  end
  local groups, gk, none = {}, {}, { key = "none", name = "No fleet", ships = {} }
  for _, o in ipairs(rows) do
    local any, seen = false, {}   -- seen: a ship listing the same tag twice is added once
    for _, tid in ipairs(byShip[o.id] or {}) do
      if names[tid] and not seen[tid] then
        seen[tid] = true
        local g = gk[tid]
        if not g then g = { key = tid, name = names[tid], ships = {} }; gk[tid] = g; groups[#groups + 1] = g end
        g.ships[#g.ships + 1] = o; any = true
      end
    end
    if not any then none.ships[#none.ships + 1] = o end
  end
  table.sort(groups, function(a, b) return a.name:lower() < b.name:lower() end)
  if #none.ships > 0 then groups[#groups + 1] = none end
  return groups
end
-- picker result: ids = ship ids (nil = every ship), name = fleet / "All ships"; one id = single-ship mode
local function pickShips(ids, name)
  if not ids then return applyAll(nil, "All ships") end
  if not name then selField("Remote").id = ids[1]; selPaint("Remote"); return onShipPick(ids[1]) end
  local set = {}; for _, id in ipairs(ids) do set[id] = true end
  applyAll(set, name)
end
local function openMulti()
  local rows = shipRows()
  selField("Remote").opts = rows
  local groups, sel = fleetGroups(rows), { k = "ship", id = S.sid }   -- start on what is selected now
  if S.all and not S.set then sel = { k = "all" }
  elseif S.all then for _, g in ipairs(groups) do if g.name == S.setName then sel = { k = "fleet", key = g.key, name = g.name, ships = g.ships } end end end
  modalSelect{ title = "Select ships", style = "multi", options = rows, rowRender = shipRowRender,
    multi = { groups = groups, fold = {}, rows = rows, sel = sel, onPick = pickShips } }
end

-- ship = block interact (ship object: its ID is selected). nil = F4 path: last picked this session, else first bench ship.
-- Toggle: if the window is open, any open closes it.
function M.open(ship)
  ExecuteInGameThread(function()
    local mode = (ship ~= nil) and "mu" or "f4"
    local wantTab, wantAll, wantSet, wantName = M.lastTab, M.lastAll, M.lastSet, M.lastSetName   -- F4 reopens on the last tab / last committed ship set
    if M.isOpen() then
      local same = (S.mode == mode)
      M.close()
      if same then return end
    end
    if S.root or S.p then M.close() end                 -- stale half-open window
    if not ok(S.cls) then S.cls = nil end                -- class object can be GC'd / stale after docking or level change: reload
    if not S.cls and not M.load() then
      L("WBP not found - opening nothing")
      return
    end
    local p
    try("FindFirstOf DMSPlayerController", function() p = FindFirstOf("DMSPlayerController") end)
    if not ok(p) then L("open: no DMSPlayerController") return end
    local vw, vh = viewport(p)
    local lib = StaticFindObject(LIB)

    local wbp
    if not ok(lib) or not ok(S.cls) then L("open: lib or class invalid, not creating"); S.cls = nil; return end
    try("CreateWidget", function() wbp = lib:Create(p, S.cls, p) end)
    if not ok(wbp) then L("open: CreateWidget fail") return end
    S.root, S.w, S.p, S.ship = wbp, wbp, p, ship
    S.mode, S.t0 = mode, os.clock()
    S.logKey = nil   -- new widgets: repaint the Log tab on the first tick
    -- ship list = bench ships; sid = block's ship if listed, else last picked (F4), else first; empty list = nil
    local list = benchShips()
    local function inList(id) for _, o in ipairs(list) do if o.id == id then return true end end return false end
    local sid = nil
    if ship ~= nil and rd(ship, "ID") ~= nil then sid = tostring(rd(ship, "ID")) end
    if mode == "f4" and not (sid and inList(sid)) or sid == nil then
      sid = (M.lastSid and inList(M.lastSid)) and M.lastSid or (list[1] and list[1].id) or nil
    end
    S.sid = sid
    S.all = false
    S.pend, S.arm, S.estop, S.etaS, S.etaKind = nil, nil, nil, nil, nil
    S.req, S.simple = nil, nil
    L("CreateWidget ok " .. nm(wbp) .. " [" .. nm(wbp, true) .. "] ship id " .. tostring(S.sid))

    -- the instance itself is the viewport widget: AddToViewport FIRST (slot setters need it), then anchors/alignment/position/size
    S.frame = wbp
    try("AddToViewport", function() wbp:AddToViewport(Z) end)
    -- EXPERIMENT: no viewport slot setters (default AddToViewport fills the screen); Panel anchors in the WBP decide the layout
    if USE_SLOT_SETTERS then
      try("anchors", function() wbp:SetAnchorsInViewport({ Minimum = { X = 0.5, Y = 0.5 }, Maximum = { X = 0.5, Y = 0.5 } }) end)
      try("alignment", function() wbp:SetAlignmentInViewport({ X = 0.5, Y = 0.5 }) end)
      try("position", function() wbp:SetPositionInViewport({ X = 0, Y = 0 }, false) end)
      try("desired size", function() wbp:SetDesiredSizeInViewport({ X = vw * 0.7, Y = vh * 0.7 }) end)
    end
    L("size set " .. tostring(vw * 0.7) .. "x" .. tostring(vh * 0.7) .. " inVp=" .. tostring(inVp(wbp))
      .. " desired=" .. (function() local s, d = pcall(function() return wbp:GetDesiredSize() end) return s and v2(d) or "?" end)()
      .. " slot=" .. (function() local s, d = pcall(function() return wbp.Slot end) return s and nm(d, true) or "?" end)())

    S.sel, S.modal = nil, nil
    selField("Remote").empty = "No ship"
    selSet("Remote", list, sid)
    M.setText("Txt_Title", titleFor(sid))
    setVis(w("Pnl_Fleet"), mode == "mu" and 1 or 0)   -- Fleet Control box: F4 only
    setVis(w("Pnl_AllWarn"), 1)
    S.q = { rows = {}, shown = {} }
    S.op = { shown = {} }
    local all = loadItems()
    local byId, min = {}, {}
    for _, it in ipairs(all) do
      byId[it.id] = it
      if isMineable(it.id) then min[#min + 1] = it end
    end
    S.items = { all = all, byId = byId, min = min }

    -- buttons by name (Is Variable in WBP)
    if BIND and BIND.onClick then
      local function bind(n, fn2)
        local b = w(n); if b then try("bind " .. n, function() BIND.onClick(b, fn2) end) end
      end
      for i, n in ipairs(TABS) do bind(n, function() M.setTab(i) end) end
      bind("Btn_Close", M.close)
      bind("Btn_RemoteAll", setAll)
      bind("Btn_LoopStart", function() sendSeq({ "mine_start" }); S.pend = { k = "start", dl = S.tick + 12 }; S.arm = nil; refresh() end)
      bind("Btn_LoopStop", function() sendSeq({ "mine_stop" }); S.pend = { k = "stop", dl = S.tick + 12 }; S.arm = nil; refresh() end)
      bind("Btn_EmergencyStop", function()
        sendSeq({ "stop_all" })                          -- hard stop for THIS ship (lasers/scooper off, jobs cancelled, loop halted); All ships: every ship
        S.arm = nil; S.pend = nil; S.estop = S.tick + 12
        L("emergency stop sent ship=" .. tostring(S.sid))
        refresh()
      end)
      -- one-shot actions (missing widgets are skipped by bind; WBP may not have them yet)
      for _, n in ipairs(ACT_ORDER) do bind(n, function() doAction(n) end) end
      modalInit(bind)
      bind("Sel_Remote", openMulti)
      S.last = S.last or {}
      for _, u in ipairs(UD_ORES) do
        bind("Btn_UDockAdd_" .. u.k, function()
          modalSelect{ title = "Unload dock", options = S.docks or {}, onPick = function(id) table.insert(S.ud[u.ini], id); udSave() end }
        end)
      end
      bind("Btn_RDockAdd", function()
        modalSelect{ title = "Refuel dock", options = S.docks or {}, onPick = function(id) table.insert(S.rd, id); udSave() end }
      end)
      selInit("Belt", bind, function(id) S.last.belt = id; iniSave({ belt_sector = id }); shipFill() end, "Sector Selector", function(o) return { cols = o.cols, ores = o.ores } end, loadSectors, function() return { "Sector", "ID", "Distance" } end)
      bind("Btn_BeltPinned", function()
        local p = rdlines(M.DATA .. "MiningPinned.txt")
        local id = trim((p and p[1]) or "")
        if id == "" or id == "0" then M.setText("Txt_Status", "error: no pinned sector (pin one on the map first)"); return end
        S.last.belt = id; iniSave({ belt_sector = id }); shipFill()
      end)
      bind("Btn_BeltWarp", function() sendCmd("warp_belt") end)  -- single ship (S.sid), not sendSeq: All-ships must not fan out
      M.setBtnText("Btn_BeltWarp", "Warp to sector")
      bind("Btn_ItemAdd", function()
        local rows = S.q.rows
        rows[#rows + 1] = { id = "", amt = "" }; qSave(); qRender()
      end)
      bind("Btn_ApplyGlobal", opDefault)
    else
      L("open: no api.onClick (call M.bind first)")
    end

    -- input focus: UI-only so drags do not reach the game camera (GameAndUI did). Fallback GameAndUIEx.
    local okIn = try("input UIOnlyEx4", function() lib:SetInputMode_UIOnlyEx(p, wbp, 0, false) end)
      or try("input UIOnlyEx2", function() lib:SetInputMode_UIOnlyEx(p, wbp) end)
      or try("input GameAndUIEx5", function() lib:SetInputMode_GameAndUIEx(p, wbp, 0, false, false) end)
      or try("input GameAndUIEx4", function() lib:SetInputMode_GameAndUIEx(p, wbp, 0, false) end)
    L("input mode ok=" .. tostring(okIn))
    try("mouse on", function() p.bShowMouseCursor = true end)
    try("focusable", function() wbp.bIsFocusable = true end)
    try("user focus", function() wbp:SetUserFocus(p) end)
    try("look ignore", function() p:SetIgnoreLookInput(true) end)
    try("move ignore", function() p:SetIgnoreMoveInput(true) end)

    try("shipLoad", shipLoad)
    try("itemsLoad", itemsLoad)
    try("opsLoad", opsLoad)
    M.setTab(1)
    if mode == "f4" then
      if wantAll then applyAll(wantSet, wantName) end
      if wantTab then M.setTab(wantTab) end
      holdReads(); S.noRead = os.clock() + 2.0   -- extra settle time after a restored tab
    end
    refresh()
    startLoop()
    L("open sid=" .. tostring(S.sid) .. " isOpen=" .. tostring(M.isOpen()))
  end)
end

local function typing()
  for _, e in ipairs(FindAllOf("EditableTextBox") or {}) do
    if ok(e) then local s, v = pcall(function() return e:HasKeyboardFocus() end); if s and v then return true end end
  end
  return false
end
function M.fKey()
  if M.isOpen() and S.mode == "mu" and os.clock() - (S.t0 or 0) > 0.5 and not typing() then M.close() end
end

return M
