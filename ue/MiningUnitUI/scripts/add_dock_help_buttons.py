"""Dock card headers + '?' help buttons (idempotent), plus the 'Park' action button.
Headers are always (re)set. Help button per card is skipped when Btn_DockHelp_<key> exists; Btn_Park skipped when it exists.
Run inside the UE editor (not run by the build). Log: out_add_dock_help_buttons.txt next to this script."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out_add_dock_help_buttons.txt')
open(LOG, 'w').close()


def log(*a):
    with open(LOG, 'a') as f:
        f.write(' '.join(str(x) for x in a) + '\n')


def src(n):
    return ELib.find_source_widget_by_name(wbp, n)


def cp(dst, ref, *props):
    for p in props:
        dst.set_editor_property(p, ref.get_editor_property(p))


def mv(panel, child):
    p, c = src(panel), src(child)
    p.remove_child(c)
    p.add_child_to_vertical_box(c)
    return c


ORES = [('Cobalt', 'Cobalt'), ('Graphite', 'Graphite'), ('Iridium', 'Iridium'), ('Nickel', 'Nickel'),
        ('Silica', 'Silica'), ('Titanium', 'Titanium'), ('WaterIce', 'Water Ice')]

# (1) headers
for k, nice in ORES:
    w = src('Lbl_HdrUD' + k)
    if w is None:
        log('MISSING Lbl_HdrUD' + k)
    else:
        w.set_editor_property('text', U.Text(nice + ' Unload Dock'))
for n, t in [('Lbl_HdrRDock', 'Refuel Docks (tried in order)'), ('Lbl_HdrUDMisc', 'Misc Docks'),
             ('Lbl_HdrUDPark', 'Parking Docks')]:
    w = src(n)
    if w is None:
        log('MISSING ' + n)
    else:
        w.set_editor_property('text', U.Text(t))

# (2) help buttons
ref_btn = src('Btn_UDockAdd_Cobalt')
ref_lbl = src('Lbl_Btn_UDockAdd_Cobalt')
if ref_btn is None or ref_lbl is None:
    log('NEEDS-THINKING: ref Btn_UDockAdd_Cobalt/Lbl_Btn_UDockAdd_Cobalt None, no help buttons')
else:
    # (key, hbox name, kind)
    cards = [(k, 'Row_CardHeadUD' + k, 'ore') for k, _ in ORES]
    cards.append(('RDock', 'Row_CardHeadRDock', 'refuel'))
    cards.append(('Misc', 'Row_CardHeadUDMisc', 'extra'))
    cards.append(('Park', 'Row_CardHeadUDPark', 'extra'))
    for key, hbn, kind in cards:
        if src('Btn_DockHelp_' + key):
            log('FOUND Btn_DockHelp_' + key + ', skip')
            continue
        if kind == 'extra':
            innern = 'Row_CardInnerUD' + key
            inner = src(innern)
            lbl = src('Lbl_HdrUD' + key)
            if inner is None or lbl is None:
                log('MISSING', innern, 'or Lbl_HdrUD' + key)
                continue
            hb = add(wbp, U.HorizontalBox, hbn, innern, var=False)
            slot(hb, pad=(0, 0, 0, 4), h='Fill')
            inner.remove_child(lbl)
            hb.add_child_to_horizontal_box(lbl)
            slot(lbl, v='Center')
            mv(innern, 'Row_Sz_Row_CardLineUD' + key)
            mv(innern, 'Dyn_UDock_' + key)
            mv(innern, 'Row_Sz_Btn_UDockAdd_' + key)
            slot(src('Row_Sz_Row_CardLineUD' + key), pad=(0, 0, 0, 8), h='Fill')
            slot(src('Dyn_UDock_' + key), h='Fill')
            slot(src('Row_Sz_Btn_UDockAdd_' + key), pad=(0, 4, 0, 0), h='Fill')
            if inner.get_child_index(hb) != 0:
                log('NEEDS-THINKING: header reorder failed', key)
                continue
        else:
            hb = src(hbn)
            if hb is None:
                log('MISSING', hbn)
                continue
            if kind == 'ore':
                l = src('Lbl_HdrUD' + key)
                img = src('Img_UDockOre_' + key)
                if l is None or img is None:
                    log('MISSING Lbl_HdrUD' + key + ' or Img_UDockOre_' + key)
                    continue
                hb.remove_child(l)
                hb.add_child_to_horizontal_box(l)
                slot(img, pad=(0, 0, 8, 0), v='Center')
                slot(l, pad=(0, 0, 0, 0), v='Center')
        sp = add(wbp, U.SizeBox, 'Row_HelpSp_' + key, hbn, var=False)
        slot(sp, fill=1.0, h='Fill')
        szn = 'Row_Sz_Btn_DockHelp_' + key
        sz = add(wbp, U.SizeBox, szn, hbn, var=False)
        sz.set_editor_property('width_override', 32.0)
        sz.set_editor_property('height_override', 28.0)
        slot(sz, pad=(8, 0, 0, 0), v='Center')
        b = add(wbp, U.Button, 'Btn_DockHelp_' + key, szn, var=False)
        cp(b, ref_btn, 'background_color', 'widget_style')
        t = text(wbp, 'Txt_DockHelp_' + key, 'Btn_DockHelp_' + key, size=15, var=False)
        cp(t, ref_lbl, 'font', 'color_and_opacity')
        t.set_editor_property('text', U.Text('?'))
        slot(t, pad=(0, 0, 0, 0), h='Center', v='Center')
        log('CREATED Btn_DockHelp_' + key)

# (3) Btn_Park
if src('Btn_Park'):
    log('FOUND Btn_Park, skip')
else:
    ub, ut, csz = src('Btn_Unload'), src('Txt_Unload'), src('Row_Sz_Btn_Collect')
    if ub is None or ut is None or csz is None:
        log('MISSING Btn_Unload/Txt_Unload/Row_Sz_Btn_Collect:', ub is None, ut is None, csz is None)
    else:
        sz = add(wbp, U.SizeBox, 'Row_Sz_Btn_Park', 'Row_ActionsBody', var=False)
        sz.set_editor_property('min_desired_height', 56.0)
        slot(sz, pad=(0, 6, 0, 0), h='Fill')
        b = add(wbp, U.Button, 'Btn_Park', 'Row_Sz_Btn_Park')
        cp(b, ub, 'background_color', 'widget_style')
        t = text(wbp, 'Txt_Park', 'Btn_Park', size=18, var=True)
        cp(t, ut, 'font', 'color_and_opacity')
        t.set_editor_property('text', U.Text('Park'))
        slot(t, pad=(14, 8, 14, 8), h='Center', v='Center')
        mv('Row_ActionsBody', 'Row_Sz_Btn_Collect')
        slot(src('Row_Sz_Btn_Collect'), pad=(0, 6, 0, 0), h='Fill')
        log('CREATED Btn_Park')

save(wbp)
log('SAVED')
