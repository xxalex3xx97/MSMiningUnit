-- Blueprint Unit window (WBP_BlueprintUnit). API: bind / open(block, player, ctl) / close / isOpen / fKey / setCost / devCost.
-- Read-only: Lua never writes an inventory. Two WBP_Inventory grids (unit + player) from the container, cost rows, placeholder buttons.
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
local WBP_CLS = "/Game/MiningUnitUI/WBP_BlueprintUnit.WBP_BlueprintUnit_C"
local INV_PKG = "/Game/Blueprints/Widgets/Display/Inventory/WBP_Inventory"
local INV_CLS = "/Game/Blueprints/Widgets/Display/Inventory/WBP_Inventory.WBP_Inventory_C"
local Z = 49                                   -- below dev menu 50 (same as Mining)
local COST_N = 40
local COST_RED = { R = 0.604, G = 0, B = 0.067, A = 1 }
local COST_WHITE = { R = 1, G = 1, B = 1, A = 1 }
-- widget names the WBP must provide (contract). Missing ones are logged once per open.
local CONTRACT = { "Panel", "Txt_Title", "Btn_Close", "Txt_Close", "Txt_Status", "Btn_Import", "Txt_Import", "Btn_Build", "Txt_Build",
  "Txt_CostHdr", "Scr_Cost", "Txt_CostEmpty", "Txt_UnitInvTitle", "Scr_UnitInv", "Txt_PlayerInvTitle", "Host_PlayerInv" }
