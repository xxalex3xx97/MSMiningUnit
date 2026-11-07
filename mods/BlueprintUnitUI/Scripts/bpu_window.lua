-- Blueprint Unit window (WBP_BlueprintUnit). API: bind / open(block, player, ctl) / close / isOpen / fKey /
-- impOpen / impClose (import modal) / setCost / devCost.
-- Read-only inventory: Lua never writes an inventory. Import = writes BlueprintImport.json and one MiningCmd.txt line,
-- then polls BlueprintImport.status (and reads BlueprintImport.cost when done).
-- Every game call goes through pcall. Every log line gets the [BPUnitUI] tag. Forward slashes only in paths.

local M = {}

local function L(m) print("[BPUnitUI] " .. m .. "\n") end
local function ok(o) if o == nil or type(o) ~= "userdata" then return false end local s, v = pcall(function() return o:IsValid() end) return s and v == true end
local function nm(o) if not ok(o) then return "?" end local s, v = pcall(function() return o:GetFName():ToString() end) return s and v or "?" end
local function cn(o) if not ok(o) then return "?" end local s, v = pcall(function() return o:GetClass():GetFName():ToString() end) return s and v or "?" end
local function rd(o, p) local s, v = pcall(function() return o[p] end) if s then return v end return nil end
local function try(tag, f, ...) local s, r = pcall(f, ...) if not s then L("FAIL " .. tag .. ": " .. tostring(r)) end return s end
local function inVp(o) local s, v = pcall(function() return o:IsInViewport() end) return s and v == true end

local LIB = "/Script/UMG.Default__WidgetBlueprintLibrary"
local WBP_CLS = "/Game/BlueprintUnitUI/WBP_BlueprintUnit.WBP_BlueprintUnit_C"
local INV_PKG = "/Game/Blueprints/Widgets/Display/Inventory/WBP_Inventory"
local INV_CLS = "/Game/Blueprints/Widgets/Display/Inventory/WBP_Inventory.WBP_Inventory_C"
local Z = 1                                    -- low: the game's cursor item stack (WBP_CursorItemStack, AddToViewport 2) draws over us during drag. F10 dev cycles Z live
local COST_N = 40
local COST_RED = { R = 0.705208, G = 0.031587, B = 0.031587, A = 1 }   -- game's own
local COST_WHITE = { R = 1, G = 1, B = 1, A = 1 }
local COST_GREEN = { R = 0.1, G = 0.6, B = 0.2, A = 1 }
local AMT_PKG = "/Game/Blueprints/Widgets/Display/Inventory/WBP_ItemAmount"   -- game's cost row widget (WBP_ItemTotals rows)
local AMT_CLS = AMT_PKG .. ".WBP_ItemAmount_C"
local HDR_TXT = "▼ Modification Cost"                                         -- designer sets it; Lua only logs a mismatch
local POLL_MS, POLL_MAX_S = 500, 60
local SEQ_MOD = 100000000                      -- MiningCmd seq: the DLL keeps only the last 8 digits
local REM_NAME = { ConveyorBelt = "belt", Splitter = "splitter", Container = "container" }
-- widget names the WBP must provide (contract). Missing ones are logged once per open.
local CONTRACT = { "Panel", "Txt_Title", "Btn_Close", "Txt_Close", "Txt_Status", "Txt_Summary", "Txt_Removes",
  "Btn_Import", "Txt_Import", "Btn_Build", "Txt_Build",
  "Txt_CostHdr", "Scr_Cost", "Txt_CostEmpty", "Txt_UnitInvTitle", "Scr_UnitInv", "Txt_PlayerInvTitle", "Host_PlayerInv",
  "Pnl_ImportModal", "Txt_ImportTitle", "Btn_ImportClose", "Txt_ImportClose", "Edt_ImportJson", "Txt_ImportHint",
  "Btn_ImportOk", "Txt_ImportOk", "Btn_ImportCancel", "Txt_ImportCancel" }
-- optional, never logged if missing: Btn_CostPin, Txt_CostPin. Row_Cost_* are fallback only (checked in setCost).

-- S.w = WBP instance = viewport widget (names resolve here). S.p = controller. S.uw/S.pw = grids. S.tc = last text per name.
-- S.imp = import modal shown. S.poll = { seq, t0, last } while waiting for our import. S.itemMap = name -> Item DA (per open).
local S = { root = nil, w = nil, p = nil, cls = nil, frame = nil, uw = nil, pw = nil, unitInv = nil, playerInv = nil,
  cost = nil, tc = nil, imp = false, poll = nil, itemMap = nil }
local BIND -- {onClick, clear, data} from main.lua
local DATA = "ue4ss/Mods/MiningBenchMod/dlls/"   -- MiningBenchMod/dlls/ (main.lua passes the absolute-ish dir)
local lastSeq = 0                                -- last MiningCmd seq we wrote (module lifetime)
function M.bind(api) BIND = api; DATA = api.data or DATA end

-- ESlateVisibility: 0 Visible, 1 Collapsed
local function setVis(o, shown) if ok(o) then try("vis", function() o:SetVisibility(shown and 0 or 1) end) end end
local function tint(o, col) if ok(o) then try("colour", function() o:SetColorAndOpacity({ SpecifiedColor = col }) end) end end

-- LoadAsset is registry-gated. FAssetData table -> AssetRegistryHelpers:GetAsset (game thread), as in MiningUnitUI.
local ARH
local function getAsset(pkg, name)
  if not ok(ARH) then ARH = StaticFindObject("/Script/AssetRegistry.Default__AssetRegistryHelpers") end
  if not ok(ARH) then L("ARH invalid"); return nil end
  local sf, r = pcall(function() return ARH:GetAsset({ PackageName = FName(pkg), AssetName = FName(name) }) end)
  L("GetAsset " .. pkg .. "." .. name .. " ok=" .. tostring(sf) .. " valid=" .. tostring(sf and ok(r)))
  if sf and ok(r) then return r end
end

-- widget by name: GetWidgetFromName, then the is-variable property
local function w(name)
  if not ok(S.w) then return nil end
  local s, x = pcall(function() return S.w:GetWidgetFromName(FName(name)) end)
  if s and ok(x) then return x end
  local s2, y = pcall(function() return S.w[name] end)
  if s2 and ok(y) then return y end
  return nil
end

-- SetText(FText) on the TextBlock; unchanged writes skipped (S.tc). Never read an edit box right after this.
local function txt(name, s)
  S.tc = S.tc or {}
  if S.tc[name] == s then return end
  local tb = w(name); if not ok(tb) then return end
  if try("SetText " .. name, function() tb:SetText(FText(s)) end) then S.tc[name] = s end
end

-- item display name (same as MiningUnitUI item label: DisplayName, else ITEM_ id without prefix)
local function itemName(it)
  local s, label = pcall(function() return it.DisplayName:ToString() end)
  if s and label and label ~= "" then return label end
  local s1, obj = pcall(function() return it:GetFName():ToString() end)
  local n = (s1 and obj) or "?"
  return (n:gsub("^ITEM_", ""))
end

-- count of a TArray (userdata) or Lua table
local function countOf(a)
  if type(a) == "table" then return #a end
  if type(a) ~= "userdata" then return "?" end
  local s, n = pcall(function() return a:GetArrayNum() end)
  if s and type(n) == "number" then return n end
  return "?"
