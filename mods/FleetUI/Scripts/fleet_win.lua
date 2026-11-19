-- FleetUI window loader (UE4SS Lua). API: open / close / toggle / isOpen / setTab.
-- Window = WBP_FleetWindow instance in the viewport. Helpers copied from the mining window loader (no sharing).
-- Forward slashes only in paths. Every handler goes through guard(): an error logs "[FleetUI] ERR <name>: <err>" and nothing else.
local M = {}
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""

local function L(m) print("[FleetUI] " .. m .. "\n") end
local function ok(o) if o == nil or type(o) ~= "userdata" then return false end local s, v = pcall(function() return o:IsValid() end) return s and v == true end
local function rd(o, p) if not ok(o) then return nil end local s, v = pcall(function() return o[p] end) if s then return v end return nil end
local function try(tag, f, ...) local s, r = pcall(f, ...) if not s then L("FAIL " .. tag .. ": " .. tostring(r)) end return s end
local function nm(o) if not ok(o) then return "invalid" end local s, r = pcall(function() return o:GetFName():ToString() end) return s and tostring(r) or "?" end
local function guard(name, fn)
  return function(...)
    local r = table.pack(pcall(fn, ...))
    if not r[1] then L("ERR " .. tostring(name) .. ": " .. tostring(r[2])) return nil end
    return table.unpack(r, 2, r.n)
  end
end

local LIB = "/Script/UMG.Default__WidgetBlueprintLibrary"
local WBP_PKG, WBP_NAME = "/Game/FleetUI/WBP_FleetWindow", "WBP_FleetWindow_C"
local WBP_CLS = WBP_PKG .. "." .. WBP_NAME
local Z = 49                                               -- below dev menu 50
local TABS = { "Btn_TabFleet", "Btn_TabRole", "Btn_TabEscort" }   -- PageSwitcher index 0..2
local TAB_ON = { R = 0.658375, G = 0.323143, B = 0.0, A = 1.0 }
local TAB_OFF = { R = 0.012983, G = 0.012983, B = 0.012983, A = 0.95 }
local TXT_ON = { R = 0.010330, G = 0.010330, B = 0.010330, A = 1.0 }
local TXT_OFF = { R = 1.0, G = 1.0, B = 1.0, A = 1.0 }
local MN = 40                                              -- modal pool size (Opt_Modal_1..40)
local WALK_NODES, WALK_DEPTH = 3000, 48
local OPT_GREY = { R = 0.068, G = 0.068, B = 0.068, A = 1.0 }   -- 4A4A4A linear
local OPT_GOLD = TAB_ON

local S = { root = nil, w = nil, p = nil, frame = nil, gen = 0, map = nil, miss = {}, modal = nil }
local function holdReads() S.noRead = os.clock() + 0.6 end
local function reading() return os.clock() >= (S.noRead or 0) end

-- ===== button clicks: ForceLayoutPrepass hook (copied from MiningUnitUI main.lua) =====
local BTN = {}
local HOOK = "/Script/UMG.Widget:ForceLayoutPrepass"
local hookIds, hookOpens = nil, 0
local function onPrepass(self)
  if next(BTN) == nil then return end   -- no window buttons: leave at once
  local okn, key = pcall(function() return self:get():GetFullName() end)
  local fn = okn and BTN[key]
  if fn then pcall(ExecuteInGameThread, function() pcall(fn) end) end
end
-- WHY no UnregisterHook: registering/removing a hook on the same game function from several Lua states crashed the game (seen 2026-11-19).
local function onClick(btn, fn)
  if not ok(btn) then return end
  if not hookIds then
    local s, pre, post = pcall(RegisterHook, HOOK, onPrepass)
    if s then hookIds = { pre, post }; hookOpens = hookOpens + 1; L("prepass hook registered ids=" .. pre .. "," .. post .. " n=" .. hookOpens) end
  end
  BTN[btn:GetFullName()] = guard("click", fn)
  btn.OnClicked:Add(btn, FName("ForceLayoutPrepass"))
end

