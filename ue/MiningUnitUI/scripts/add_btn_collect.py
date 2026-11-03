"""Add the 'Collect ore' button to the Actions box of WBP_MiningUnit (after Unload). Idempotent: skips when Btn_Collect exists.
Modelled on rm_refuel_row.py. Run inside the UE editor (not run by the build)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
if not ELib.find_source_widget_by_name(wbp, 'Btn_Collect'):
    sz = add(wbp, U.SizeBox, 'Row_Sz_Btn_Collect', 'Row_ActionsBody', var=False)
    sz.set_editor_property('min_desired_height', 56.0)
    slot(sz, pad=(0, 6, 0, 0), h='Fill')
    b = add(wbp, U.Button, 'Btn_Collect', 'Row_Sz_Btn_Collect')
    ub = ELib.find_source_widget_by_name(wbp, 'Btn_Unload')
    b.set_editor_property('background_color', ub.get_editor_property('background_color'))
    b.set_editor_property('widget_style', ub.get_editor_property('widget_style'))
    t = text(wbp, 'Txt_Collect', 'Btn_Collect', size=18, var=True)
    ut = ELib.find_source_widget_by_name(wbp, 'Txt_Unload')
    t.set_editor_property('font', ut.get_editor_property('font'))
    t.set_editor_property('color_and_opacity', ut.get_editor_property('color_and_opacity'))
    t.set_editor_property('text', U.Text('Collect ore'))
    slot(t, pad=(14, 8, 14, 8), h='Center', v='Center')
save(wbp)
print('SAVED')
