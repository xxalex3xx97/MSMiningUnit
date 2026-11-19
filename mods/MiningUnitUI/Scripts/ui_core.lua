-- Generic UI helpers for MiningUnitUI page modules. No game or fleet words here.
-- find(name) = widget finder (page index, list entry or nil). Every helper is a no-op for a missing widget.
local C = {}
local api
local PAL = {}                                                   -- name -> "#RRGGBB"
local IDX = {}                                                   -- page name -> find(name)
local BOUND = setmetatable({}, { __mode = "k" })                 -- widget -> click bound once
local HANDLER = setmetatable({}, { __mode = "k" })               -- widget -> latest click fn
local WALK_NODES = 2600

function C.init(a) api = a end

-- palette({ name = "#RRGGBB" }) merged into the registry
function C.palette(t)
  for k, v in pairs(t or {}) do PAL[k] = v end
  return PAL
end

-- "#RRGGBB", "RRGGBB" or a registered name -> linear colour {R,G,B,A} (lin = (c/255)^2.2); nil if unknown
function C.col(name)
  local hex = PAL[name] or name
  if type(hex) ~= "string" then return nil end
  hex = hex:gsub("^#", "")
  local r, g, b = tonumber(hex:sub(1, 2), 16), tonumber(hex:sub(3, 4), 16), tonumber(hex:sub(5, 6), 16)
  if not (r and g and b) then return nil end
  local function lin(c) return (c / 255) ^ 2.2 end
  return { R = lin(r), G = lin(g), B = lin(b), A = 1.0 }
end

function C.text(o, s)
  if not api.ok(o) then return end
  pcall(function() o:SetText(FText(tostring(s or ""))) end)
end

function C.tint(o, colour)
  local c = C.col(colour)
  if c and api.ok(o) then pcall(function() o:SetColorAndOpacity({ SpecifiedColor = c }) end) end
end

-- chip(find, bdr, icon, txt, text, colour): Border takes the colour, icon and text are dark (111111) on it
function C.chip(find, bdr, icon, txt, text, colour)
  local b = find(bdr)
  local c = C.col(colour)
  if c and api.ok(b) then pcall(function() b:SetBrushColor(c) end) end
  C.tint(find(icon), "#111111")
  C.tint(find(txt), "#111111")
  C.text(find(txt), text)
end

-- bar(widget, pct 0..1, colour?): SetPercent + optional fill colour
function C.bar(o, pct, colour)
  if not api.ok(o) then return end
  pcall(function() o:SetPercent(math.max(0, math.min(1, pct or 0))) end)
  local c = C.col(colour)
  if c then pcall(function() o:SetFillColorAndOpacity(c) end) end
end

-- fold(stateTable, key): toggles and returns the new flag
function C.fold(t, key)
  t[key] = not t[key]
  return t[key]
end

-- bind_button(find, name, fn): click bound once per widget; the latest fn wins (rebuilt rows never run stale closures)
function C.bind_button(find, name, fn)
  local b = find(name)
  if not api.ok(b) then return nil end
  HANDLER[b] = fn
  if not BOUND[b] then
    BOUND[b] = true
    api.onClick(b, function() local f = HANDLER[b]; if f then f() end end)
  end
  return b
end

-- Select field: Sel_<x> Button with text Txt_Sel<x>. spec = {title, options(), get(), set(v), display(v), repaint()}
function C.sel_field(find, name, spec)
  local v = spec.get()
  C.text(find("Txt_Sel" .. name:sub(5)), spec.display and spec.display(v) or (v ~= nil and tostring(v)) or "-")
  C.bind_button(find, name, function()
    api.modalSelect{ title = spec.title or "", options = spec.options and spec.options() or {}, id = spec.get(),
      onPick = function(id) spec.set(id); if spec.repaint then spec.repaint() end end }
  end)
end

-- Edit field: Edt_<x> EditableTextBox. Write only; read with read_edit
function C.edit_field(find, name, v)
  local e = find(name)
  if api.ok(e) then pcall(function() e:SetText(FText(v ~= nil and tostring(v) or "")) end) end
end

-- read_edit: number or text as typed; the box is read only through api.reading() (never right after a tab change)
function C.read_edit(find, name, isNumber)
  local e = find(name)
  if not (api.ok(e) and api.reading()) then return nil end
  local s, t = pcall(function() return e:GetText():ToString() end)
  if not (s and t) then return nil end
  if isNumber then return tonumber(t) end
  return t
end

-- Check field: Chk_<x> CheckBox
function C.check_field(find, name, on)
  local c = find(name)
  if api.ok(c) then pcall(function() c:SetIsChecked(on and true or false) end) end
end

function C.read_check(find, name)
  local c = find(name)
  if not (api.ok(c) and api.reading()) then return nil end
  local s, v = pcall(function() return c:IsChecked() end)
  if s then return v == true end
  return nil
end

-- list{key, anchor, box, rows, rowFn(row, find, i, entry), bindFn(entry, find, idxFn)} over api.dynList
function C.list(sp)
  return api.dynList{ key = sp.key, anchor = sp.anchor, box = sp.box, n = #sp.rows,
    paint = function(entry, find, i) sp.rowFn(sp.rows[i], find, i, entry) end,
    bind = sp.bindFn }
end

-- index(page): one bounded walk of the page subtree, cached until C.reset(). Returns find(name) (page-local only)
function C.index(page)
  if IDX[page] then return IDX[page] end
  local map, nodes = {}, 0
  local function rec(o, d)
    if nodes >= WALK_NODES or d > 48 or not api.ok(o) then return end
    nodes = nodes + 1
    local s, n = pcall(function() return o:GetFName():ToString() end)
    if s and n and map[n] == nil then map[n] = o end
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
  rec(api.w(page), 0)
  local function find(name) return map[name] end
  IDX[page] = find
  return find
end

-- drop cached indexes and bind marks (call on window close)
function C.reset()
  IDX = {}
  BOUND = setmetatable({}, { __mode = "k" })
  HANDLER = setmetatable({}, { __mode = "k" })
end

return C
