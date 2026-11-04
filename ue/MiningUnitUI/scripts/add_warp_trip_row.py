"""Add the 'Warp trip distance (km)' row (Txt_OpWarpTripLabel + Edt_WarpTripKm) to the Warp card of WBP_MiningUnit,
right after the Warp range row (Row_CardInnerWarp, before Row_BeltButtons). Idempotent: skips when Edt_WarpTripKm exists.
Modelled on add_btn_collect.py. Run inside the UE editor (not run by the build)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
if not ELib.find_source_widget_by_name(wbp, 'Edt_WarpTripKm'):
    row = add(wbp, U.HorizontalBox, 'Row_Edt_WarpTripKm', 'Row_CardInnerWarp', var=False)
    t = add(wbp, U.TextBlock, 'Txt_OpWarpTripLabel', 'Row_Edt_WarpTripKm')
    t.set_editor_property('text', U.Text('Warp trip distance (km)'))
    ut = ELib.find_source_widget_by_name(wbp, 'Txt_OpWarpRangeLabel')
    t.set_editor_property('font', ut.get_editor_property('font'))
    t.set_editor_property('color_and_opacity', ut.get_editor_property('color_and_opacity'))
    slot(t, pad=(0, 0, 6, 0), fill=1.0, v='Center')
    sz = add(wbp, U.SizeBox, 'Row_Sz_Edt_WarpTripKm', 'Row_Edt_WarpTripKm', var=False)
    sz.set_editor_property('width_override', 80.0)
    slot(sz, v='Center')
    e = add(wbp, U.EditableTextBox, 'Edt_WarpTripKm', 'Row_Sz_Edt_WarpTripKm')
    e.set_editor_property('widget_style', ELib.find_source_widget_by_name(wbp, 'Edt_WarpRange').get_editor_property('widget_style'))
    move_last('Row_CardInnerWarp', wbp, 'Row_BeltButtons')
save(wbp)
print('SAVED')
