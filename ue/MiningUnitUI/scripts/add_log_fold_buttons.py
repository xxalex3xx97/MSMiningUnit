"""Add a fold/unfold icon button (Btn_LogFold_<f>) to each log card header Row_LogCardHead_<f>, f=1..20.
Idempotent: skips cards that already have Btn_LogFold_<f>. Run inside the UE editor (not run by the build)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out_add_log_fold_buttons.txt')


def log(m):
    with open(OUT, 'a') as fh:
        fh.write(str(m) + '\n')


def tex(n):
    return '%s/%s.%s' % (ASSETS, n, n)


try:
    wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
    find = ELib.find_source_widget_by_name
    for f in range(1, 21):
        if find(wbp, 'Btn_LogFold_%d' % f):
            log('skip %d (exists)' % f)
            continue
        headname = 'Row_LogCardHead_%d' % f
        if not find(wbp, headname):
            log('MISSING %s' % headname)
            continue
        st = find(wbp, 'Txt_LogCardStatus_%d' % f)
        if st:
            slot(st, fill=1.0, v='Center')
        else:
            log('MISSING Txt_LogCardStatus_%d' % f)
        sz = add(wbp, U.SizeBox, 'Row_Sz_Btn_LogFold_%d' % f, headname, var=False)
        sz.set_editor_property('width_override', 28.0)
        sz.set_editor_property('height_override', 28.0)
        slot(sz, pad=(8, 0, 0, 0), v='Center')
        b = add(wbp, U.Button, 'Btn_LogFold_%d' % f, 'Row_Sz_Btn_LogFold_%d' % f, var=False)
        nb = U.SlateBrush(); nb.set_editor_property('draw_as', U.SlateBrushDrawType.NO_DRAW_TYPE)
        b.set_editor_property('widget_style', button_style(nb, nb, nb))
        add(wbp, U.Overlay, 'Row_LogFoldOv_%d' % f, 'Btn_LogFold_%d' % f, var=False)
        for nm, icon in (('Lbl_LogFoldUp_%d' % f, 'T_IconUp'), ('Lbl_LogFoldDn_%d' % f, 'T_IconDown')):
            im = add(wbp, U.Image, nm, 'Row_LogFoldOv_%d' % f, var=False)
            im.set_editor_property('brush', image_brush(tex(icon), lin('B9B9D2'), 20.0))
            slot(im, h='Center', v='Center')
            if icon == 'T_IconDown':
                im.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
        log('added %d' % f)
    save(wbp)
    log('SAVED')
except Exception as e:
    import traceback
    log('ERROR ' + traceback.format_exc())
    raise