-- ===== assets =====
local ARH
local function getAsset(pkg, name)
  if not ok(ARH) then ARH = StaticFindObject("/Script/AssetRegistry.Default__AssetRegistryHelpers") end
  if not ok(ARH) then L("ARH invalid"); return nil end
  local sf, r = pcall(function() return ARH:GetAsset({ PackageName = FName(pkg), AssetName = FName(name) }) end)
  L("GetAsset " .. pkg .. "." .. name .. " ok=" .. tostring(sf) .. " valid=" .. tostring(sf and ok(r)))
  if sf and ok(r) then return r end
end
local function loadClass()
  if ok(S.cls) then return true end
  local c = StaticFindObject(WBP_CLS)
  if not ok(c) then c = getAsset(WBP_PKG, WBP_NAME) or StaticFindObject(WBP_CLS) end
  if not ok(c) then L("WBP class NOT FOUND " .. WBP_CLS); return false end
  S.cls = c
  return true
end

-- ===== widget lookup: ONE bounded walk of the whole tree per window open (never a walk per lookup: that froze the game) =====
local function buildMap()
  local t0 = os.clock()
  local map, nodes = {}, 0
  S.map = map
  local s, root = pcall(function() return S.w.WidgetTree.RootWidget end)
  if not (s and ok(root)) then L("walk: no root"); return end
  local function rec(o, d)
    if nodes >= WALK_NODES or d > WALK_DEPTH or not ok(o) then return end
    nodes = nodes + 1
    local s1, n = pcall(function() return o:GetFName():ToString() end)
    if s1 and n and map[n] == nil then map[n] = o end
    local s2, c = pcall(function() return o:GetChildrenCount() end)
    if s2 and type(c) == "number" and c > 0 then
      for i = 0, c - 1 do
        local s3, ch = pcall(function() return o:GetChildAt(i) end)
        if s3 then rec(ch, d + 1) end
      end
    else
      local s4, ct = pcall(function() return o:GetContent() end)
      if s4 then rec(ct, d + 1) end
    end
  end
  rec(root, 0)
  L(string.format("walk: %d nodes, %.0f ms%s", nodes, (os.clock() - t0) * 1000, nodes >= WALK_NODES and " (CAP HIT)" or ""))
end
local function w(name)
  local o = S.map and S.map[name]
  if o ~= nil then return ok(o) and o or nil end
  if S.miss[name] or not ok(S.w) then return nil end
  local s, r = pcall(function() return S.w:GetWidgetFromName(FName(name)) end)
  if s and ok(r) then if S.map then S.map[name] = r end return r end
  S.miss[name] = true
  return nil
end

local function setVis(o, v) if ok(o) then try("vis", function() o:SetVisibility(v) end) end end
local TXT = setmetatable({}, { __mode = "k" })
local function setText(o, s)
  if not ok(o) then return end
  s = tostring(s or "")
  if TXT[o] == s then return end
  if pcall(function() o:SetText(FText(s)) end) then TXT[o] = s end
end

-- find by name below a node (entry widgets are not in the open-time map)
local function deepFind(node, name, depth)
  depth = depth or 0
  if depth > 16 or not ok(node) then return nil end
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

