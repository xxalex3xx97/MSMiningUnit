"""Every list of the window: row widgets (WBP_DockRow, WBP_LogLine, WBP_ItemRow) + their Dyn_ boxes via uew.dyn_list; static row pools removed. Idempotent.
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

ARR = lin('B9B9D2')
LOG_N, ROW_N = 60, 20

# ---- WBP_DockRow: select + trash (Btn_Sel / Txt_Sel / Btn_Del: same names as every select row)
w = new_wbp('WBP_DockRow')
add(w, U.HorizontalBox, 'Row_Root', '', var=False)
sel = add(w, U.Button, 'Btn_Sel', 'Row_Root')
sel.set_editor_property('widget_style', button_style(round_brush(lin('D9D9D9')), round_brush(lin('C38800')), round_brush(lin('C38800'))))
slot(sel, pad=(0, 0, 0, 0), fill=1.0, h='Fill', v='Center')
txt = add(w, U.TextBlock, 'Txt_Sel', 'Btn_Sel')
txt.set_editor_property('text', U.Text(''))
txt.set_editor_property('color_and_opacity', slate_color(U.LinearColor(0, 0, 0, 1)))
f = txt.get_editor_property('font'); f.set_editor_property('size', 11); txt.set_editor_property('font', f)
slot(txt, pad=(8, 4, 8, 4), h='Left', v='Center')
sz = add(w, U.SizeBox, 'Row_DelSz', 'Row_Root', var=False)
sz.set_editor_property('width_override', 40.0); sz.set_editor_property('height_override', 40.0)
sz.set_editor_property('b_override_width_override', True) if False else None
slot(sz, pad=(8, 0, 0, 0), v='Center')
dl = add(w, U.Button, 'Btn_Del', 'Row_DelSz')
st = button_style(round_brush(lin('2E2E2E')), round_brush(lin('3C3C3C')), round_brush(lin('222222')))
dl.set_editor_property('widget_style', st)
ic = add(w, U.Image, 'Img_Del', 'Btn_Del', var=False)
ic.set_editor_property('brush', image_brush('/Game/MiningUnitUI/T_IconTrash.T_IconTrash', ARR, 24))
slot(ic, h='Center', v='Center')
save(w)

# ---- WBP_LogLine: one wrapped text line
w = new_wbp('WBP_LogLine')
text(w, 'Txt', '', size=14, wrap=True)
save(w)

# ---- WBP_ItemRow: icon | select | amount | up/down | trash
w = new_wbp('WBP_ItemRow')
add(w, U.HorizontalBox, 'Row_Root', '', var=False)
isz = add(w, U.SizeBox, 'Sz_Icon', 'Row_Root'); isz.set_editor_property('width_override', 32.0); isz.set_editor_property('height_override', 32.0)
slot(isz, pad=(0, 0, 8, 0), v='Top')
ic = add(w, U.Image, 'Img_Icon', 'Sz_Icon')
ib = U.SlateBrush(); ib.set_editor_property('draw_as', U.SlateBrushDrawType.IMAGE)
dsz = U.DeprecateSlateVector2D(); dsz.set_editor_property('x', 32.0); dsz.set_editor_property('y', 32.0); ib.set_editor_property('image_size', dsz)
ic.set_editor_property('brush', ib); ic.set_editor_property('visibility', U.SlateVisibility.HIDDEN)
sel = add(w, U.Button, 'Btn_Sel', 'Row_Root')
sel.set_editor_property('widget_style', button_style(round_brush(lin('D9D9D9')), round_brush(lin('C38800')), round_brush(lin('C38800'))))
slot(sel, fill=1.0, h='Fill', v='Top')
text(w, 'Txt_Sel', 'Btn_Sel', size=11, color=U.LinearColor(0, 0, 0, 1), slot_kw=dict(pad=(8, 4, 8, 4), h='Left', v='Center'))
asz = add(w, U.SizeBox, 'Sz_Amt', 'Row_Root'); asz.set_editor_property('width_override', 110.0)
slot(asz, pad=(8, 0, 8, 0), v='Top')
eb = add(w, U.EditableTextBox, 'Edt_Amount', 'Sz_Amt')
blk = U.SlateColor(specified_color=U.LinearColor(0, 0, 0, 1))
st = eb.get_editor_property('widget_style')
ts = st.get_editor_property('text_style')
f = ts.get_editor_property('font'); f.set_editor_property('size', 11); ts.set_editor_property('font', f)
ts.set_editor_property('color_and_opacity', blk)
st.set_editor_property('text_style', ts)
for pn in ('foreground_color', 'focused_foreground_color', 'read_only_foreground_color'):
    st.set_editor_property(pn, blk)
eb.set_editor_property('widget_style', st)
ud = add(w, U.VerticalBox, 'Col_UD', 'Row_Root', var=False); slot(ud, pad=(0, 0, 8, 0), v='Center')
icon_button(w, 'Btn_Up', 'Col_UD', 'T_IconUp', 20, ARR, box=(20, 20))
icon_button(w, 'Btn_Down', 'Col_UD', 'T_IconDown', 20, ARR, box=(20, 20))
dsz_ = add(w, U.SizeBox, 'Sz_Del', 'Row_Root'); dsz_.set_editor_property('width_override', 40.0); dsz_.set_editor_property('height_override', 40.0)
slot(dsz_, v='Center')
dl = add(w, U.Button, 'Btn_Del', 'Sz_Del')
dl.set_editor_property('widget_style', button_style(round_brush(lin('2E2E2E')), round_brush(lin('3C3C3C')), round_brush(lin('222222'))))
im = add(w, U.Image, 'Img_Del', 'Btn_Del', var=False)
im.set_editor_property('brush', image_brush('/Game/MiningUnitUI/T_IconTrash.T_IconTrash', ARR, 24)); slot(im, h='Center', v='Center')
save(w)

# ---- main window
main = U.load_asset(ASSETS + '/WBP_MiningUnit')
for i in range(1, LOG_N + 1):
    remove(main, 'Txt_Log_%d' % i)
dyn_list(main, 'Dyn_Log', 'Row_LogScroll', 'WBP_LogLine', spacing=0.0, pad=(6, 2, 12, 2))
for r in range(1, ROW_N + 1):
    remove(main, 'Pnl_ItemRow_%d' % r)
dyn_list(main, 'Dyn_Items', 'Row_Scroll_Page_Items', 'WBP_ItemRow', add_btn='Row_Sz_Btn_ItemAdd')
ORES = ["Cobalt", "Graphite", "Iridium", "Nickel", "Silica", "Titanium", "WaterIce"]
dyn_list(main, 'Dyn_RDock', 'Row_CardInnerRDock', 'WBP_DockRow', add_btn='Row_Sz_Btn_RDockAdd')
for k in ORES:
    dyn_list(main, 'Dyn_UDock_' + k, 'Row_CardInnerUD' + k, 'WBP_DockRow', add_btn='Row_Sz_Btn_UDockAdd_' + k)
save(main)
print('LISTS built')
