"""Moves each dock card's Add button into the card header and adds a fold (v/>) button (idempotent).
Header becomes [lead][title][icons][spacer][Add dock][?][fold]. Skipped per card when Btn_DockFold_<key> exists.
Run inside the UE editor (not run by the build). Log: out_add_dock_card_fold.txt next to this script."""
import sys, os, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out_add_dock_card_fold.txt')
open(LOG, 'w').close()


def log(*a):
    with open(LOG, 'a') as f:
        f.write(' '.join(str(x) for x in a) + '\n')


def src(n):
    return ELib.find_source_widget_by_name(wbp, n)


def cp(dst, ref, *props):
    for p in props:
        dst.set_editor_property(p, ref.get_editor_property(p))


ORES = ['Cobalt', 'Graphite', 'Iridium', 'Nickel', 'Silica', 'Titanium', 'WaterIce']
CARDS = [(k, 'Row_CardHeadUD' + k, 'Btn_UDockAdd_' + k) for k in ORES]
CARDS += [('RDock', 'Row_CardHeadRDock', 'Btn_RDockAdd'), ('Misc', 'Row_CardHeadUDMisc', 'Btn_UDockAdd_Misc'),
          ('Park', 'Row_CardHeadUDPark', 'Btn_UDockAdd_Park')]

try:
    for key, hbn, addn in CARDS:
        if src('Btn_DockFold_' + key):
            log('SKIP Btn_DockFold_' + key)
            continue
        hb, asz, hsz = src(hbn), src('Row_Sz_' + addn), src('Row_Sz_Btn_DockHelp_' + key)
        if hb is None or asz is None or hsz is None:
            log('MISSING', key, hb is None, asz is None, hsz is None)
            continue
        old = asz.get_parent()
        old.remove_child(asz)
        hb.add_child_to_horizontal_box(asz)
        asz.set_editor_property('min_desired_height', 28.0)
        slot(asz, pad=(8, 0, 0, 0), v='Center')
        p = asz.get_parent()
        if p is None or p.get_name() != hbn:
            asz.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
            log('FALLBACK collapsed', key)
        hb.remove_child(hsz)
        hb.add_child_to_horizontal_box(hsz)
        slot(hsz, pad=(8, 0, 0, 0), v='Center')
        szn = 'Row_Sz_Btn_DockFold_' + key
        sz = add(wbp, U.SizeBox, szn, hbn, var=False)
        sz.set_editor_property('width_override', 32.0)
        sz.set_editor_property('height_override', 28.0)
        slot(sz, pad=(6, 0, 0, 0), v='Center')
        b = add(wbp, U.Button, 'Btn_DockFold_' + key, szn)
        cp(b, src('Btn_DockHelp_' + key), 'background_color', 'widget_style')
        t = text(wbp, 'Txt_DockFold_' + key, 'Btn_DockFold_' + key, size=15, var=True)
        cp(t, src('Txt_DockHelp_' + key), 'font', 'color_and_opacity')
        t.set_editor_property('text', U.Text('v'))
        slot(t, pad=(0, 0, 0, 0), h='Center', v='Center')
        if not (hb.get_child_index(asz) < hb.get_child_index(hsz) < hb.get_child_index(src(szn))):
            log('NEEDS-THINKING: header order wrong', key)
        else:
            log('CREATED Btn_DockFold_' + key)
    save(wbp)
    log('SAVED')
except Exception:
    log(traceback.format_exc())
