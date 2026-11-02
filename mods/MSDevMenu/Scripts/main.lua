-- SandboxMenu: UE4SS Dev Menu for Main Sequence Demo (opens with the inventory). Static build, untested in game.
-- DEV ONLY: cheats and test buttons. Not in the release zip; never ship it to players.
-- Every game call goes through try()/pcall. Every log line gets TAG.

local TAG = "[SandboxMenu] "

---------------------------------------------------------------- helpers

local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this script's dir (Mods/MSDevMenu/Scripts/)
package.path = ((SRC or ""):gsub("\\", "/")) .. "../../MSCore/Scripts/?.lua;" .. package.path
local MSCore = require("mscore")
MSCore.require(1)
local log, try = MSCore.logger(TAG)
local S, UIH, R = MSCore.safe, MSCore.ui, MSCore.resolve
local val, nameOf, validObj, objName, live, rd = S.val, S.nameOf, S.valid, S.objName, S.live, S.rd

local function first(cls)
    local ok, obj = pcall(function()
        local o = FindFirstOf(cls)
        if o and o:IsValid() then return o end
        return nil
    end)
    if not ok then
        log("first " .. cls .. " FAIL: " .. tostring(obj))
        return nil
    end
    log("first " .. cls .. " -> " .. (obj and "found" or "nil"))
    return obj
end

local function player()
    return first(R.player[1]) or first(R.player[2])
end

local function ctrl()
    return first(R.controller[1]) or first(R.controller[2])
end

---------------------------------------------------------------- dev menu actions

local function toggleCreative()
    local p = player()
    local cur = p ~= nil and val(p.bCreative) == true
    ctrl():SetCreativeMode(not cur)
end

local function unlockResearch()
    ctrl():SetCreativeUnlockAllResearch()
end

---------------------------------------------------------------- inventory side panel

local UI = { root = nil, shown = false, rows = {}, collapsed = false }
local mk, setText, onClick = UIH.mk, UIH.setText, UIH.onClick

-- ESlateVisibility: 0 Visible, 1 Collapsed, 2 Hidden, 3 HitTestInvisible, 4 SelfHitTestInvisible
local VIS_OPEN = { [0] = true, [3] = true, [4] = true }

local function invOpen()
    local info = { valid = false, inViewport = "-", vis = "-" }
    local ok, open = pcall(function()
        local w = FindFirstOf(R.invWidget)
        if not (w and w:IsValid()) then return false end
        info.valid = true
        local okv, vp = pcall(function() return w:IsInViewport() == true end)
        if not okv then info.inViewport = "ERR" return false end
        info.inViewport = tostring(vp)
        if not vp then return false end
        local okb, b = pcall(function() return w:IsVisible() end)
        if okb then info.vis = "IsVisible=" .. tostring(b) return b == true end
        local okg, g = pcall(function() return w:GetVisibility() end)
        if okg then
            local n = tonumber(val(g))
            info.vis = "GetVisibility=" .. tostring(n)
            return n ~= nil and VIS_OPEN[n] == true
        end
        info.vis = "ERR"
        return true
    end)
    return ok and open == true, info
end

local rgba = UIH.rgba
local C_GOLD  = rgba(0xD4, 0x9A, 0x00)        -- ~ R0.658 G0.323 B0
local C_BODY  = rgba(0x2B, 0x2B, 0x2B, 0.85)
local C_TABOFF = rgba(0x1E, 0x1E, 0x1E, 0.95)
local C_BTN   = rgba(0x4A, 0x4A, 0x4A, 1)
local C_DANGER = rgba(0x9C, 0x22, 0x22, 1)

local smallFont = UIH.smallFont
local hadd = UIH.hadd