end

-- item by object name (ITEM_x), FindAllOf('Item') once per open
local function itemFor(name)
  if not S.itemMap then
    S.itemMap = {}
    local s, list = pcall(FindAllOf, "Item")
    for _, it in ipairs((s and type(list) == "table") and list or {}) do
      if ok(it) then
        local n = nm(it)
        if n ~= "?" and not n:find("Default__", 1, true) then S.itemMap[n] = it end
      end
    end
  end
  return S.itemMap[name]
end

-- read-only count: unit + player inventory. GetItemAmount error -> logged once, counts as 0
local amtErr = false
local function haveOf(item)
  local have = 0
  if not item then return 0 end
  for _, inv in ipairs({ S.unitInv, S.playerInv }) do
    if ok(inv) then
      local s, v = pcall(function() return inv:GetItemAmount(item) end)
      if s and type(v) == "number" then have = have + v
      elseif not amtErr then amtErr = true; L("GetItemAmount failed (" .. tostring(v) .. "), using 0") end
    end
  end
  return have
end

-- cost rows: list = array of { name = "ITEM_x", need, refund, net } (net = need - refund). Read-only.
-- net == 0 -> no row. net > 0 -> need row "have/netx" (red when short). net < 0 -> refund row "+N refund" (green).
-- Game's own WBP_ItemAmount rows in Scr_Cost (need rows sorted by name, then refund rows). Own Row_Cost_* = fallback only.
local iconLogged, missLogged = false, {}
local amtFailed, amtCls = {}, nil
local function fmt(n)   -- 1595 -> "1,595"
  local out = tostring(math.floor(n)):reverse():gsub("(%d%d%d)", "%1,"):reverse()
  return (out:gsub("^,", ""))
end
local function amtStep(step, f)   -- non-fatal step, logged once per step name
  local s, e = pcall(f)
  if not s and not amtFailed[step] then amtFailed[step] = true; L("cost row step " .. step .. " failed: " .. tostring(e)) end
  return s
end
local function amtClass()
  if not ok(amtCls) then amtCls = StaticFindObject(AMT_CLS) end
  if not ok(amtCls) then amtCls = getAsset(AMT_PKG, "WBP_ItemAmount_C") end
  return ok(amtCls) and amtCls or nil
end
-- one WBP_ItemAmount row: SetItemStack, then our amount text + colour. Returns false only if the row could not be added.
local function amtRow(host, cls, p, item, amount, text, col)
  local row
  if not amtStep("create", function() row = StaticFindObject(LIB):Create(p, cls, p) end) or not ok(row) then return false end
  amtStep("set", function() row:SetItemStack({ Item = item, Amount = amount }, nil) end)
  amtStep("text", function() row.AmountText:SetText(FText(text)) end)
  amtStep("colour", function() row.AmountText:SetColour({ SpecifiedColor = col, ColorUseRule = 0 }) end)
  pcall(function() row.WBP_Item:SetIconSize(40) end)
  return amtStep("add", function() host:AddChild(row) end)
end
-- fills Scr_Cost with game rows. false = a row could not be added (caller falls back)
local function fillGame(host, cls, p, need, refund)
  if not pcall(function() host:ClearChildren() end) then L("cost: ClearChildren failed"); return false, 0 end   -- also drops leftover designer rows
  local shown = 0
  for pass = 1, 2 do
    for _, e in ipairs(pass == 1 and need or refund) do
      local item = itemFor(e.name)
      if not item then
        if not missLogged[e.name] then missLogged[e.name] = true; L("cost item not found: " .. e.name) end
      elseif shown < COST_N then
        local okr
        if pass == 1 then
          local have = math.floor(haveOf(item))
          okr = amtRow(host, cls, p, item, e.net, fmt(have) .. "/" .. fmt(e.net) .. "x", have < e.net and COST_RED or COST_WHITE)
        else
          okr = amtRow(host, cls, p, item, -e.net, "+" .. fmt(-e.net) .. " refund", COST_GREEN)
        end
        if not okr then return false, shown end
        shown = shown + 1
      end
    end
  end
  return true, shown
end

