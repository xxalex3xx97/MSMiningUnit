"""Add the 'Station safe radius (m)' row (Txt_OpStationZoneLabel + Edt_StationZoneRadius) to the Mining card of WBP_MiningUnit,
after the Max radius row (end of Row_CardInnerMining). Idempotent: skips when Edt_StationZoneRadius exists.
Modelled on add_warp_trip_row.py. Run inside the UE editor (not run by the build)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
if not ELib.find_source_widget_by_name(wbp, 'Edt_StationZoneRadius'):
    row = add(wbp, U.HorizontalBox, 'Row_Edt_StationZoneRadius', 'Row_CardInnerMining', var=False)
    t = add(wbp, U.TextBlock, 'Txt_OpStationZoneLabel', 'Row_Edt_StationZoneRadius')
    t.set_editor_property('text', U.Text('Station safe radius (m)'))
    ut = ELib.find_source_widget_by_name(wbp, 'Txt_OpMaxRadiusLabel')
    t.set_editor_property('font', ut.get_editor_property('font'))
    t.set_editor_property('color_and_opacity', ut.get_editor_property('color_and_opacity'))
    slot(t, pad=(0, 0, 6, 0), fill=1.0, v='Center')
    sz = add(wbp, U.SizeBox, 'Row_Sz_Edt_StationZoneRadius', 'Row_Edt_StationZoneRadius', var=False)
    sz.set_editor_property('width_override', 80.0)
    slot(sz, v='Center')
    e = add(wbp, U.EditableTextBox, 'Edt_StationZoneRadius', 'Row_Sz_Edt_StationZoneRadius')
    e.set_editor_property('widget_style', ELib.find_source_widget_by_name(wbp, 'Edt_MaxRadius').get_editor_property('widget_style'))
save(wbp)
print('SAVED')