for i = 1, COST_N do
  CONTRACT[#CONTRACT + 1] = "Row_Cost_" .. i
  CONTRACT[#CONTRACT + 1] = "Img_Cost_" .. i
  CONTRACT[#CONTRACT + 1] = "Txt_CostHave_" .. i
  CONTRACT[#CONTRACT + 1] = "Txt_CostName_" .. i
end

-- S.w = WBP instance = viewport widget (names resolve here). S.p = controller. S.uw/S.pw = grids. S.tc = last text per name.
local S = { root = nil, w = nil, p = nil, cls = nil, frame = nil, uw = nil, pw = nil, unitInv = nil, playerInv = nil, cost = nil, tc = nil }
local BIND -- {onClick, clear} from main.lua
function M.bind(api) BIND = api end

-- ESlateVisibility: 0 Visible, 1 Collapsed
local function setVis(o, shown) if ok(o) then try("vis", function() o:SetVisibility(shown and 0 or 1) end) end end

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

-- read-only count: unit + player inventory. GetItemAmount error -> logged once, counts as 0
local amtErr = false
local function haveOf(item)
  local have = 0
  for _, inv in ipairs({ S.unitInv, S.playerInv }) do
    if ok(inv) then
      local s, v = pcall(function() return inv:GetItemAmount(item) end)
      if s and type(v) == "number" then have = have + v
      elseif not amtErr then amtErr = true; L("GetItemAmount failed (" .. tostring(v) .. "), using 0") end
    end
  end
  return have
end

-- cost rows: list = array of { item = <Item DA>, need = n }. Row i shown when set, else collapsed. Read-only.
local iconLogged = false
function M.setCost(list)
  S.cost = list or {}
  setVis(w("Txt_CostEmpty"), #S.cost == 0)
  for i = 1, COST_N do
    local row = w("Row_Cost_" .. i)
    local e = S.cost[i]
    if e and ok(e.item) then
      local item, need = e.item, e.need or 0
      local have = haveOf(item)
      setVis(row, true)
      txt("Txt_CostName_" .. i, itemName(item))
      txt("Txt_CostHave_" .. i, string.format("%d/%dx", math.floor(have), math.floor(need)))
      local tb = w("Txt_CostHave_" .. i)
      local col = have < need and COST_RED or COST_WHITE
      if ok(tb) then try("cost colour " .. i, function() tb:SetColorAndOpacity({ SpecifiedColor = col }) end) end
      local img = w("Img_Cost_" .. i)
      if ok(img) then try("cost icon " .. i, function() img:SetBrushFromTexture(item.Icon, false) end) end
      if not iconLogged then
        iconLogged = true
        local s, ic = pcall(function() return item.Icon end)
        L("P5 Item.Icon class=" .. ((s and ok(ic)) and cn(ic) or tostring(s and ic or "nil")))
      end
    else
      setVis(row, false)
    end
  end
end

-- DEV: 3 fake cost rows from the first Item DAs (need 5 / 50 / 500)
function M.devCost()
  local items = {}
  local s, list = pcall(FindAllOf, "Item")
  for _, it in ipairs(s and list or {}) do
    if #items == 3 then break end
    if ok(it) then
      local s1, n = pcall(function() return it:GetFName():ToString() end)
      if s1 and n and not n:find("Default__", 1, true) then items[#items + 1] = it end
    end
  end
  local needs = { 5, 50, 500 }
  local out = {}
  for i, it in ipairs(items) do out[i] = { item = it, need = needs[i] } end
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

function M.close()
  if BIND and BIND.clear then BIND.clear() end  -- drop button callbacks + global layout hook
  S.cost, S.tc = nil, nil
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

function M.fKey()
  if M.isOpen() and os.clock() - (S.t0 or 0) > 0.5 and not typing() then M.close() end
end

-- block = BPO_BlueprintUnit_C (or the BP_BPUnitStoreObj_01_C container itself). player = DMSPlayerObject. ctl = controller or nil.
function M.open(block, player, ctl)
  ExecuteInGameThread(function()
    if M.closing and os.clock() - (M.closing_at or 0) < 3 then return end   -- previous close still tearing down
    if M.isOpen() or S.root or S.p then return end                           -- already open: block interact does nothing
    if not ok(block) then L("open: no block"); return end

    local p = ok(ctl) and ctl or nil
    if not ok(p) then p = localCtl() end
    if not ok(p) then L("open: no DMSPlayerController") return end
    if not ok(player) then player = FindFirstOf("DMSPlayerObject") ; L("open: player arg missing, FindFirstOf fallback") end

    if not ok(S.cls) then S.cls = nil end
    if not S.cls then
      local c = StaticFindObject(WBP_CLS)
      if not ok(c) then c = getAsset("/Game/MiningUnitUI/WBP_BlueprintUnit", "WBP_BlueprintUnit_C") or StaticFindObject(WBP_CLS) end
      if not ok(c) then L("WBP class NOT FOUND " .. WBP_CLS); return end
      S.cls = c
    end

    -- container: the block's ContainerObject (ParentID == block ID, or same frame); fallback U1 = block frame inventory
    local cont, srcName = nil, nil
    if cn(block) == "BP_BPUnitStoreObj_01_C" then cont = block end
    local bid = rd(block, "ID")
    if not cont then
      local s0, bf = pcall(function() return block:GetFrame():GetFullName() end)
      for _, c in ipairs(FindAllOf("ContainerObject") or {}) do
        if not cont and ok(c) and cn(c) == "BP_BPUnitStoreObj_01_C" then
          local pid = rd(c, "ParentID")
          local sameFrame = false
          if not (pid ~= nil and pid == bid) and s0 then
            local s1, cf = pcall(function() return c:GetFrame():GetFullName() end)
            sameFrame = s1 and cf == bf
          end
          if (pid ~= nil and pid == bid) or sameFrame then cont = c end
        end
      end
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
    L(string.format("P2 container ID=%s ParentID=%s inventory ID=%s slots=%s StackMultiplier=%s src=%s",
      tostring(cont and rd(cont, "ID") or "-"), tostring(cont and rd(cont, "ParentID") or "-"), tostring(rd(unitInv, "ID")),
      tostring(countOf(slots)), tostring(rd(unitInv, "StackMultiplier")), tostring(srcName)))

    local lib = StaticFindObject(LIB)
    if not ok(lib) then L("open: lib invalid"); return end
    local wbp
    try("CreateWidget", function() wbp = lib:Create(p, S.cls, p) end)
    if not ok(wbp) then L("open: CreateWidget fail") return end
    S.root, S.w, S.p, S.frame = wbp, wbp, p, wbp
    S.unitInv, S.playerInv = unitInv, playerInv
    S.t0 = os.clock()
    S.tc = {}
    L("CreateWidget ok " .. nm(wbp) .. " [" .. cn(wbp) .. "]")
    try("AddToViewport", function() wbp:AddToViewport(Z) end)

    local miss = {}
    for _, n in ipairs(CONTRACT) do if not ok(w(n)) then miss[#miss + 1] = n end end
    if #miss > 0 then L("missing widgets: " .. table.concat(miss, " ")) end

    -- grids: unit (Scr_UnitInv) and player mirror (Host_PlayerInv)
    local invCls = StaticFindObject(INV_CLS)
    if not ok(invCls) then invCls = getAsset(INV_PKG, "WBP_Inventory_C") or StaticFindObject(INV_CLS) end
    if ok(invCls) then
      S.uw = mkGrid(invCls, p, "Scr_UnitInv", unitInv, playerInv)
      S.pw = mkGrid(invCls, p, "Host_PlayerInv", playerInv, unitInv)
    else
      L("P3: WBP_Inventory class not found, grids skipped")
    end

    -- buttons (placeholders: no inventory write)
    if BIND and BIND.onClick then
      local function bind(n, fn)
        local b = w(n); if ok(b) then try("bind " .. n, function() BIND.onClick(b, fn) end) end
      end
      bind("Btn_Close", M.close)
      bind("Btn_Import", function() L("Btn_Import clicked (placeholder)"); txt("Txt_Status", "ready") end)
      bind("Btn_Build", function() L("Btn_Build clicked (placeholder)"); txt("Txt_Status", "ready") end)
    else
      L("open: no api.onClick (call bind first)")
    end
    M.setCost({})

    -- input focus: UI-only so drags do not reach the game camera. Fallbacks as in MiningUnitUI.
    local ui = StaticFindObject(LIB)
    local okIn = try("input UIOnlyEx4", function() ui:SetInputMode_UIOnlyEx(p, wbp, 0, false) end)
      or try("input UIOnlyEx2", function() ui:SetInputMode_UIOnlyEx(p, wbp) end)
      or try("input GameAndUIEx5", function() ui:SetInputMode_GameAndUIEx(p, wbp, 0, false, false) end)
      or try("input GameAndUIEx4", function() ui:SetInputMode_GameAndUIEx(p, wbp, 0, false) end)
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

return M