-- flat list for collapsible groups: {k="hdr",key,name,count,folded} / {k="item",item}. keyFn nil = "~other"; "~" keys last.
local function groupList(items, keyFn, nameFn, fold)
  local by, keys = {}, {}
  for _, it in ipairs(items) do
    local k = keyFn(it) or "~other"
    if not by[k] then by[k] = {}; keys[#keys + 1] = k end
    by[k][#by[k] + 1] = it
  end
  local nm2 = {}
  for _, k in ipairs(keys) do nm2[k] = tostring(nameFn(k)):lower() end
  table.sort(keys, function(a, b)
    local ta, tb = a:sub(1, 1) == "~", b:sub(1, 1) == "~"
    if ta ~= tb then return tb end
    if nm2[a] ~= nm2[b] then return nm2[a] < nm2[b] end
    return a < b
  end)
  local out = {}
  for _, k in ipairs(keys) do
    local folded = (fold and fold[k]) and true or false
    out[#out + 1] = { k = "hdr", key = k, name = nameFn(k), count = #by[k], folded = folded }
    if not folded then for _, it in ipairs(by[k]) do out[#out + 1] = { k = "item", item = it } end end
  end
  return out
end

-- dynamic entry list. spec: key, anchor (page widget above the box), box (name), n, bind(e, find, idx) once per entry, paint(e, find, i) per row.
local dynWarn = {}
local function dynList(sp)
  local anchor = w(sp.anchor)
  if not anchor then if not dynWarn[sp.key] then dynWarn[sp.key] = true; L("dynList " .. sp.key .. ": anchor " .. sp.anchor .. " not found") end return nil end
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
    if sp.bind then guard("bind " .. sp.key, sp.bind)(e, find, function() return idxOf(e) end) end
  end
  while #st.ents > sp.n do
    local r = table.remove(st.ents); try("RemoveEntry", function() st.box:RemoveEntry(r.w) end)
  end
  if sp.paint then for i, r in ipairs(st.ents) do sp.paint(r.w, r.find, i) end end   -- sp.paint is guarded by the caller (ui_core.list)
  return st
end

-- ===== modal select on the pooled Opt_Modal_1..40 (Btn + Txt_ModalC1_i). a = {title, options={{id,text}}, id (selected), onPick(id, opt)} =====
local function modalClose()
  S.modal = nil
  setVis(w("Pnl_Modal"), 1)
end
local function modalSelect(a)
  holdReads()
  local opts = a.options or {}
  if #opts > MN then L("modal: " .. #opts .. " options, showing " .. MN) end
  S.modal = { opts = opts, onPick = a.onPick }
  setText(w("Txt_ModalTitle"), a.title or "")
  for i = 1, MN do
    local b, t = w("Opt_Modal_" .. i), w("Txt_ModalC1_" .. i)
    local o = opts[i]
    if o then
      setText(t, o.text or tostring(o.id))
      if ok(b) then try("opt bg", function() b:SetBackgroundColor(o.id == a.id and OPT_GOLD or OPT_GREY) end) end
      if ok(t) then try("opt txt", function() t:SetColorAndOpacity({ SpecifiedColor = o.id == a.id and TXT_ON or TXT_OFF }) end) end
      setVis(b, 0)
    else
      setVis(b, 1)
    end
  end
  setVis(w("Pnl_Modal"), 0)
end
local function modalPick(i)
  local m = S.modal
  local o = m and m.opts[i]
  if not o then return end
  modalClose()
  if m.onPick then m.onPick(o.id, o) end
end

-- ===== tabs =====
local FLEET
function M.setTab(i)
  holdReads()
  local sw = w("PageSwitcher")
  if sw then try("SetActiveWidgetIndex", function() sw:SetActiveWidgetIndex(i - 1) end) end
  for k, n in ipairs(TABS) do
    local on = (k == i)
    local b = w(n)
    if b then try("tab bg " .. n, function() b:SetBackgroundColor(on and TAB_ON or TAB_OFF) end) end
    local t = w("Txt_" .. n:sub(5))
    if t then try("tab txt " .. n, function() t:SetColorAndOpacity({ SpecifiedColor = on and TXT_ON or TXT_OFF }) end) end
  end
  if FLEET then guard("onTab", FLEET.onTab)(i) end
end

-- ===== api for fleet_ui =====
local api = { w = w, setVis = setVis, setText = setText, try = try, ok = ok, rd = rd, L = L, guard = guard, deepFind = deepFind,
  dynList = dynList, groupList = groupList, modalSelect = modalSelect, reading = reading, holdReads = holdReads,
  setTab = M.setTab, onClick = onClick, src = SRC, data = SRC,
  benchShips = function() return {} end, shipIcons = function() return {} end }
local okF, F = pcall(dofile, SRC .. "fleet_ui.lua")
if okF and type(F) == "table" then
  FLEET = F
  guard("init", FLEET.init)(api)
else
  L("ERR load fleet_ui: " .. tostring(F))
end

-- ===== window =====
function M.isOpen()
  if not ok(S.frame) then return false end
  local s, v = pcall(function() return S.frame:IsInViewport() end)
  return s and v == true
end

function M.close()
  S.gen = S.gen + 1   -- stops the refresh loop
  BTN = {}
  S.modal, S.dyn, S.map, S.miss = nil, nil, nil, {}
  dynWarn = {}
  if FLEET then guard("onClose", FLEET.onClose)() end
  if not (S.root or S.p or S.frame) then return end
  if M.closing and os.clock() - (M.closing_at or 0) < 3 then return end
  M.closes = (M.closes or 0) + 1
  M.closing, M.closing_at = true, os.clock()
  local root, p, ep, n = S.root, S.p, M.opens or 0, M.closes
  S.root, S.p, S.w, S.frame = nil, nil, nil, nil
  -- RemoveFromParent + input restore run on a later tick, outside the caller (click / key) stack
  local function teardown()
    if ok(root) then try("RemoveFromParent", function() root:RemoveFromParent() end) end
    if ok(p) and ep == (M.opens or 0) then   -- only if no window was opened since this close
      try("look unignore", function() p:SetIgnoreLookInput(false) end)
      try("move unignore", function() p:SetIgnoreMoveInput(false) end)
      try("mouse off", function() p.bShowMouseCursor = false end)
      try("input GameOnly", function() StaticFindObject(LIB):SetInputMode_GameOnly(p, false) end)
    end
    M.closing = false
    L("closed n=" .. tostring(n))
  end
  if not pcall(ExecuteWithDelay, 50, function() pcall(ExecuteInGameThread, teardown) end) then teardown() end
end

local function startLoop()
  local g = S.gen
  LoopAsync(500, function()
    if g ~= S.gen then return true end
    ExecuteInGameThread(function()
      if g == S.gen and ok(S.root) and FLEET then guard("tick", FLEET.tick)() end
    end)
    return false
  end)
end

function M.open()
  if M.isOpen() then return end
  if not loadClass() then return end
  local p
  try("local DMSPlayerController", function()
    for _, c in ipairs(FindAllOf("DMSPlayerController") or {}) do
      if ok(c) then
        local sl, loc = pcall(function() return c:IsLocalPlayerController() end)
        if sl and loc then p = c break end
      end
    end
  end)
  if not ok(p) then try("FindFirstOf DMSPlayerController", function() p = FindFirstOf("DMSPlayerController") end) end
  if not ok(p) then L("open: no DMSPlayerController") return end
  local lib = StaticFindObject(LIB)
  if not ok(lib) then L("open: lib invalid"); return end
  local wbp
  try("CreateWidget", function() wbp = lib:Create(p, S.cls, p) end)
  if not ok(wbp) then L("open: CreateWidget fail"); S.cls = nil; return end
  S.root, S.w, S.p, S.frame = wbp, wbp, p, wbp
  S.map, S.miss, S.modal, S.dyn = nil, {}, nil, nil
  try("AddToViewport", function() wbp:AddToViewport(Z) end)
  buildMap()
  for _, n in ipairs({ "PageSwitcher", "Btn_FlClose", "Btn_TabFleet", "Btn_TabRole", "Btn_TabEscort", "Pnl_PageFleet", "Pnl_PageRole", "Pnl_PageEscort" }) do
    if not w(n) then L("FAIL widget missing " .. n) end
  end

  -- clicks: close, tabs, modal
  setVis(w("Pnl_Modal"), 1)
  onClick(w("Btn_FlClose"), M.close)
  for i, n in ipairs(TABS) do onClick(w(n), function() M.setTab(i) end) end
  onClick(w("Btn_ModalOutside"), modalClose)
  onClick(w("Btn_ModalClose"), modalClose)
  for i = 1, MN do onClick(w("Opt_Modal_" .. i), function() modalPick(i) end) end
  if FLEET then guard("bind", FLEET.bind)() end

  -- input focus: UI-only so drags do not reach the game camera
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

  M.setTab(1)
  startLoop()
  M.opens = (M.opens or 0) + 1
  L("open n=" .. M.opens .. " isOpen=" .. tostring(M.isOpen()))
end

function M.toggle()
  if M.isOpen() then M.close() else M.open() end
end

return M
