"""Add the 'Parking docks' card at the end of the Ship tab and set the tag-hint headers (idempotent).
Card Pnl_UDockCard_Park = copy of the Cobalt unload-dock card (Pnl_UDockCard_Cobalt); Lua shows it (starts Collapsed).
Skips the create block when Pnl_UDockCard_Park exists; the header texts are always (re)set. Modelled on add_misc_dock_card.py.
gen_paste.py + build_lists.py build the same card shell; this script is for the live asset.
Run inside the UE editor (not run by the build)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')


def src(n):
    return ELib.find_source_widget_by_name(wbp, n)


def cp(dst, ref, *props):
    for p in props:
        dst.set_editor_property(p, ref.get_editor_property(p))


def cp_slot(dst, ref, *props):
    for p in props:
        try:
            dst.slot.set_editor_property(p, ref.slot.get_editor_property(p))
        except Exception as e:
            print('cp_slot skip', p, e)


if src('Pnl_UDockCard_Park'):
    print('FOUND Pnl_UDockCard_Park, skip')
else:
    refs = ['Pnl_UDockCard_Cobalt', 'Row_CardBodyUDCobalt', 'Lbl_HdrUDCobalt', 'Row_CardLineUDCobalt',
            'Row_Sz_Row_CardLineUDCobalt', 'Dyn_UDock_Cobalt', 'Row_Sz_Btn_UDockAdd_Cobalt',
            'Btn_UDockAdd_Cobalt', 'Lbl_Btn_UDockAdd_Cobalt', 'Row_Scroll_Page_Ship']
    R = {n: src(n) for n in refs}
    missing = [n for n, w in R.items() if w is None]
    print('refs found:', [n for n in refs if n not in missing])
    if missing:
        print('NEEDS-THINKING: reference widget(s) None:', missing)
    else:
        sc = 'Row_Scroll_Page_Ship'
        card = add(wbp, U.Border, 'Pnl_UDockCard_Park', sc)
        cp(card, R['Pnl_UDockCard_Cobalt'], 'background', 'brush_color', 'padding', 'content_color_and_opacity')
        cp_slot(card, R['Pnl_UDockCard_Cobalt'], 'padding', 'horizontal_alignment', 'vertical_alignment')
        card.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)

        body = add(wbp, U.Border, 'Row_CardBodyUDPark', 'Pnl_UDockCard_Park', var=False)
        cp(body, R['Row_CardBodyUDCobalt'], 'background', 'brush_color', 'padding', 'content_color_and_opacity')
        slot(body, h='Fill', v='Fill')

        inner = add(wbp, U.VerticalBox, 'Row_CardInnerUDPark', 'Row_CardBodyUDPark', var=False)
        slot(inner, h='Fill', v='Fill')

        t = add(wbp, U.TextBlock, 'Lbl_HdrUDPark', 'Row_CardInnerUDPark', var=False)
        t.set_editor_property('text', U.Text('Parking docks (idle, queue, stop loop)  (auto: name has [PARKING])'))
        cp(t, R['Lbl_HdrUDCobalt'], 'font', 'color_and_opacity')
        slot(t, pad=(0, 0, 0, 4))

        lsz = add(wbp, U.SizeBox, 'Row_Sz_Row_CardLineUDPark', 'Row_CardInnerUDPark', var=False)
        lsz.set_editor_property('min_desired_height', 2.0)
        slot(lsz, pad=(0, 0, 0, 8), h='Fill')
        ln = add(wbp, U.Border, 'Row_CardLineUDPark', 'Row_Sz_Row_CardLineUDPark', var=False)
        cp(ln, R['Row_CardLineUDCobalt'], 'background', 'brush_color')

        d = dyn_list(wbp, 'Dyn_UDock_Park', 'Row_CardInnerUDPark', 'WBP_DockRow')
        cp(d, R['Dyn_UDock_Cobalt'], 'entry_spacing')
        cp_slot(d, R['Dyn_UDock_Cobalt'], 'padding')

        bsz = add(wbp, U.SizeBox, 'Row_Sz_Btn_UDockAdd_Park', 'Row_CardInnerUDPark', var=False)
        cp(bsz, R['Row_Sz_Btn_UDockAdd_Cobalt'], 'width_override', 'height_override', 'min_desired_width',
           'min_desired_height', 'max_desired_width', 'max_desired_height')
        slot(bsz, pad=(0, 4, 0, 0), h='Fill')
        b = add(wbp, U.Button, 'Btn_UDockAdd_Park', 'Row_Sz_Btn_UDockAdd_Park')
        cp(b, R['Btn_UDockAdd_Cobalt'], 'background_color', 'widget_style')
        bt = add(wbp, U.TextBlock, 'Lbl_Btn_UDockAdd_Park', 'Btn_UDockAdd_Park', var=False)
        bt.set_editor_property('text', U.Text('Add dock'))
        cp(bt, R['Lbl_Btn_UDockAdd_Cobalt'], 'font', 'color_and_opacity')
        slot(bt, pad=(14, 8, 14, 8), h='Center', v='Center')
        print('CREATED Pnl_UDockCard_Park, Dyn_UDock_Park, Btn_UDockAdd_Park')

# tag-hint headers (always, idempotent)
for k, nice in [('Cobalt', 'Cobalt'), ('Graphite', 'Graphite'), ('Iridium', 'Iridium'), ('Nickel', 'Nickel'),
                ('Silica', 'Silica'), ('Titanium', 'Titanium'), ('WaterIce', 'Water Ice')]:
    w = src('Lbl_HdrUD' + k)
    if w is None:
        print('MISSING Lbl_HdrUD' + k)
    else:
        w.set_editor_property('text', U.Text('Unload docks: %s  (auto: name has [%s], add [AUTO-FILTER] to also set its filter)' % (nice, k.upper())))
w = src('Lbl_HdrUDMisc')
if w is None:
    print('MISSING Lbl_HdrUDMisc')
else:
    w.set_editor_property('text', U.Text('Misc docks (leftover cargo, last)  (auto: name has [MISC])'))
save(wbp)
print('SAVED')
