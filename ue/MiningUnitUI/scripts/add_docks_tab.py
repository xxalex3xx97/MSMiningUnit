"""Docks tab for WBP_MiningUnit: WBP_NetQueueRow (one waiting ship), WBP_NetDockCard (one dock: status icon | name | status | used by |
purpose | since | queue = Dyn_Queue of WBP_NetQueueRow in a ScrollBox capped at 3 rows), Btn_TabDocks + Txt_TabDocks (after the Log tab),
Pnl_PageDocks (PageSwitcher index 4) with the column header Row_DocksHdr, a gold line, ScrollBox Row_DocksScroll and the Dyn_Docks list.
Idempotent: every part is skipped when it exists (the row WBPs are built only when missing; header/dyn_list are re-applied each run).
Lua fills the rows (wbp_loader.lua, docksPaint). The old WBP_NetDockRow / Txt_DocksHdr / Sz_DocksLine stay in the assets, unused (collapsed).
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed). Not run by the build."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

WHITE = lin('FFFFFF')
GOLD = lin('F2B01E')
DIM = lin('FFFFFF', 0.6)
CARD = lin('FFFFFF', 0.06)

# column fill weights, shared by the card and the header so they line up
COLS = [('Name', 3.0), ('Status', 2.0), ('Used', 3.0), ('Purpose', 1.5), ('Since', 1.0), ('Queue', 6.5)]
FILL = dict(COLS)
ICON_W = 22.0       # status icon box
CARD_PAD = 8.0      # card border padding left/right
# every icon the card / queue rows show (hidden source images in the card give Lua the textures)
ICONS = [('idle', 'StIdle'), ('mining', 'StMining'), ('docking', 'StDocking'), ('undocking', 'StUndocking'), ('refueling', 'StRefueling'),
         ('unloading', 'StUnloading'), ('zz', 'Zz'), ('cargo', 'Cargo'), ('fuel', 'Fuel'), ('clock', 'Clock'), ('pause', 'Pause'), ('ship', 'Ship')]


def ellipsis(t):
    try:
        t.set_editor_property('text_overflow_policy', U.TextOverflowPolicy.ELLIPSIS)
    except Exception as e:
        print('ellipsis not set:', e)


# ---- WBP_NetQueueRow: reason icon | ship | reason value (coloured) | phase icon | slot
if not U.EditorAssetLibrary.does_asset_exist(ASSETS + '/WBP_NetQueueRow'):
    w = new_wbp('WBP_NetQueueRow')
    add(w, U.HorizontalBox, 'Row_Root', '', var=False)
    qz = add(w, U.SizeBox, 'Sz_QReason', 'Row_Root', var=False)
    qz.set_editor_property('width_override', 18.0); qz.set_editor_property('height_override', 18.0)
    slot(qz, pad=(0, 0, 4, 0), v='Center')
    qi = add(w, U.Image, 'Img_QReason', 'Sz_QReason', var=False)
    qi.set_editor_property('brush', image_brush(ASSETS + '/T_IconZz', WHITE, 18.0))
    t = text(w, 'Txt_QName', 'Row_Root', size=13, color=WHITE, wrap=True, var=False, slot_kw=dict(pad=(4, 1, 4, 1), fill=3.0, h='Fill', v='Center'))
    t = text(w, 'Txt_QWhy', 'Row_Root', size=13, color=DIM, var=False, slot_kw=dict(pad=(4, 1, 4, 1), fill=1.6, h='Fill', v='Center')); ellipsis(t)
    pz = add(w, U.SizeBox, 'Sz_QPhase', 'Row_Root', var=False)
    pz.set_editor_property('width_override', 16.0); pz.set_editor_property('height_override', 16.0)
    slot(pz, pad=(4, 0, 4, 0), v='Center')
    pi = add(w, U.Image, 'Img_QPhase', 'Sz_QPhase', var=False)
    pi.set_editor_property('brush', image_brush(ASSETS + '/T_IconShip', WHITE, 16.0))
    t = text(w, 'Txt_QSlot', 'Row_Root', size=13, color=DIM, var=False, slot_kw=dict(pad=(4, 1, 4, 1), fill=1.0, h='Fill', v='Center')); ellipsis(t)
    save(w)

# ---- WBP_NetDockCard
if not U.EditorAssetLibrary.does_asset_exist(ASSETS + '/WBP_NetDockCard'):
    w = new_wbp('WBP_NetDockCard')
    bd = add(w, U.Border, 'Bdr_DCard', '', var=False)
    bd.set_editor_property('brush_color', CARD)
    bd.set_editor_property('padding', U.Margin(CARD_PAD, 6, CARD_PAD, 6))
    add(w, U.VerticalBox, 'Col_Root', 'Bdr_DCard', var=False)
    # hidden icon sources: Lua reads Img_Src_<key>.Brush.ResourceObject (no soft-path loads)
    src = add(w, U.HorizontalBox, 'Row_DSrc', 'Col_Root', var=False)
    src.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
    for key, asset in ICONS:
        im = add(w, U.Image, 'Img_Src_' + key, 'Row_DSrc', var=False)
        im.set_editor_property('brush', image_brush(ASSETS + '/T_Icon' + asset, WHITE, 18.0))
    add(w, U.HorizontalBox, 'Row_Root', 'Col_Root', var=False)
    isz = add(w, U.SizeBox, 'Sz_DIcon', 'Row_Root', var=False)
    isz.set_editor_property('width_override', ICON_W); isz.set_editor_property('height_override', ICON_W)
    slot(isz, v='Center')
    ic = add(w, U.Image, 'Img_DIcon', 'Sz_DIcon', var=False)
    ic.set_editor_property('brush', image_brush(ASSETS + '/T_IconStIdle', WHITE, 20.0))
    for key, fill in COLS[:-1]:
        t = text(w, 'Txt_D' + key, 'Row_Root', size=15 if key == 'Name' else 14, wrap=True, var=False,
                 slot_kw=dict(pad=(8, 2, 8, 2), fill=fill, h='Fill', v='Center'))
    # queue cell: "-" when empty, else up to 3 rows (one waiting ship each; Lua caps it) + "+n waiting"
    qc = add(w, U.VerticalBox, 'Box_DQueue', 'Row_Root', var=False)
    slot(qc, pad=(8, 2, 8, 2), fill=FILL['Queue'], h='Fill', v='Center')
    text(w, 'Txt_DQueueNone', 'Box_DQueue', size=14, color=DIM, var=False, slot_kw=dict(h='Left'))
    dq = dyn_box(w, 'Dyn_Queue', 'Box_DQueue', 'WBP_NetQueueRow', spacing=2.0)
    slot(dq, h='Fill')
    text(w, 'Txt_DQueueMore', 'Box_DQueue', size=13, color=lin('F2D21E'), var=False, slot_kw=dict(pad=(22, 2, 0, 0), h='Left'))
    save(w)
main = U.load_asset(ASSETS + '/WBP_MiningUnit')

# ---- tab button after Btn_TabLog (copy Log tab style); the Log tab stops being the last one (right pad 3)
if not ELib.find_source_widget_by_name(main, 'Btn_TabDocks'):
    lg = ELib.find_source_widget_by_name(main, 'Btn_TabLog')
    ltxt = ELib.find_source_widget_by_name(main, 'Txt_TabLog')
    slot(lg, pad=(3, 0, 3, 0), fill=1.0, h='Fill', v='Fill')
    b = add(main, U.Button, 'Btn_TabDocks', 'TabBar')
    b.set_editor_property('background_color', lg.get_editor_property('background_color'))
    slot(b, pad=(3, 0, 0, 0), fill=1.0, h='Fill', v='Fill')
    t = text(main, 'Txt_TabDocks', 'Btn_TabDocks', size=16, var=True, slot_kw=dict(pad=(16, 8, 16, 8), h='Center', v='Center'))
    t.set_editor_property('text', U.Text('Docks'))
    t.set_editor_property('font', ltxt.get_editor_property('font'))
    t.set_editor_property('color_and_opacity', ltxt.get_editor_property('color_and_opacity'))

# ---- page: header, scroll, Dyn_Docks (next child of the page switcher = index 4)
if not ELib.find_source_widget_by_name(main, 'Pnl_PageDocks'):
    pg = add(main, U.VerticalBox, 'Pnl_PageDocks', 'PageSwitcher')
    slot(pg, h='Fill', v='Fill')
    hd = text(main, 'Txt_DocksHdr', 'Pnl_PageDocks', size=14, color=GOLD, var=True,
              slot_kw=dict(pad=(6, 0, 6, 6), h='Fill'))
    hd.set_editor_property('text', U.Text('Dock | Status | Used by | Purpose | Since | Waiting'))
    sb = add(main, U.ScrollBox, 'Row_DocksScroll', 'Pnl_PageDocks', var=True)
    cmw = getattr(U.ConsumeMouseWheel, 'WHEN_SCROLLING_POSSIBLE', None)
    if cmw is not None:
        sb.set_editor_property('consume_mouse_wheel', cmw)
    slot(sb, fill=1.0, h='Fill', v='Fill')

# ---- table header: same columns/weights/pads as WBP_NetDockCard; the old one-line Txt_DocksHdr is collapsed
if not ELib.find_source_widget_by_name(main, 'Row_DocksHdr'):
    ELib.find_source_widget_by_name(main, 'Txt_DocksHdr').set_visibility(U.SlateVisibility.COLLAPSED)
    hr = add(main, U.HorizontalBox, 'Row_DocksHdr', 'Pnl_PageDocks', var=False)
    hz = add(main, U.SizeBox, 'Sz_DocksHdrDot', 'Row_DocksHdr', var=False)
    for name, label in [('Txt_HName', 'Dock'), ('Txt_HStatus', 'Status'), ('Txt_HUsed', 'Used by'),
                        ('Txt_HPurpose', 'Purpose'), ('Txt_HSince', 'Since'), ('Txt_HQueue', 'Waiting (queue)')]:
        t = text(main, name, 'Row_DocksHdr', size=14, color=GOLD, var=False)
        t.set_editor_property('text', U.Text(label))
# (re)apply header geometry every run so it follows COLS / ICON_W / CARD_PAD
hr = ELib.find_source_widget_by_name(main, 'Row_DocksHdr')
slot(hr, pad=(6, 0, 12, 0), h='Fill')
hz = ELib.find_source_widget_by_name(main, 'Sz_DocksHdrDot')
hz.set_editor_property('width_override', ICON_W)
slot(hz, pad=(CARD_PAD, 0, 0, 0))
for key, fill in COLS:
    slot(ELib.find_source_widget_by_name(main, 'Txt_H' + key), pad=(8, 2, 8, 4), fill=fill, h='Fill', v='Center')
# the dyn_list pad is (6, 2, 12, 2) + card padding; keep the header right edge in line with the cards
slot(hr, pad=(6, 0, 12 + CARD_PAD, 0), h='Fill')

# ---- "?" after the queue header: toggles Pnl_DocksLegend (what every icon / colour means); Lua binds the click
if not ELib.find_source_widget_by_name(main, 'Btn_DocksHelp'):
    hb = add(main, U.Button, 'Btn_DocksHelp', 'Row_DocksHdr')
    hb.set_editor_property('background_color', lin('FFFFFF', 0.18))
    slot(hb, pad=(4, 0, 0, 2), v='Center')
    ht = text(main, 'Txt_DocksHelp', 'Btn_DocksHelp', size=14, color=GOLD, var=False, slot_kw=dict(pad=(8, 0, 8, 0), h='Center', v='Center'))
    ht.set_editor_property('text', U.Text('?'))
# ---- gold line under the header (a Border: its padding is its height). The first attempt, Sz_DocksLine + Img_DocksLine, is collapsed.
if not ELib.find_source_widget_by_name(main, 'Bdr_DocksLine'):
    old = ELib.find_source_widget_by_name(main, 'Sz_DocksLine')
    if old:
        old.set_visibility(U.SlateVisibility.COLLAPSED)
    ln = add(main, U.Border, 'Bdr_DocksLine', 'Pnl_PageDocks', var=False)
    ln.set_editor_property('brush_color', GOLD)
    ln.set_editor_property('padding', U.Margin(0, 1, 0, 1))
    slot(ln, pad=(6, 0, 12, 6), h='Fill')
    slot(move_last('Pnl_PageDocks', main, 'Row_DocksScroll'), fill=1.0, h='Fill', v='Fill')

# ---- legend panel (collapsed; the "?" toggles it): icon + meaning, two columns
if not ELib.find_source_widget_by_name(main, 'Pnl_DocksLegend'):
    lg = add(main, U.Border, 'Pnl_DocksLegend', 'Pnl_PageDocks', var=True)
    lg.set_editor_property('brush_color', lin('FFFFFF', 0.08))
    lg.set_editor_property('padding', U.Margin(10, 6, 10, 6))
    lg.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
    slot(lg, pad=(6, 0, 12, 6), h='Fill')
    add(main, U.HorizontalBox, 'Row_LegendCols', 'Pnl_DocksLegend', var=False)
    left = [('StIdle', 'Dock free'), ('StDocking', 'Dock taken / ship parked'), ('StMining', 'Ship working at the dock'),
            ('StRefueling', 'Refueling'), ('StUnloading', 'Unloading (full dock = red)')]
    right = [('Zz', 'Waiting for a free dock'), ('Cargo', 'Cargo %: green low, yellow half, red nearly full'), ('Fuel', 'Fuel low (red)'),
             ('Clock', 'Waited too long: goes first (orange)'), ('Ship', 'Flying to the dock'), ('Pause', 'Holding near the dock (slot = place)')]
    for ci, items in enumerate((left, right)):
        col = add(main, U.VerticalBox, 'Col_Legend%d' % ci, 'Row_LegendCols', var=False)
        slot(col, pad=(0, 0, 24, 0), fill=1.0, h='Fill')
        for ii, (asset, label) in enumerate(items):
            rn, zn, imn, tn = 'Row_Legend%d_%d' % (ci, ii), 'Sz_Legend%d_%d' % (ci, ii), 'Img_Legend%d_%d' % (ci, ii), 'Txt_Legend%d_%d' % (ci, ii)
            rw = add(main, U.HorizontalBox, rn, 'Col_Legend%d' % ci, var=False)
            slot(rw, pad=(0, 2, 0, 2))
            sz = add(main, U.SizeBox, zn, rn, var=False)
            sz.set_editor_property('width_override', 18.0); sz.set_editor_property('height_override', 18.0)
            slot(sz, pad=(0, 0, 8, 0), v='Center')
            im = add(main, U.Image, imn, zn, var=False)
            im.set_editor_property('brush', image_brush(ASSETS + '/T_Icon' + asset, WHITE, 18.0))
            tt = text(main, tn, rn, size=13, color=WHITE, var=False, slot_kw=dict(v='Center'))
            tt.set_editor_property('text', U.Text(label))
    slot(move_last('Pnl_PageDocks', main, 'Row_DocksScroll'), fill=1.0, h='Fill', v='Fill')
# (re)apply every legend icon: the panel is built once, but its textures may not have existed on that first run
LEGEND_ICONS = {'Img_Legend0_%d' % i: a for i, a in enumerate(['StIdle', 'StDocking', 'StMining', 'StRefueling', 'StUnloading'])}
LEGEND_ICONS.update({'Img_Legend1_%d' % i: a for i, a in enumerate(['Zz', 'Cargo', 'Fuel', 'Clock', 'Ship', 'Pause'])})
for _n, _a in LEGEND_ICONS.items():
    _w = ELib.find_source_widget_by_name(main, _n)
    if _w:
        _w.set_editor_property('brush', image_brush(ASSETS + '/T_Icon' + _a, WHITE, 18.0))
# header icon spacer: an empty SizeBox collapses to 0 wide, so give it a child; queue header starts after the ship icon
if ELib.find_source_widget_by_name(main, 'Spc_DocksHdr'):
    remove(main, 'Spc_DocksHdr')
if not ELib.find_source_widget_by_name(main, 'Img_DocksHdrDot'):
    _hi = add(main, U.Image, 'Img_DocksHdrDot', 'Sz_DocksHdrDot', var=False)
    _hi.set_editor_property('brush', image_brush(ASSETS + '/T_IconStIdle', lin('FFFFFF', 0.0), ICON_W))
slot(ELib.find_source_widget_by_name(main, 'Txt_HQueue'), pad=(8 + 22, 2, 8, 4), fill=FILL['Queue'], h='Fill', v='Center')
dyn_list(main, 'Dyn_Docks', 'Row_DocksScroll', 'WBP_NetDockCard', spacing=6.0, pad=(6, 2, 12, 2))
save(main)
print('SAVED')
