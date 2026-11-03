"""Docks tab for WBP_MiningUnit: WBP_NetDockRow (row), Btn_TabDocks + Txt_TabDocks (after the Log tab), Pnl_PageDocks
(PageSwitcher index 4) with header Txt_DocksHdr, ScrollBox Row_DocksScroll and the Dyn_Docks list. Idempotent: every part is
skipped when it exists (the row WBP is built only when missing). Lua fills the rows (wbp_loader.lua, docksPaint).
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed). Not run by the build."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

WHITE = lin('FFFFFF')
GOLD = lin('F2B01E')

# ---- WBP_NetDockRow: status dot | name | status | used by | purpose | since | waiting (Fill weights; header is one line)
if not U.EditorAssetLibrary.does_asset_exist(ASSETS + '/WBP_NetDockRow'):
    w = new_wbp('WBP_NetDockRow')
    add(w, U.HorizontalBox, 'Row_Root', '', var=False)
    dsz = add(w, U.SizeBox, 'Sz_DDot', 'Row_Root', var=False)
    dsz.set_editor_property('width_override', 10.0); dsz.set_editor_property('height_override', 10.0)
    slot(dsz, pad=(6, 0, 0, 0), v='Center')
    dot = add(w, U.Image, 'Img_DDot', 'Sz_DDot', var=False)
    dot.set_editor_property('brush', round_brush(WHITE, 5.0))
    cols = [('Txt_DName', 3.0), ('Txt_DStatus', 2.0), ('Txt_DUsed', 2.0), ('Txt_DPurpose', 1.5), ('Txt_DSince', 1.0), ('Txt_DQueue', 4.0)]
    for name, fill in cols:
        text(w, name, 'Row_Root', size=14, wrap=(name == 'Txt_DQueue'), var=False,
             slot_kw=dict(pad=(8, 2, 8, 2), fill=fill, h='Fill', v='Center'))
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
dyn_list(main, 'Dyn_Docks', 'Row_DocksScroll', 'WBP_NetDockRow', spacing=2.0, pad=(6, 2, 12, 2))
save(main)
print('SAVED')