-- viewport in slate units. GetViewportSize out-param handling is a guess, hence pcall + fallback.
local function layout(pc)
    local vw, vh, scale = 3440, 1440, 1
    local okv, a, b = pcall(function() local x, y = pc:GetViewportSize(0, 0) return x, y end)
    if okv and type(a) == "number" and type(b) == "number" and a > 0 and b > 0 then vw, vh = a, b end
    local oks, s = pcall(function()
        return StaticFindObject("/Script/UMG.Default__WidgetLayoutLibrary"):GetViewportScale(pc)
    end)
    if oks and type(s) == "number" and s > 0 then scale = s end
    local sw, sh = vw / scale, vh / scale
    -- Missions panel rect in slate units. FALLBACK = fractions measured from the user screenshot
    -- (2000x837 view: left 490, top 167, right 767, bottom 668, gap to next panel ~18).
    local mL, mT, mW, mH = 0.245 * sw, 0.1995 * sh, 0.1385 * sw, 0.5986 * sh
    local gap = 0.009 * sw
    local method = "fallback-fractions"
    -- live read (all guesses, validated against sane ranges, else fallback)
    pcall(function()
        local w
        for _, cls in ipairs({ R.missionsWidget }) do
            local o = FindFirstOf(cls)
            if o and o:IsValid() then w = o break end
        end
        if not w then return end
        local lib = StaticFindObject("/Script/UMG.Default__SlateBlueprintLibrary")
        local geo = w:GetCachedGeometry()
        local size = lib:GetLocalSize(geo)
        local pix, vp = {}, {}
        lib:LocalToViewport(pc, geo, { X = 0, Y = 0 }, pix, vp)
        local x, y = tonumber(vp.X), tonumber(vp.Y)
        local gw, gh = tonumber(size.X), tonumber(size.Y)
        if x and y and gw and gh and gw > 100 and gh > 100 and x > 0 and x < sw and y >= 0 and y < sh
           and x + gw <= sw + 1 then
            mL, mT, mW, mH = x, y, gw, gh
            method = "live-geometry"
        end
    end)
    local W = 430
    if mL - gap - W < 0 then W = math.max(300, mL - gap) end
    local H = math.max(400, math.min(mH, sh - mT))
    local X = math.max(0, mL - gap - W)
    local listH = math.max(120, math.floor(H - 170))
    -- DEV toggle button just below the paint-brush button of Player Status. The brush widget name is
    -- unknown (nothing in legacy), so always screenshot fractions (2000x833 view: brush 1423..1459 x, 83..118 y).
    local bs = math.max(28, math.min(48, 0.018 * sw))
    local bx, by = 0.7115 * sw, 0.150 * sh
    log(string.format("devmenu geom method=%s missions L=%.0f T=%.0f W=%.0f H=%.0f gap=%.0f -> X=%.0f Y=%.0f W=%d H=%.0f listH=%d",
        method, mL, mT, mW, mH, gap, X, mT, W, H, listH))
    if not UI.btnLogged then
        UI.btnLogged = true
        log(string.format("dev button geom method=fallback-fractions (brush widget not located) x=%.0f y=%.0f size=%.0f", bx, by, bs))
    end
    return { vw = vw, vh = vh, scale = scale, listH = listH, X = X, Y = mT, W = W, H = H, bx = bx, by = by, bs = bs }
end

local function mkBtn(label, fn, tree)
    local tr = tree or UI.tree
    local btn = mk("Button", tr)
    pcall(function() btn:SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = 0 }) end)
    pcall(function() btn:SetBackgroundColor(C_BTN) end)
    local tb = mk("TextBlock", tr)
    setText(tb, label)
    smallFont(tb, 14)
    pcall(function() tb:SetJustification(1) end)
    local cs = btn:SetContent(tb)
    pcall(function() cs:SetHorizontalAlignment(2) end)
    onClick(btn, fn)
    return btn
end