local setCostLegacy -- defined below
function M.setCost(list)
  local need, refund = {}, {}
  for _, e in ipairs(list or {}) do
    if type(e) == "table" and type(e.name) == "string" and (e.net or 0) ~= 0 then
      if e.net > 0 then need[#need + 1] = e else refund[#refund + 1] = e end
    end
  end
  local byName = function(a, b) return a.name:lower() < b.name:lower() end
  table.sort(need, byName); table.sort(refund, byName)
  local rows = {}
  for _, e in ipairs(need) do rows[#rows + 1] = e end
  for _, e in ipairs(refund) do rows[#rows + 1] = e end
  if #rows > COST_N then L("cost rows truncated " .. (#rows - COST_N)) end
  S.cost = rows
  local host, cls = w("Scr_Cost"), amtClass()
  local legacy = ok(w("Row_Cost_1"))
  local done, shown = false, 0
  if ok(host) and cls and ok(S.p) then done, shown = fillGame(host, cls, S.p, need, refund) end
  if not done then
    if legacy then shown = setCostLegacy(rows)
    else L("cost rows: WBP_ItemAmount row failed (see step log above), no Row_Cost fallback: nothing shown"); shown = 0 end
  end
  setVis(w("Txt_CostEmpty"), shown == 0)
  L("cost rows " .. shown)
end

-- fallback: the old own rows (Row_Cost_* in the WBP). Only used when the game row cannot be built.
setCostLegacy = function(rows)
  local shown = math.min(#rows, COST_N)
  for i = 1, COST_N do
    local row = w("Row_Cost_" .. i)
    local e = (i <= shown) and rows[i] or nil
    if e then
      local item = itemFor(e.name)
      if not item and not missLogged[e.name] then missLogged[e.name] = true; L("cost item not found: " .. e.name) end
      local net = e.net or 0
      local label = item and itemName(item) or (e.name:gsub("^ITEM_", ""))
      setVis(row, true)
      txt("Txt_CostName_" .. i, label)
      local have = math.floor(haveOf(item))
      local hv = w("Txt_CostHave_" .. i)
      local rf = w("Txt_CostRefund_" .. i)
      if net > 0 then
        txt("Txt_CostHave_" .. i, string.format("%d/%dx", have, math.floor(net)))
        tint(hv, have < net and COST_RED or COST_WHITE)
        setVis(rf, false)
      else
        txt("Txt_CostHave_" .. i, "needed 0")
        tint(hv, COST_WHITE)
        txt("Txt_CostRefund_" .. i, string.format("+%d refund", math.floor(-net)))
        tint(rf, COST_GREEN)
        setVis(rf, true)
      end
      local img = w("Img_Cost_" .. i)
      if item and ok(img) then try("cost icon " .. i, function() img:SetBrushFromTexture(item.Icon, false) end) end
      if item and not iconLogged then
        iconLogged = true
        local s, ic = pcall(function() return item.Icon end)
        L("P5 Item.Icon class=" .. ((s and ok(ic)) and cn(ic) or tostring(s and ic or "nil")))
      end
    else
      setVis(row, false)
    end
  end
  return shown
end

-- DEV: 3 fake cost entries from the first Item DAs (need 5 / 50 / 10, refund 0 / 0 / 12 -> last one shows refund)
function M.devCost()
  local names = {}
  local s, list = pcall(FindAllOf, "Item")
  for _, it in ipairs((s and type(list) == "table") and list or {}) do
    if #names == 3 then break end
    if ok(it) then
      local n = nm(it)
      if n ~= "?" and not n:find("Default__", 1, true) then names[#names + 1] = n end
    end
  end
  local needs, refunds = { 5, 50, 10 }, { 0, 0, 12 }
  local out = {}
  for i, n in ipairs(names) do out[i] = { name = n, need = needs[i], refund = refunds[i], net = needs[i] - refunds[i] } end
  L("dev cost: " .. #out .. " entries")
  M.setCost(out)
end

-- one WBP_Inventory grid: Columns / ShowSortButton before setup (pcall: ignore if property missing)
local function mkGrid(cls, p, host, inv, other)
  local g
  try("CreateGrid " .. host, function() g = StaticFindObject(LIB):Create(p, cls, p) end)
  if not ok(g) then L("P3: could not create WBP_Inventory for " .. host .. " (class " .. nm(cls) .. ")"); return nil end
  pcall(function() g.Columns = 8 end)
  pcall(function() g.ShowSortButton = true end)
  local h = w(host)
  if not ok(h) then L("host " .. host .. " missing"); return g end
  try("AddChild " .. host, function() h:AddChild(g) end)
  if ok(inv) and ok(other) then try("SetupInventory " .. host, function() g:SetupInventory(inv, other, p) end)
  else L("setup skipped " .. host .. ": inventory invalid") end
  return g
end

local function localCtl()
  for _, c in ipairs(FindAllOf("DMSPlayerController") or {}) do
    if ok(c) then
      local sl, loc = pcall(function() return c:IsLocalPlayerController() end)
      if sl and loc then return c end
    end
  end
  return nil
end

-- typing in an edit box: F does not close the window then
local function typing()
  for _, e in ipairs(FindAllOf("EditableTextBox") or {}) do
    if ok(e) then local s, v = pcall(function() return e:HasKeyboardFocus() end); if s and v then return true end end
  end
  return false
end

function M.isOpen() return ok(S.frame) and inVp(S.frame) end

-- import modal state (Lua flag + window valid)
function M.impOpen() return S.imp == true and ok(S.w) end

-- ---------------------------------------------------------------------------------------------
-- Import: BlueprintImport.json (text), MiningCmd.txt line "<seq>|0|bp_import <abs path>", BlueprintImport.status / .cost
-- ---------------------------------------------------------------------------------------------
-- forward slashes, '..' segments collapsed (DATA is built from the script dir + ../../)
local function normPath(p)
  p = p:gsub("\\", "/")
  local prev
  repeat prev = p; p = p:gsub("[^/]+/%.%./", "", 1) until p == prev
  return (p:gsub("/%./", "/"))
end
local function dataPath(name) return normPath(DATA .. name) end

local function writeFile(path, data)
  local fh, err = io.open(path, "wb")
  if not fh then return false, err end
  local okw, werr = fh:write(data)
  fh:close()
  return okw ~= nil, werr
end

-- MiningCmd.txt: one line, no newline. tag "build " for bp_build. false = write failed
local function sendCmd(line, tag)
  local cok, cerr = writeFile(dataPath("MiningCmd.txt"), line)
  if not cok then L((tag or "") .. "MiningCmd.txt write failed " .. tostring(cerr)); return false end
  L((tag or "") .. "cmd " .. line)
  return true
end

-- seq: below 1e8 (DLL keeps last 8 digits), strictly above: clock (unix s), last one we wrote, and the one in MiningCmd.txt
local function nextSeq()
  local fileSeq = 0
  local fh = io.open(dataPath("MiningCmd.txt"), "rb")
  if fh then
    local line = fh:read("l")
    fh:close()
    fileSeq = tonumber(line and line:match("^(%d+)|") or "") or 0
  end
  local fileSeq8 = fileSeq % SEQ_MOD
  local clock = os.time() % SEQ_MOD
  if clock <= fileSeq8 then
    L("[BPUnitImport] clock seq " .. clock .. " <= MiningCmd.txt seq " .. fileSeq8 .. " (DLL ignores seq <= last): bumped")
  end
  lastSeq = math.max(clock, lastSeq + 1, fileSeq8 + 1)
  if lastSeq >= SEQ_MOD then L("[BPUnitImport] seq " .. lastSeq .. " >= 1e8: DLL keeps last 8 digits, may be ignored") end
  return lastSeq
end

-- "BP_ContainerObject" -> "container", "BP_ContainerObject_2" -> "large container", others as before
local function prettyName(raw)
  local n = raw:gsub("_C$", ""):gsub("^BP_", "")
  local base, num = n:match("^(.-)_(%d+)$")
  if base and tonumber(num) >= 2 and base:gsub("Object$", "") == "Container" then return "large container" end
  n = n:gsub("Object$", "")
  return REM_NAME[n] or n:lower()
end

local function prettyRemoves(s)
  local out = {}
  for part in s:gmatch("[^,]+") do
    local raw, cnt = part:match("^(.-):(%d+)$")
    if raw then
      local n = prettyName(raw)
      local c = tonumber(cnt)
      out[#out + 1] = c .. " " .. (c == 1 and n or (n .. "s"))
    end
  end
  return table.concat(out, ", ")
end

-- BlueprintImport.cost body: no header, lines ITEM_Name|need|refund|net (integers). Returns list, count of malformed lines.
local function parseCost(body)
  local list, bad = {}, 0
  for line in body:gmatch("[^\r\n]+") do
    local name, need, refund, net = line:match("^([^|]+)|(%-?%d+)|(%-?%d+)|(%-?%d+)$")
    if name then
      list[#list + 1] = { name = name, need = tonumber(need), refund = tonumber(refund), net = tonumber(net) }
    else
      bad = bad + 1
    end
  end
  return list, bad
end

local function readCost()
  local fh = io.open(dataPath("BlueprintImport.cost"), "rb")
  if not fh then L("cost file missing"); M.setCost({}); return end
  local body = fh:read("a") or ""
  fh:close()
  local list, bad = parseCost(body)
  if bad > 0 then L("cost: skipped " .. bad .. " malformed lines") end
  M.setCost(list)
end

-- results area: summary, removes, cost rows back to empty
local function resetResults()
  txt("Txt_Summary", ""); setVis(w("Txt_Summary"), false)
  txt("Txt_Removes", ""); setVis(w("Txt_Removes"), false)
  M.setCost({})
end

-- BlueprintImport.status body: line 1 "state|text", then key=value lines. Returns state, text, kv
local function parseStatus(body)
  local lines = {}
  for line in body:gmatch("[^\r\n]+") do lines[#lines + 1] = line end
  local st, text = (lines[1] or ""):match("^([^|]*)|?(.*)$")
  local kv = {}
  for i = 2, #lines do
    local k, v = lines[i]:match("^(%w+)=(.*)$")
    if k then kv[k] = v end
  end
  return st or "", text or "", kv
end

-- nil if no file. name = status file (default BlueprintImport.status)
local function readStatus(name)
  local fh = io.open(dataPath(name or "BlueprintImport.status"), "rb")
  if not fh then return nil end
  local body = fh:read("a") or ""
  fh:close()
  return parseStatus(body)
end

-- status seq vs sent seq: equal modulo 1e8 (the DLL keeps the last 8 digits)
local function seqMatch(statusSeq, importSeq)
  local s = tonumber(statusSeq)
  return s ~= nil and s % SEQ_MOD == importSeq % SEQ_MOD
end

local pollTick -- forward: schedulePoll and pollTick call each other
local function schedulePoll()
  pcall(ExecuteWithDelay, POLL_MS, function() pcall(ExecuteInGameThread, pollTick) end)
end

local imported = false                 -- a done import happened (module lifetime): Build needs one first
local function showSummary(text) txt("Txt_Summary", text); setVis(w("Txt_Summary"), text ~= "") end
-- finished build line (S.lastBuild) goes above the import summary, consumed once
local function buildSummary(extra)
  local b = S.lastBuild; S.lastBuild = nil
  if not b then return extra end
  return extra ~= "" and (b .. "\n" .. extra) or b
end

local function showDone(text, kv)
  imported = true
  local bp = kv.blueprint or ""
  txt("Txt_Status", bp ~= "" and ("Blueprint: " .. bp) or "Ready")
  showSummary(buildSummary(text))
  local pretty = ((tonumber(kv.remove) or 0) > 0) and prettyRemoves(kv.removes or "") or ""
  txt("Txt_Removes", pretty ~= "" and ("removes: " .. pretty) or "")
  setVis(w("Txt_Removes"), pretty ~= "")
  readCost()
end

pollTick = function()
  local p = S.poll
  if not p then return end
  if not ok(S.w) then S.poll = nil; return end
  if os.time() - p.t0 > POLL_MAX_S then
    S.poll = nil
    txt("Txt_Status", "error: timeout")
    if S.lastBuild then showSummary(buildSummary("")) end
    L("import timeout seq=" .. p.seq)
    return
  end
  local st, text, kv = readStatus()
  if st and kv and kv.seq and not seqMatch(kv.seq, p.seq) and not p.misLogged[kv.seq] then
    p.misLogged[kv.seq] = true
    L("[BPUnitImport] status seq " .. kv.seq .. " != import_seq " .. p.seq .. " (ignored)")
  end
  if st and kv and kv.seq and seqMatch(kv.seq, p.seq) then
    local line = st .. "|" .. text
    if line ~= p.last then p.last = line; L("import status seq=" .. p.seq .. " " .. line) end
    if st == "done" then
      S.poll = nil
      showDone(text, kv)
    elseif st == "error" then
      S.poll = nil
      txt("Txt_Status", "error: " .. text)
      if S.lastBuild then showSummary(buildSummary("")) end
    elseif st == "working" then
      txt("Txt_Status", text ~= "" and text or "Importing...")
    end
  end
  if S.poll == p then schedulePoll() end
end

-- Btn_Import: open the modal, keep previous text, focus the box
local function impOpenModal()
  if not ok(S.w) or S.build then return end   -- no import while a build runs
  S.imp = true
  setVis(w("Pnl_ImportModal"), true)
  local box = w("Edt_ImportJson")
  if ok(box) then try("import focus", function() box:SetKeyboardFocus() end) end
end

-- X and ESC: close the modal, keep the text
function M.impClose()
  S.imp = false
  setVis(w("Pnl_ImportModal"), false)
end

-- Cancel: clear the box, close the modal
local function impCancel()
  if not M.impOpen() then return end
  local box = w("Edt_ImportJson")
  if ok(box) then try("import clear", function() box:SetText(FText("")) end) end
  M.impClose()
end

-- Import: read the box once, write the file atomically, send the command, poll
local function impOk()
  if not M.impOpen() then return end
  local box = w("Edt_ImportJson")
  if not ok(box) then L("import: text box missing"); txt("Txt_Status", "error: text box missing"); return end
  local s, t = pcall(function() return box:GetText():ToString() end)
  if not s or type(t) ~= "string" then L("import: text read failed"); txt("Txt_Status", "error: could not read text"); return end
  if t == "" then txt("Txt_Status", "paste the blueprint JSON first"); return end

  local target = dataPath("BlueprintImport.json")
  local tmp = target .. ".tmp"
  local wok, werr = writeFile(tmp, t)
  if not wok then L("import: tmp write failed " .. tostring(werr)); txt("Txt_Status", "error: write failed"); return end
  os.remove(target)
  local rok, rerr = os.rename(tmp, target)
  if not rok then
    L("import: rename failed (" .. tostring(rerr) .. "), writing target directly")
    os.remove(tmp)
    wok, werr = writeFile(target, t)
    if not wok then L("import: write failed " .. tostring(werr)); txt("Txt_Status", "error: write failed"); return end
  end
  L("import text " .. #t .. " bytes written")

  M.impClose()
  txt("Txt_Status", "Importing...")
  resetResults()

  local seq = nextSeq()
  if not sendCmd(seq .. "|0|bp_import " .. target) then txt("Txt_Status", "error: command write failed"); return end
  S.poll = { seq = seq, t0 = os.time(), last = nil, misLogged = {} }
  schedulePoll()
end

function M.close()
  if BIND and BIND.clear then BIND.clear() end  -- drop button callbacks + global layout hook
  S.cost, S.tc, S.itemMap = nil, nil, nil
  S.poll, S.imp, S.build = nil, false, nil   -- closing stops the build poll (DLL keeps running)
  if not (S.root or S.p or S.frame) then return end
  if M.closing and os.clock() - (M.closing_at or 0) < 3 then return end   -- already closing: idempotent
  M.closes = (M.closes or 0) + 1
  M.closing, M.closing_at = true, os.clock()
  local root, p, ep, n = S.root, S.p, M.opens or 0, M.closes
  local uw, pw = S.uw, S.pw
  S.root, S.p, S.w, S.frame, S.uw, S.pw = nil, nil, nil, nil, nil, nil
  S.unitInv, S.playerInv = nil, nil
  -- runs on a later tick, outside the caller (button / key) stack
  local function teardown()
    -- MANDATORY ORDER: grid cleanup (removes delegates) first, then RemoveFromParent, then input restore
    if ok(uw) then pcall(function() uw:CleanupInventory() end) end
    if ok(pw) then pcall(function() pw:CleanupInventory() end) end
    if ok(root) then try("RemoveFromParent", function() root:RemoveFromParent() end) end
    -- input restore only if no window was opened since this close
    if ok(p) and ep == (M.opens or 0) then
      try("look unignore", function() p:SetIgnoreLookInput(false) end)
      try("move unignore", function() p:SetIgnoreMoveInput(false) end)
      try("mouse off", function() p.bShowMouseCursor = false end)
      try("input GameOnly", function() StaticFindObject(LIB):SetInputMode_GameOnly(p, false) end)
    end
    M.closing = false
    L("closed n=" .. tostring(n) .. " opens=" .. tostring(M.opens or 0) .. " closes=" .. tostring(M.closes))
  end
  if not pcall(ExecuteWithDelay, 50, function() pcall(ExecuteInGameThread, teardown) end) then teardown() end
end

-- F: closes the window, except while the import modal is open (typing) or an edit box has focus
function M.fKey()
  if M.impOpen() then return end
  if M.isOpen() and os.clock() - (S.t0 or 0) > 0.5 and not typing() then M.close() end
end

-- fixed centred panel (Station Console size). Slate units = viewport px / DPI scale, clamped to 90% of the viewport.
-- Runs AFTER AddToViewport: anchors/alignment/position/size are slot setters (silent no-op before the slot exists).
local LIB_LAYOUT = "/Script/UMG.Default__WidgetLayoutLibrary"
local WIN_W, WIN_H, WIN_FRAC = 1200, 900, 0.9
-- false: the WBP owns the size (full-screen Root, centred fixed 1200x900 SizeBox). Lua calls below are not run.
local USE_VIEWPORT_SIZING = false
local function sizeCall(tag, f)
  local s, e = pcall(f)
  L("size " .. tag .. " ok=" .. tostring(s) .. (s and "" or (" err=" .. tostring(e))))
  return s
end
local function sizeWindow(wbp, p)
  local vw, vh, sc = nil, nil, 1
  local okv, v = pcall(function() return StaticFindObject(LIB_LAYOUT):GetViewportSize(p) end)
  if okv and type(v) == "userdata" then
    local x, y = rd(v, "X"), rd(v, "Y")
    if type(x) == "number" and type(y) == "number" and x > 0 and y > 0 then vw, vh = x, y end
  end
  local oks, sv = pcall(function() return StaticFindObject(LIB_LAYOUT):GetViewportScale(p) end)
  if oks and type(sv) == "number" and sv > 0 then sc = sv end
  L("size GetViewportSize ok=" .. tostring(okv) .. " type=" .. type(v) .. " GetViewportScale ok=" .. tostring(oks) .. " scale=" .. tostring(sc))
  if not vw then vw, vh = 3440, 1440; L("size: viewport fallback 3440x1440") end
  local sw, sh = vw / sc, vh / sc
  local w, h = math.min(WIN_W, WIN_FRAC * sw), math.min(WIN_H, WIN_FRAC * sh)
  L(string.format("size: viewport px %.0fx%.0f dpi=%.3f slate %.0fx%.0f -> window %.0fx%.0f", vw, vh, sc, sw, sh, w, h))
  sizeCall("anchors", function() wbp:SetAnchorsInViewport({ Minimum = { X = 0.5, Y = 0.5 }, Maximum = { X = 0.5, Y = 0.5 } }) end)
  sizeCall("alignment", function() wbp:SetAlignmentInViewport({ X = 0.5, Y = 0.5 }) end)
  sizeCall("position", function() wbp:SetPositionInViewport({ X = 0, Y = 0 }, false) end)
  sizeCall("desired", function() wbp:SetDesiredSizeInViewport({ X = w, Y = h }) end)
  local sd, d = pcall(function() return wbp:GetDesiredSize() end)
  L("size final desired ok=" .. tostring(sd) .. " x=" .. tostring(sd and rd(d, "X")) .. " y=" .. tostring(sd and rd(d, "Y")) .. " inVp=" .. tostring(inVp(wbp)))
end

-- geometry probe (open + 200 ms): real layout numbers, to compare with the expected centred 1200x900 slate frame.
-- Values are "XxY" or "n/a" (pcall'd). One line.
local function v2(s, v) if s and type(v) == "userdata" then return tostring(rd(v, "X")) .. "x" .. tostring(rd(v, "Y")) end return "n/a" end
local function geomProbe(wbp, p)
  if S.w ~= wbp or not ok(wbp) then L("geometry skipped: window closed"); return end
  local sv, v = pcall(function() return StaticFindObject(LIB_LAYOUT):GetViewportSize(p) end)
  local ss, sc = pcall(function() return StaticFindObject(LIB_LAYOUT):GetViewportScale(p) end)
  local sd, d = pcall(function() return wbp:GetDesiredSize() end)
  local sg, g = pcall(function() return wbp:GetCachedGeometry() end)
  local ga, gl = nil, nil
  if sg and type(g) == "userdata" then
    local sa, a = pcall(function() return g:GetAbsoluteSize() end); ga = v2(sa, a)
    local sl, l = pcall(function() return g:GetLocalSize() end); gl = v2(sl, l)
  end
  L("geometry desired=" .. v2(sd, d) .. " cachedAbs=" .. (ga or "n/a") .. " cachedLocal=" .. (gl or "n/a")
    .. " viewport=" .. v2(sv, v) .. " dpi=" .. tostring(ss and type(sc) == "number" and sc or "n/a")
    .. " want=" .. WIN_W .. "x" .. WIN_H .. " inVp=" .. tostring(inVp(wbp)))
end

-- P4 probe (open time only): why no container matched. ContainerObject classes, machines whose ParentID == block ID or frame ID (cap 30 lines).
local function diagContainers(bid, fid)
  local okL, all = pcall(FindAllOf, "ContainerObject")
  all = (okL and type(all) == "table") and all or {}
  local byCls, nC = {}, 0
  for _, c in ipairs(all) do if ok(c) then nC = nC + 1; local k = cn(c); byCls[k] = (byCls[k] or 0) + 1 end end
  local parts = {}
  for k, n in pairs(byCls) do parts[#parts + 1] = k .. " x" .. n end
  L("P4 ContainerObject instances=" .. nC .. " [" .. table.concat(parts, ", ") .. "]")
  L("P4 match rule: cls == BP_BPUnitStoreObj_01_C and (ParentID == block ID " .. tostring(bid) .. " or ParentID == frame ID " .. tostring(fid) .. ") or same GetFrame")
  local okM, ms = pcall(FindAllOf, "MachineObject")
  ms = (okM and type(ms) == "table") and ms or {}
  local shown, nM, nMatch = 0, 0, 0
  for _, m in ipairs(ms) do
    if ok(m) then
      nM = nM + 1
      local pid = rd(m, "ParentID")
      if pid ~= nil and (pid == bid or (fid ~= nil and pid == fid)) then
        nMatch = nMatch + 1
        if shown < 30 then
          shown = shown + 1
          L("P4 machine cls=" .. cn(m) .. " ID=" .. tostring(rd(m, "ID")) .. " ParentID=" .. tostring(pid))
        end
      end
    end
  end
  L("P4 MachineObject instances=" .. nM .. " matching block/frame=" .. nMatch .. " shown=" .. shown)
end

-- DEV (F10): cycle the window's Z live. Re-add only: no close/teardown. Only while open.
local ZCYC, zIdx = { 1, 0, 49 }, 1
function M.zCycle()
  local wbp = S.root
  if not M.isOpen() or not ok(wbp) then return end
  zIdx = zIdx % #ZCYC + 1
  local n = ZCYC[zIdx]
  pcall(function() wbp:RemoveFromParent() end)
  if not pcall(function() wbp:AddToViewport(n) end) then L("zorder FAIL re-add") return end
  L("zorder " .. tostring(n))
end

-- DEV (F11): live shift probe. Repeats every 500 ms while on; stops on second F11 or when the window closes.
-- Key form: FKey struct first, Key library constant second. First form that answers is logged.
local SHIFT_FORM, shiftOn = nil, false
local function keyDown(p, name)
  local s, v = pcall(function() return p:IsInputKeyDown({ KeyName = FName(name) }) end)
  if s and type(v) == "boolean" then if not SHIFT_FORM then SHIFT_FORM = "struct"; L("shift probe key form = struct") end return v end
  s, v = pcall(function() return p:IsInputKeyDown(Key[name]) end)
  if s and type(v) == "boolean" then if not SHIFT_FORM then SHIFT_FORM = "const"; L("shift probe key form = const") end return v end
  return nil
end
local function shiftTick()
  if not M.isOpen() or not ok(S.p) then L("shift probe stopped: window closed"); shiftOn = false; return end
  local p = S.p
  L(string.format("shift probe LeftShift=%s RightShift=%s cursor=%s", tostring(keyDown(p, "LeftShift")),
    tostring(keyDown(p, "RightShift")), tostring(rd(p, "bShowMouseCursor"))))
end
function M.shiftToggle()
  shiftOn = not shiftOn
  L("shift probe " .. (shiftOn and "on" or "off"))
  if not shiftOn then return end
  pcall(LoopAsync, 500, function()
    if not shiftOn then return true end   -- true = stop the loop
    pcall(ExecuteInGameThread, function() pcall(shiftTick) end)
    return false
  end)
end

-- DEV (F12): cycle input-mode variants on the live window. Each press logs the variant name. No teardown.
local VARIANTS = {
  { "1 UIOnlyEx(p,wbp,0,false)", function(ui, p, w) ui:SetInputMode_UIOnlyEx(p, w, 0, false) end },
  { "2 GameAndUIEx(p,wbp,0,true,false)", function(ui, p, w) ui:SetInputMode_GameAndUIEx(p, w, 0, true, false) end },
  { "3 GameAndUIEx(p,wbp,0,false,false)", function(ui, p, w) ui:SetInputMode_GameAndUIEx(p, w, 0, false, false) end },
  { "4 GameAndUIEx(p,nil,0,true,false)", function(ui, p, w) ui:SetInputMode_GameAndUIEx(p, nil, 0, true, false) end },
}
local varIdx = 0
function M.inputCycle()
  if not M.isOpen() or not ok(S.root) then return end
  varIdx = varIdx % #VARIANTS + 1
  local v = VARIANTS[varIdx]
  local s, e = pcall(v[2], StaticFindObject(LIB), S.p, S.root)
  L("input variant " .. v[1] .. " ok=" .. tostring(s) .. (s and "" or (" err=" .. tostring(e))))
end

-- ---------------------------------------------------------------------------------------------
-- Crate hide. Our container = sim BP_BPUnitStoreObj_01_C, ParentID = unit block ID or frame ID.
-- Its actor = BP_Container_2_C with BPC_MachineInteraction.MachineObject == that sim (match by ID, class checked).
-- Exactly one link -> hide actor + its interaction (no collision, hidden). Anything else: log once, hide nothing.
-- ---------------------------------------------------------------------------------------------
local CRATE_SIM, CRATE_ACT = "BP_BPUnitStoreObj_01_C", "BP_Container_2_C"
local crateDone, crateLogged = {}, {}   -- crateDone[simID|actor name] = true once hidden
local function vstr(a)
  local s, v = pcall(function() return a:K2_GetActorLocation() end)
  if not s or type(v) ~= "userdata" then return "?" end
  return tostring(rd(v, "X")) .. "," .. tostring(rd(v, "Y")) .. "," .. tostring(rd(v, "Z"))
end
-- sim IDs of unit blocks and their frames (block arg optional; every BPO_BlueprintUnit_C in the world is added)
local function unitIds(block)
  local set = {}
  local function add(b)
    if not ok(b) then return end
    local id = rd(b, "ID"); if id ~= nil then set[id] = true end
    local s, fr = pcall(function() return b:GetFrame() end)
    if s and ok(fr) and rd(fr, "ID") ~= nil then set[rd(fr, "ID")] = true end
  end
  add(block)
  local s, list = pcall(FindAllOf, "BPO_BlueprintUnit_C")
  if s and type(list) == "table" then for _, b in ipairs(list) do add(b) end end
  return set
end
-- BP_Container_2_C actors whose MachineObject is this sim (ID equal, class BP_BPUnitStoreObj_01_C)
local function linkedActors(sid)
  local hits = {}
  local s, list = pcall(FindAllOf, CRATE_ACT)
  for _, a in ipairs((s and type(list) == "table") and list or {}) do
    if ok(a) then
      local comp = rd(a, "BPC_MachineInteraction")
      local mo = ok(comp) and rd(comp, "MachineObject") or nil
      if ok(mo) and rd(mo, "ID") == sid and cn(mo) == CRATE_SIM then hits[#hits + 1] = a end
    end
  end
  return hits
end
local function hideCrate(a, sid)
  try("crate hide", function() a:SetActorHiddenInGame(true) end)
  try("crate no collision", function() a:SetActorEnableCollision(false) end)
  local comp = rd(a, "BPC_MachineInteraction")   -- the interaction component: 'Press F to Access Container' prompt
  if ok(comp) then
    try("crate interact off", function() comp:SetCollisionEnabled(0) end)   -- ECollisionEnabled::NoCollision
    try("crate interact hidden", function() comp:SetVisibility(false, true) end)
  end
  L("crate hidden ID=" .. tostring(sid) .. " actor=" .. nm(a))
end
-- block may be nil (world scan). Cheap when no crate sim exists.
function M.hideCrates(block)
  local s, sims = pcall(FindAllOf, CRATE_SIM)
  if not s or type(sims) ~= "table" or #sims == 0 then return end
  local ids = unitIds(block)
  for _, sim in ipairs(sims) do
    if ok(sim) then
      local sid, pid = rd(sim, "ID"), rd(sim, "ParentID")
      if sid ~= nil and pid ~= nil and ids[pid] then
        local hits = linkedActors(sid)
        if #hits == 1 then
          local key = tostring(sid) .. "|" .. nm(hits[1])
          if not crateDone[key] then crateDone[key] = true; hideCrate(hits[1], sid) end
        elseif not crateLogged[sid] then
          crateLogged[sid] = true
          L("crate link unknown ID=" .. tostring(sid) .. " parent=" .. tostring(pid) .. " actors=" .. #hits .. " (nothing hidden)")
        end
      end
    end
  end
end

-- DEV (F8): log every crate sim, its link actor, every BP_Container_2_C actor and unit block. Nothing changed.
function M.crateDebug()
  local s, sims = pcall(FindAllOf, CRATE_SIM)
  sims = (s and type(sims) == "table") and sims or {}
  local ids = unitIds(nil)
  L("F8 crate sims=" .. #sims)
  for _, sim in ipairs(sims) do
    if ok(sim) then
      local sid, pid = rd(sim, "ID"), rd(sim, "ParentID")
      local names = {}
      for _, a in ipairs(linkedActors(sid)) do names[#names + 1] = nm(a) .. " @" .. vstr(a) end
      L("F8 sim cls=" .. cn(sim) .. " ID=" .. tostring(sid) .. " ParentID=" .. tostring(pid) .. " ours=" .. tostring(ids[pid] == true)
        .. " actors=" .. (#names > 0 and table.concat(names, "; ") or "none"))
    end
  end
  local okA, acts = pcall(FindAllOf, CRATE_ACT)
  for _, a in ipairs((okA and type(acts) == "table") and acts or {}) do
    if ok(a) then
      local comp = rd(a, "BPC_MachineInteraction")
      local mo = ok(comp) and rd(comp, "MachineObject") or nil
      L("F8 actor " .. nm(a) .. " @" .. vstr(a) .. " machine=" .. (ok(mo) and (cn(mo) .. " ID=" .. tostring(rd(mo, "ID")) .. " ParentID=" .. tostring(rd(mo, "ParentID"))) or "none"))
    end
  end
  local okU, units = pcall(FindAllOf, "BPO_BlueprintUnit_C")
  for _, b in ipairs((okU and type(units) == "table") and units or {}) do
    if ok(b) then
      local fr = nil
      pcall(function() fr = b:GetFrame() end)
      L("F8 unit ID=" .. tostring(rd(b, "ID")) .. " frameID=" .. tostring(ok(fr) and rd(fr, "ID") or "?"))
    end
  end
end

-- DEV (F7): one game WBP_ItemAmount row with the first item, each step logged. Add to Scr_Cost only if the window is open.
function M.probeCost()
  local function P(step, v) L("probeCost " .. step .. " " .. tostring(v)) end
  local cls = amtClass()
  P("class found", ok(cls))
  if not cls then return end
  local item
  local s, list = pcall(FindAllOf, "Item")
  for _, it in ipairs((s and type(list) == "table") and list or {}) do
    if not item and ok(it) and nm(it) ~= "?" and not nm(it):find("Default__", 1, true) then item = it end
  end
  P("item found", ok(item) and nm(item) or false)
  if not item then return end
  local p = ok(S.p) and S.p or localCtl()
  local row
  local okC = pcall(function() row = StaticFindObject(LIB):Create(p, cls, p) end)
  P("create", okC and ok(row))
  if not ok(row) then return end
  P("set", pcall(function() row:SetItemStack({ Item = item, Amount = 5 }, nil) end))
  P("text", pcall(function() row.AmountText:SetText(FText("5/10x")) end))
  P("colour", pcall(function() row.AmountText:SetColour({ SpecifiedColor = COST_RED, ColorUseRule = 0 }) end))
  local host = w("Scr_Cost")
  if ok(host) then P("add", pcall(function() host:AddChild(row) end)) else P("add", "skipped (window closed)") end
end

-- ---------------------------------------------------------------------------------------------
-- Build: MiningCmd.txt "<seq>|0|bp_build [unit <sim ID>]", poll BlueprintBuild.status, then re-run the import (cost list)
-- ---------------------------------------------------------------------------------------------
local BUILD_MAX_S = 330
local buildTick
local function setEnabled(name, b) local o = w(name); if ok(o) then pcall(function() o:SetIsEnabled(b) end) end end
local function setBuildBtns(on) setEnabled("Btn_Build", on); setEnabled("Btn_Import", on) end
local function scheduleBuild() pcall(ExecuteWithDelay, POLL_MS, function() pcall(ExecuteInGameThread, buildTick) end) end

-- unit container sim (BP_BPUnitStoreObj_01_C) with ParentID == opened block ID or its frame ID. Exactly one -> its ID, else nil
local function unitSimId()
  local block = S.block
  if not ok(block) then L("build: no unit block, no unit arg"); return nil end
  local bid = rd(block, "ID")
  local fid
  local sf, fr = pcall(function() return block:GetFrame() end)
  if sf and ok(fr) then fid = rd(fr, "ID") end
  local s, sims = pcall(FindAllOf, CRATE_SIM)
  local found, n = nil, 0
  for _, sim in ipairs((s and type(sims) == "table") and sims or {}) do
    local pid = ok(sim) and rd(sim, "ParentID") or nil
    if pid ~= nil and ((bid ~= nil and pid == bid) or (fid ~= nil and pid == fid)) then n = n + 1; found = rd(sim, "ID") end
  end
  if n == 1 and found ~= nil then return found end
  L("build: unit container " .. (n == 0 and "not found" or ("ambiguous x" .. n)) .. ", no unit arg")
  return nil
end

-- re-run the import of BlueprintImport.json (same path as Btn_Import) so the cost list refreshes
local function sendImport()
  local seq = nextSeq()
  if not sendCmd(seq .. "|0|bp_import " .. dataPath("BlueprintImport.json")) then
    txt("Txt_Status", "error: command write failed"); showSummary(buildSummary("")); return
  end
  S.poll = { seq = seq, t0 = os.time(), last = nil, misLogged = {} }
  schedulePoll()
end

buildTick = function()
  local b = S.build
  if not b then return end
  if os.time() - b.t0 > BUILD_MAX_S then
    S.build = nil
    txt("Txt_Status", "error: timeout")
    setBuildBtns(true)
    L("build timeout seq=" .. b.seq)
    return
  end
  local st, text, kv = readStatus("BlueprintBuild.status")
  if st and kv.seq and not seqMatch(kv.seq, b.seq) and not b.misLogged[kv.seq] then
    b.misLogged[kv.seq] = true
    L("build status seq " .. kv.seq .. " != build_seq " .. b.seq .. " (ignored)")
  end
  if st and kv.seq and seqMatch(kv.seq, b.seq) then
    local chunk = kv.chunk and (" (chunk " .. kv.chunk .. ")") or ""
    local line = st .. "|" .. text .. chunk
    if line ~= b.last then b.last = line; L("build status seq=" .. b.seq .. " " .. line) end
    if st == "done" or st == "error" then
      S.build = nil
      local t = (st == "error") and ("error: " .. text) or text
      txt("Txt_Status", t)
      setBuildBtns(true)
      S.lastBuild = t
      sendImport()
      return
    elseif st == "working" then
      txt("Txt_Status", (text ~= "" and text or "Building...") .. chunk)
    end
  end
  if S.build == b then scheduleBuild() end
end

local function buildClick()
  if S.poll or S.build then return end
  if not imported then txt("Txt_Status", "import first"); return end
  local seq = nextSeq()
  local uid = unitSimId()
  if not sendCmd(seq .. "|0|bp_build" .. (uid and (" unit " .. uid) or ""), "build ") then
    txt("Txt_Status", "error: command write failed"); return
  end
  S.build = { seq = seq, t0 = os.time(), last = nil, misLogged = {} }
  txt("Txt_Status", "Building...")
  setBuildBtns(false)
  scheduleBuild()
end

-- block = BPO_BlueprintUnit_C only (main.lua opener filter; the container is used for lookups below, never as opener). player = DMSPlayerObject. ctl = controller or nil.
function M.open(block, player, ctl)
  ExecuteInGameThread(function()
    if M.closing and os.clock() - (M.closing_at or 0) < 3 then return end   -- previous close still tearing down
    if M.isOpen() or S.root or S.p then return end                           -- already open: block interact does nothing
    if not ok(block) then L("open: no block"); return end
    pcall(M.hideCrates, block)

    local p = ok(ctl) and ctl or nil
    if not ok(p) then p = localCtl() end
    if not ok(p) then L("open: no DMSPlayerController") return end
    if not ok(player) then player = FindFirstOf("DMSPlayerObject") ; L("open: player arg missing, FindFirstOf fallback") end

    if not ok(S.cls) then S.cls = nil end
    if not S.cls then
      local c = StaticFindObject(WBP_CLS)
      if not ok(c) then c = getAsset("/Game/BlueprintUnitUI/WBP_BlueprintUnit", "WBP_BlueprintUnit_C") or StaticFindObject(WBP_CLS) end
      if not ok(c) then L("WBP class NOT FOUND " .. WBP_CLS); return end
      S.cls = c
    end

    -- container: the block's ContainerObject (ParentID == block ID, or same frame); fallback U1 = block frame inventory
    local cont, srcName, rule = nil, nil, nil
    if cn(block) == "BP_BPUnitStoreObj_01_C" then cont = block; rule = "block is container" end
    local bid = rd(block, "ID")
    -- machines link to their frame by ParentID (MachineObject.ParentID = frame ID, per rea-machine-config), so match frame ID too
    local fid, bf = nil, nil
    do
      local sf, fr = pcall(function() return block:GetFrame() end)
      if sf and ok(fr) then
        fid = rd(fr, "ID")
        local sn, fn = pcall(function() return fr:GetFullName() end)
        if sn then bf = fn end
      end
    end
    if not cont then
      for _, c in ipairs(FindAllOf("ContainerObject") or {}) do
        if not cont and ok(c) and cn(c) == "BP_BPUnitStoreObj_01_C" then
          local pid = rd(c, "ParentID")
          local byId = pid ~= nil and ((bid ~= nil and pid == bid) or (fid ~= nil and pid == fid))
          local sameFrame = false
          if not byId and bf then
            local s1, cf = pcall(function() return c:GetFrame():GetFullName() end)
            sameFrame = s1 and cf == bf
          end
          if byId or sameFrame then
            cont = c
            rule = (bid ~= nil and pid == bid) and "ParentID=block ID" or (fid ~= nil and pid == fid) and "ParentID=frame ID" or "same frame"
          end
        end
      end
      if not cont then diagContainers(bid, fid) end
    end
    local unitInv
    if ok(cont) then
      local inv = rd(cont, "Inventory")
      if ok(inv) then unitInv = inv; srcName = "container" end
    end
    if not ok(unitInv) then
      local s2, inv = pcall(function() return block:GetFrame():GetInventory() end)
      if s2 and ok(inv) then unitInv = inv; srcName = "U1 24 slots"; cont = nil end
    end
    if not ok(unitInv) then L("open: no unit inventory (no container, U1 failed)"); return end
    local playerInv = rd(player, "Inventory")

    -- probe P2
    local slots
    pcall(function() slots = unitInv:GetItemSlots() end)
    L(string.format("P2 container ID=%s ParentID=%s inventory ID=%s slots=%s StackMultiplier=%s src=%s rule=%s",
      tostring(cont and rd(cont, "ID") or "-"), tostring(cont and rd(cont, "ParentID") or "-"), tostring(rd(unitInv, "ID")),
      tostring(countOf(slots)), tostring(rd(unitInv, "StackMultiplier")), tostring(srcName), tostring(rule or "-")))

    local lib = StaticFindObject(LIB)
    if not ok(lib) then L("open: lib invalid"); return end
    local wbp
    try("CreateWidget", function() wbp = lib:Create(p, S.cls, p) end)
    if not ok(wbp) then L("open: CreateWidget fail") return end
    S.root, S.w, S.p, S.frame = wbp, wbp, p, wbp
    S.unitInv, S.playerInv = unitInv, playerInv
    S.t0 = os.clock()
    S.tc = {}
    S.imp, S.poll, S.build, S.itemMap, S.block = false, nil, nil, nil, block
    L("CreateWidget ok " .. nm(wbp) .. " [" .. cn(wbp) .. "]")
    try("AddToViewport", function() wbp:AddToViewport(Z) end)
    if USE_VIEWPORT_SIZING then sizeWindow(wbp, p) end
    pcall(ExecuteWithDelay, 200, function() pcall(ExecuteInGameThread, function() geomProbe(wbp, p) end) end)

    local miss = {}
    for _, n in ipairs(CONTRACT) do if not ok(w(n)) then miss[#miss + 1] = n end end
    if #miss > 0 then L("missing widgets: " .. table.concat(miss, " ")) end
    local hdr = w("Txt_CostHdr")
    if ok(hdr) then
      local sh, ht = pcall(function() return hdr:GetText():ToString() end)
      if sh and ht ~= HDR_TXT then L("cost header text is '" .. tostring(ht) .. "', designer should set '" .. HDR_TXT .. "'") end
    end

    -- grids: unit (Scr_UnitInv) and player mirror (Host_PlayerInv)
    local invCls = StaticFindObject(INV_CLS)
    if not ok(invCls) then invCls = getAsset(INV_PKG, "WBP_Inventory_C") or StaticFindObject(INV_CLS) end
    if ok(invCls) then
      S.uw = mkGrid(invCls, p, "Scr_UnitInv", unitInv, playerInv)
      S.pw = mkGrid(invCls, p, "Host_PlayerInv", playerInv, unitInv)
    else
      L("P3: WBP_Inventory class not found, grids skipped")
    end
    L(string.format("grids unit=%s player=%s secondary valid=%s/%s", tostring(rd(unitInv, "ID")), tostring(rd(playerInv, "ID")),
      tostring(ok(playerInv)), tostring(ok(unitInv))))

    -- buttons
    if BIND and BIND.onClick then
      local function bind(n, fn)
        local b = w(n); if ok(b) then try("bind " .. n, function() BIND.onClick(b, fn) end) end
      end
      bind("Btn_Close", M.close)
      bind("Btn_Import", impOpenModal)
      bind("Btn_Build", buildClick)
      bind("Btn_ImportClose", M.impClose)
      bind("Btn_ImportCancel", impCancel)
      bind("Btn_ImportOk", impOk)
    else
      L("open: no api.onClick (call bind first)")
    end
    setVis(w("Pnl_ImportModal"), false)
    resetResults()

    -- input focus: GameAndUI like the vanilla windows. UIOnly makes PC:IsInputKeyDown(Shift) false, which breaks shift-click quick move.
    -- Drags stay stable through the ignore look/move calls below.
    local ui = StaticFindObject(LIB)
    local okIn = try("input GameAndUIEx5", function() ui:SetInputMode_GameAndUIEx(p, wbp, 0, true, false) end)
      or try("input GameAndUIEx4", function() ui:SetInputMode_GameAndUIEx(p, wbp, 0, true) end)
      or try("input UIOnlyEx4", function() ui:SetInputMode_UIOnlyEx(p, wbp, 0, false) end)
    L("input mode ok=" .. tostring(okIn))
    try("mouse on", function() p.bShowMouseCursor = true end)
    try("focusable", function() wbp.bIsFocusable = true end)
    try("user focus", function() wbp:SetUserFocus(p) end)
    try("look ignore", function() p:SetIgnoreLookInput(true) end)
    try("move ignore", function() p:SetIgnoreMoveInput(true) end)

    M.opens = (M.opens or 0) + 1
    L("open n=" .. M.opens .. " isOpen=" .. tostring(M.isOpen()))
  end)
end

-- pure parse helpers, exposed for tests/test_parse.lua (no game calls)
M._parse = { status = parseStatus, cost = parseCost, removes = prettyRemoves, seqMatch = seqMatch }

return M
