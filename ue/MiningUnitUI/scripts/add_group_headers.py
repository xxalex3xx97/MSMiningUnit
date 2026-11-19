"""Add group headers: Row_DGroup in WBP_NetDockCard, and Row_LogGrp_<f> (name + fold buttons) before each Pnl_LogCard_<f> in
Row_LogAllScroll of WBP_MiningUnit, f=1..20. Idempotent. Run inside the UE editor (not run by the build)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out_add_group_headers.txt')


def log(m):
    with open(OUT, 'a') as fh:
        fh.write(str(m) + '\n')


def to_end(sb, wd):
    sb.remove_child_at(sb.get_child_index(wd))
    sb.add_child(wd)


try:
    c = U.load_asset(ASSETS + '/WBP_NetDockCard')
    dock_group_header(c)
    save(c)
    log('dock header ok')

    wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
    find = ELib.find_source_widget_by_name
    sb = find(wbp, 'Row_LogAllScroll')
    D9, DK = lin('D9D9D9'), lin('1A1A1A')
    for f in range(1, 21):
        if not find(wbp, 'Row_LogGrp_%d' % f):
            rn, hn, fn = 'Row_LogGrp_%d' % f, 'Btn_LogGrpHdr_%d' % f, 'Btn_LogGrpFold_%d' % f
            r = add(wbp, U.HorizontalBox, rn, 'Row_LogAllScroll', var=False)
            r.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
            slot(r, pad=(0, 0, 12, 8), h='Fill')
            hb = add(wbp, U.Button, hn, rn, var=False)
            hb.set_editor_property('background_color', D9)
            slot(hb, fill=1.0, h='Fill', v='Fill')
            text(wbp, 'Txt_LogGrpName_%d' % f, hn, size=18, color=DK, slot_kw=dict(pad=(12, 6, 12, 6), h='Left', v='Center'))
            fb = add(wbp, U.Button, fn, rn, var=False)
            fb.set_editor_property('background_color', D9)
            slot(fb, pad=(6, 0, 0, 0), v='Fill')
            t = text(wbp, 'Txt_LogGrpFold_%d' % f, fn, size=18, color=DK, slot_kw=dict(pad=(14, 6, 14, 6), h='Center', v='Center'))
            t.set_editor_property('text', U.Text('v'))
            log('added %d' % f)
        card = find(wbp, 'Pnl_LogCard_%d' % f)
        if card:
            to_end(sb, card)
        else:
            log('MISSING Pnl_LogCard_%d' % f)
    names = [sb.get_child_at(i).get_name() for i in range(sb.get_children_count())]
    log(names)
    want = []
    for f in range(1, 21):
        want += ['Row_LogGrp_%d' % f, 'Pnl_LogCard_%d' % f]
    if [n for n in names if n in want] != want:
        log('NEEDS-THINKING: %s' % names)
    else:
        save(wbp)
        log('SAVED')
except Exception:
    import traceback
    log('ERROR ' + traceback.format_exc())
    raise