-- helpers for the coder: UI.tabs[name] is a VerticalBox; append rows to it.
-- UI.addButton("Cheats", "Creative toggle", fn)  -> full-width 26px button row, returns the Button
-- UI.addLabel("Status", "creative: ?")           -> 18px TextBlock row, returns TextBlock (use setText(tb, s))
function UI.addButton(tab, label, fn)
    local box = mk("SizeBox", UI.tree)
    box:SetHeightOverride(24)
    local btn = mkBtn(label, fn)
    box:SetContent(btn)
    local s = UI.tabs[tab]:AddChildToVerticalBox(box)
    pcall(function() s:SetPadding({ Left = 0, Top = 2, Right = 0, Bottom = 2 }) end)
    return btn
end

function UI.addLabel(tab, text)
    local tb = mk("TextBlock", UI.tree)
    setText(tb, text)
    smallFont(tb, 14)
    local s = UI.tabs[tab]:AddChildToVerticalBox(tb)
    pcall(function() s:SetPadding({ Left = 0, Top = 2, Right = 0, Bottom = 2 }) end)
    return tb
end

-- Cheats description: 14px label (addLabel) with wrap in pcall
local function cheatDesc(text)
    local tb = UI.addLabel("Cheats", text)
    pcall(function() tb:SetAutoWrapText(true) end)
    return tb
end

-- button click wrapped in try(): logs "<label> ok" / "<label> FAIL: ..."
local function addTry(tab, label, f)
    return UI.addButton(tab, label, function() try(label, f) end)
end

-- Status tab readouts. verbose (Refresh button) also logs the full research list.
local function refreshStatus(verbose)
    local p = live(table.unpack(R.player))
    setText(UI.stCreative, "creative: " .. rd(function() return val(p.bCreative) end))
    setText(UI.stInv, "inventory size: " .. rd(function() return val(p.InventorySize) end))
    local names = {}
    local okR, list = pcall(FindAllOf, R.research)
    if okR and list then
        for _, o in ipairs(list) do
            local n = objName(o)
            if not string.find(n, "Default__", 1, true) then names[#names + 1] = n end
        end
    end
    setText(UI.stRes, "research: " .. #names)
    setText(UI.stResNames, table.concat(names, ", ", 1, math.min(3, #names)) .. (#names > 3 and ", ..." or ""))
    if verbose then log("research (" .. #names .. "): " .. table.concat(names, ", ")) end
end

-- Cheats tab: creative label shows live [ON]/[OFF] from player().bCreative
local function creativeText()
    local p = live(table.unpack(R.player))
    local okc, v = pcall(function() return val(p.bCreative) end)
    local tag = (okc and v == true and "[ON]") or (okc and v == false and "[OFF]") or "[?]"
    return "Creative mode: build for free, items not consumed. " .. tag
end
local function refreshCreative()
    if UI.creativeDesc then pcall(setText, UI.creativeDesc, creativeText()) end
end

local MD = SRC and (SRC .. "../../MiningBenchMod/dlls/") or "ue4ss/Mods/MiningBenchMod/dlls/"

local cmdSeq = 0
local function sendMiningCmd(cmd)
    local base = os.time() * 1000
    cmdSeq = (base > cmdSeq) and base or (cmdSeq + 1)
    local fh = io.open(MD .. "MiningCmd.txt", "w")
    if not fh then log("MiningCmd.txt open failed") return end
    fh:write(cmdSeq .. "|0|" .. cmd .. "\n")
    fh:close()
    log("cmd " .. cmdSeq .. "|0|" .. cmd)
end

local function readLines(path)
    local fh = io.open(path, "r")
    if not fh then return nil end
    local out = {}
    for line in fh:lines() do out[#out + 1] = (line:gsub("\r$", "")) end
    fh:close()
    return out
end

local function splitBar(s)
    local out, i = {}, 1
    while true do
        local j = string.find(s, "|", i, true)
        if not j then out[#out + 1] = s:sub(i) return out end
        out[#out + 1] = s:sub(i, j - 1)
        i = j + 1
    end
end

local STATIONS = MD .. "DevStations.txt"
local STRES = MD .. "DevStationResult.txt"

local function stationsRefresh() sendMiningCmd("dev_stations") end

local function stationCancel()
    UI.stPending = nil
    if UI.stConfirm then pcall(function() UI.stConfirm:SetVisibility(1) end) end
end

local function stationAsk(i)
    local r = UI.stRows[i]
    if not r or r.id == 0 then return end
    UI.stPending = { id = r.id, name = r.name }
    pcall(setText, UI.stConfirmTb, "Destroy station '" .. r.name .. "' (id " .. r.id .. ")? Everything inside is deleted. This cannot be undone.")
    UI.stConfirm:SetVisibility(0)
end

local function stationConfirm()
    local p = UI.stPending
    if not p then return end
    stationCancel()
    UI.stSent = p.id
    pcall(setText, UI.stnRes, "sent destroy for '" .. p.name .. "' ...")
    sendMiningCmd("dev_destroy_station:" .. p.id)
end

-- re-read DevStations.txt (rows only when the content changed) and DevStationResult.txt. Called only while the Stations tab is visible.
local function stationsLoad()
    local lines = readLines(STATIONS) or {}
    local sig = table.concat(lines, "\n")
    if sig ~= UI.stSig then
        UI.stSig = sig
        local list = {}
        for _, l in ipairs(lines) do
            local f = splitBar(l)
            if #f >= 5 and f[5] == "1" then list[#list + 1] = { id = tonumber(f[1]) or 0, name = f[2], pl = f[4] } end
        end
        local hidden = #lines - #list
        for i = 1, UI.stRowsN do
            local r, s = UI.stRows[i], list[i]
            if s then
                r.id, r.name = s.id, s.name
                pcall(setText, r.tb, s.name .. "  (id " .. s.id .. ", " .. s.pl .. " platforms)")
                r.hb:SetVisibility(0)
            else
                r.id = 0
                r.hb:SetVisibility(1)
            end
        end
        pcall(setText, UI.stInfo, #list .. " player-built stations" .. (hidden > 0 and (", " .. hidden .. " protected hidden") or "")
            .. (#list > UI.stRowsN and ("; showing first " .. UI.stRowsN) or ""))
    end
    if UI.stSent then
        local res = readLines(STRES)
        if res and res[1] then
            local f = splitBar(res[1])
            if tonumber(f[1]) == UI.stSent then pcall(setText, UI.stnRes, (f[2] or "") .. ": " .. (f[3] or "")) end
        end
    end
end

local TAB_NAMES = { "Items", "Cheats", "Status", "Stations", "Settings" }

function UI.setTab(name)
    UI.activeTab = name
    for _, n in ipairs(TAB_NAMES) do
        local on = (n == name)
        UI.tabs[n]:SetVisibility(on and 0 or 1)
        local t = UI.tabBtns[n]
        pcall(function() t.btn:SetBackgroundColor(on and C_GOLD or C_TABOFF) end)
    end
    if name ~= "Stations" then stationCancel() end
    if name == "Stations" then UI.stSig = nil stationsRefresh() end
    if name == "Status" then refreshStatus(false) end
end

local function amt()
    return math.max(1, math.floor(tonumber(UI.amount:GetText():ToString()) or 1))
end

local function doGive()
    if not UI.sel then setText(UI.status, "pick an item") return end
    local n = amt()
    local it, label = UI.sel, UI.selLabel
    local ok = try("GiveCreativeItems", function()
        ctrl():GiveCreativeItems({ Item = { Item = it, Amount = n } })
    end)
    setText(UI.status, ok and ("gave " .. n .. " x " .. label) or "give failed (see log)")
end

local function doRemove()
    if not UI.sel then setText(UI.status, "pick an item") return end
    local p = player()
    if not p then setText(UI.status, "no player") return end
    local inv = p.Inventory
    local have = inv:GetItemAmount(UI.sel)
    local n = math.min(amt(), have)
    if n <= 0 then setText(UI.status, "none in inventory") return end
    local it, label = UI.sel, UI.selLabel
    local ok = try("RemoveItems", function()
        inv:RemoveItems({ Item = it, Amount = n })
    end)
    setText(UI.status, ok and ("removed " .. n .. " x " .. label) or "remove failed (see log)")
end

local function buildPanel()
    local pc = ctrl()
    local L = layout(pc)
    UI.root = mk("UserWidget", pc)
    UI.rows = {}
    UI.lastQ = nil
    UI.sel = nil
    UI.selLabel = nil
    local tree = mk("WidgetTree", UI.root)
    UI.root.WidgetTree = tree
    UI.tree = tree
    local sb = mk("SizeBox", tree)
    tree.RootWidget = sb
    sb:SetWidthOverride(L.W)
    sb:SetHeightOverride(L.H)
    local border = mk("Border", tree)
    border:SetBrushColor(C_BODY)
    sb:SetContent(border)
    local outer = mk("VerticalBox", tree)
    border:SetContent(outer)

    -- gold title header (game style; the slanted tab edge is not reproducible with plain borders)
    local head = mk("Border", tree)
    head:SetBrushColor(C_GOLD)
    pcall(function() head:SetPadding({ Left = 10, Top = 2, Right = 10, Bottom = 2 }) end)
    local title = mk("TextBlock", tree)
    setText(title, "Dev Menu")
    smallFont(title, 20)
    head:SetContent(title)
    outer:AddChildToVerticalBox(head)

    -- tab bar
    local bar = mk("HorizontalBox", tree)
    UI.tabBtns = {}
    for _, n in ipairs(TAB_NAMES) do
        local btn = mk("Button", tree)
        pcall(function() btn:SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = 0 }) end)
        local tb = mk("TextBlock", tree)
        setText(tb, n)
        smallFont(tb, 14)
        pcall(function() tb:SetJustification(1) end)
        local cs = btn:SetContent(tb)
        pcall(function() cs:SetHorizontalAlignment(2) end)
        local slot = bar:AddChildToHorizontalBox(btn)
        pcall(function() slot:SetSize({ Value = 1, SizeRule = 1 }) end)
        pcall(function() slot:SetHorizontalAlignment(0) end)
        pcall(function() slot:SetPadding({ Left = 1, Top = 0, Right = 1, Bottom = 0 }) end)
        UI.tabBtns[n] = { btn = btn, tb = tb }
        onClick(btn, function() UI.setTab(n) end)
    end
    local barBox = mk("SizeBox", tree)
    barBox:SetHeightOverride(24)
    barBox:SetContent(bar)
    outer:AddChildToVerticalBox(barBox)

    -- content: one VerticalBox per tab, only the active one visible
    local body = mk("VerticalBox", tree)
    local bslot = outer:AddChildToVerticalBox(body)
    pcall(function() bslot:SetPadding({ Left = 8, Top = 6, Right = 8, Bottom = 6 }) end)
    UI.tabs = {}
    for _, n in ipairs(TAB_NAMES) do
        UI.tabs[n] = mk("VerticalBox", tree)
        body:AddChildToVerticalBox(UI.tabs[n])
    end
    local vb = UI.tabs.Items
    UI.vb = vb

    -- Cheats tab (former Ctrl+Num hotkeys)
    addTry("Cheats", "Toggle creative mode", function() toggleCreative() refreshCreative() end)
    UI.creativeDesc = cheatDesc(creativeText())
    addTry("Cheats", "Unlock all research", unlockResearch)
    cheatDesc("Unlock all research: instantly completes the whole tech tree")
    addTry("Cheats", "Warp current ship to pinned sector", function() sendMiningCmd("warp_cur:pinned") end) -- real warp (variant e), uses warp fuel
    cheatDesc("Warp current ship to pinned sector: real warp, uses warp fuel from the drive. Pin a sector on the map first.")
    addTry("Cheats", "Release all chunk holds", function() sendMiningCmd("chunkhold:release_all") end) -- mb_chunkhold.hpp, safety button
    cheatDesc("Releases every chunk hold of the mining loop (far AI miners keep their chunks loaded until the loop stops). Result in UE4SS.log (CHUNKHOLD).")


    -- Status tab (auto-refresh every ~1s while active)
    UI.stCreative = UI.addLabel("Status", "creative: ?")
    UI.stInv = UI.addLabel("Status", "inventory size: ?")
    UI.stRes = UI.addLabel("Status", "research: ?")
    UI.stResNames = UI.addLabel("Status", "")
    addTry("Status", "Refresh", function() refreshStatus(true) end)

    -- Stations tab (dev only): list from DevStations.txt, Destroy with a confirm step
    UI.stRowsN = 12
    UI.stRows = {}
    UI.stSig = nil
    UI.stPending = nil
    UI.stSent = nil
    local stHead = UI.addLabel("Stations", "Player-built stations. Destroy deletes the station and everything inside it.")
    pcall(function() stHead:SetAutoWrapText(true) end)
    UI.addButton("Stations", "Refresh list", stationsRefresh)
    UI.stInfo = UI.addLabel("Stations", " ")
    local stBox = mk("SizeBox", tree)
    stBox:SetHeightOverride(math.max(80, math.floor(L.listH / 2)))
    local stScroll = mk("ScrollBox", tree)
    stBox:SetContent(stScroll)
    UI.tabs.Stations:AddChildToVerticalBox(stBox)
    for i = 1, UI.stRowsN do
        local hb = mk("HorizontalBox", tree)
        local tb = mk("TextBlock", tree)
        setText(tb, " ")
        smallFont(tb, 14)
        local ts = hb:AddChildToHorizontalBox(tb)
        pcall(function() ts:SetSize({ Value = 1, SizeRule = 1 }) end)
        local bx = mk("SizeBox", tree)
        bx:SetWidthOverride(80)
        bx:SetContent(mkBtn("Destroy", function() stationAsk(i) end))
        hadd(hb, bx)
        hb:SetVisibility(1)
        stScroll:AddChild(hb)
        UI.stRows[i] = { hb = hb, tb = tb, id = 0, name = "" }
    end
    UI.stConfirm = mk("VerticalBox", tree)
    UI.stConfirmTb = mk("TextBlock", tree)
    setText(UI.stConfirmTb, " ")
    smallFont(UI.stConfirmTb, 14)
    pcall(function() UI.stConfirmTb:SetAutoWrapText(true) end)
    UI.stConfirm:AddChildToVerticalBox(UI.stConfirmTb)
    local stBtns = mk("HorizontalBox", tree)
    local cbtn = mkBtn("Confirm destroy", stationConfirm)
    pcall(function() cbtn:SetBackgroundColor(C_DANGER) end)
    hadd(stBtns, cbtn)
    hadd(stBtns, mkBtn("Cancel", stationCancel))
    UI.stConfirm:AddChildToVerticalBox(stBtns)
    UI.stConfirm:SetVisibility(1)
    UI.tabs.Stations:AddChildToVerticalBox(UI.stConfirm)
    UI.stnRes = UI.addLabel("Stations", " ")

    -- Settings tab
    UI.addLabel("Settings", "Log: UE4SS.log")

    UI.status = mk("TextBlock", tree)
    setText(UI.status, " ")
    smallFont(UI.status, 14)
    vb:AddChildToVerticalBox(UI.status)

    UI.search = mk("EditableTextBox", tree)
    UI.search:SetHintText(FText("search"))
    vb:AddChildToVerticalBox(UI.search)

    -- action row: amount | Give | Remove
    local act = mk("HorizontalBox", tree)
    UI.amount = mk("EditableTextBox", tree)
    UI.amount:SetText(FText("100"))
    local amtBox = mk("SizeBox", tree)
    amtBox:SetWidthOverride(80)
    amtBox:SetContent(UI.amount)
    hadd(act, amtBox)
    hadd(act, mkBtn("Give", doGive))
    hadd(act, mkBtn("Remove", doRemove))
    vb:AddChildToVerticalBox(act)

    -- fixed-height list (ScrollBox fills it, never grows past it)
    local listBox = mk("SizeBox", tree)
    listBox:SetHeightOverride(L.listH)
    local scroll = mk("ScrollBox", tree)
    listBox:SetContent(scroll)
    vb:AddChildToVerticalBox(listBox)
    UI.listBox = listBox

    local list = FindAllOf(R.item) or {}
    for _, it in ipairs(list) do
        local name = objName(it)
        local okI, icon = pcall(function()
            local ic = it.Icon
            if ic and ic:IsValid() then return ic end
        end)
        if okI and icon and not string.find(name, "Default__", 1, true) and not string.find(name, "Filter", 1, true) then
            local okL, label = pcall(function() return it.DisplayName:ToString() end)
            if not (okL and label and label ~= "") then label = (objName(it):gsub("^ITEM_", "")) end
            local btn = mk("Button", tree)
            local hb = mk("HorizontalBox", tree)
            btn:SetContent(hb)
            local img = mk("Image", tree)
            img:SetBrushFromTexture(icon, false)
            img:SetDesiredSizeOverride({ X = 18, Y = 18 })
            hb:AddChildToHorizontalBox(img)
            local tb = mk("TextBlock", tree)
            setText(tb, label)
            smallFont(tb, 15)
            hb:AddChildToHorizontalBox(tb)
            local box = mk("SizeBox", tree)
            box:SetHeightOverride(24)
            box:SetContent(btn)
            scroll:AddChild(box)
            UI.rows[#UI.rows + 1] = { btn = btn, box = box, item = it, name = label:lower(), label = label }
            onClick(btn, function()
                UI.sel = it
                UI.selLabel = label
                setText(UI.status, "Selected: " .. label)
            end)
        end
    end

    UI.setTab("Items")
    UI.root:SetPositionInViewport({ X = L.X, Y = L.Y }, false)
    UI.root:AddToViewport(50)
    UI.root:SetVisibility(2)

    -- DEV collapse toggle (own widget, below the paint-brush button)
    UI.btnRoot = mk("UserWidget", pc)
    local t2 = mk("WidgetTree", UI.btnRoot)
    UI.btnRoot.WidgetTree = t2
    local bsb = mk("SizeBox", t2)
    t2.RootWidget = bsb
    bsb:SetWidthOverride(L.bs)
    bsb:SetHeightOverride(L.bs)
    local bb = mk("Border", t2)
    bb:SetBrushColor(rgba(255, 255, 255))
    pcall(function() bb:SetPadding({ Left = 2, Top = 2, Right = 2, Bottom = 2 }) end)
    bsb:SetContent(bb)
    local tbtn = mk("Button", t2)
    pcall(function() tbtn:SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = 0 }) end)
    pcall(function() tbtn:SetBackgroundColor(C_TABOFF) end)
    UI.btnTb = mk("TextBlock", t2)
    setText(UI.btnTb, UI.collapsed and ">" or "<")
    smallFont(UI.btnTb, 16)
    pcall(function() UI.btnTb:SetJustification(1) end)
    local bcs = tbtn:SetContent(UI.btnTb)
    pcall(function() bcs:SetHorizontalAlignment(2) end)
    pcall(function() bcs:SetVerticalAlignment(2) end)
    bb:SetContent(tbtn)
    onClick(tbtn, function()
        UI.collapsed = not UI.collapsed
        setText(UI.btnTb, UI.collapsed and ">" or "<")
        if UI.root and UI.root:IsValid() and UI.shown then
            UI.root:SetVisibility(UI.collapsed and 2 or 0)
        end
        log("collapsed=" .. tostring(UI.collapsed))
    end)
    UI.btnRoot:SetPositionInViewport({ X = L.bx, Y = L.by }, false)
    UI.btnRoot:AddToViewport(51)
    UI.btnRoot:SetVisibility(2)
    log("panel built, rows " .. #UI.rows)
end

local function tick()
    if UI.root and not UI.root:IsValid() then
        UI.root = nil
        UI.btnRoot = nil
        UI.shown = false
        UI.rows = {}
    end
    local open, info = invOpen()
    -- no controller or player (map change): treat as closed and rebuild on next open
    if not (live(table.unpack(R.controller))
            and live(table.unpack(R.player))) then
        open = false
        UI.root = nil
        UI.btnRoot = nil
        UI.shown = false
        UI.rows = {}
        UI.lastQ = nil
        UI.sel = nil
    end
    -- Esc forced hide: stays hidden until the inventory is seen closed once
    if open and UI.forceHide then open = false end
    if not open then UI.forceHide = false end
    if UI.f3 then open = true end   -- F3 opens the Dev Menu without the inventory
    if open and not UI.root then try("buildPanel", buildPanel) end
    if open ~= UI.lastOpen then
        UI.lastOpen = open
        log(string.format("panel %s inViewport=%s vis=%s valid=%s",
            tostring(open), info.inViewport, info.vis, tostring(info.valid)))
    end
    if UI.root and open ~= UI.shown then
        UI.shown = open
        UI.root:SetVisibility((open and not UI.collapsed) and 0 or 2)
        if UI.btnRoot then pcall(function() UI.btnRoot:SetVisibility(open and 0 or 2) end) end
    end
    if UI.search and UI.shown then
        local q = UI.search:GetText():ToString():lower()
        if q ~= UI.lastQ then
            UI.lastQ = q
            for _, row in ipairs(UI.rows) do
                row.box:SetVisibility((q == "" or string.find(row.name, q, 1, true)) and 0 or 1)
            end
        end
    end
    -- Status tab auto-refresh: every 4 ticks (~1s), only while shown and Status active
    if UI.shown and UI.activeTab == "Status" then
        UI.stT = (UI.stT or 0) + 1
        if UI.stT >= 4 then
            UI.stT = 0
            refreshStatus(false)
        end
    end
    -- Cheats creative label auto-refresh: every 4 ticks (~1s), only while shown and Cheats active
    if UI.shown and UI.activeTab == "Cheats" then
        UI.cheT = (UI.cheT or 0) + 1
        if UI.cheT >= 4 then
            UI.cheT = 0
            refreshCreative()
        end
    end
    -- Stations tab: re-read the two files every 4 ticks (~1 s), only while shown and active (no commands are sent here)
    if UI.shown and UI.activeTab == "Stations" then
        UI.sT = (UI.sT or 0) + 1
        if UI.sT >= 4 then
            UI.sT = 0
            pcall(stationsLoad)
        end
    end
end

MSCore.events.on("tick", tick)

try("RegisterKeyBind Esc", function()
    RegisterKeyBind(Key.ESCAPE, function()
        pcall(ExecuteInGameThread, function()
            if UI.root and UI.root:IsValid() then UI.root:SetVisibility(2) end
            if UI.btnRoot then pcall(function() UI.btnRoot:SetVisibility(2) end) end
            UI.shown = false
            UI.forceHide = true
            UI.f3 = false
        end)
    end)
end)

try("RegisterKeyBind F3", function()
    RegisterKeyBind(Key.F3, function()
        pcall(ExecuteInGameThread, function()
            UI.f3 = not UI.f3
            local pc = ctrl()
            if pc then pcall(function() pc.bShowMouseCursor = UI.f3 end) end   -- clickable without the inventory
            if not UI.f3 and UI.root and UI.root:IsValid() and not invOpen() then
                UI.root:SetVisibility(2)
                if UI.btnRoot then pcall(function() UI.btnRoot:SetVisibility(2) end) end
                UI.shown = false
            end
        end)
    end)
end)

---------------------------------------------------------------- end

log("loaded. Dev Menu: open inventory or F3")
try("StaticFindObject DMSPlayerController", function()
    local c = StaticFindObject(R.controllerPath)
    log("DMSPlayerController resolved: " .. tostring(c ~= nil and c:IsValid()))
end)
