"""Hide the redundant top 'Refuel dock' select row on the Ship page (the Refuel docks list replaces it).
Deleting it leaves stale variable GUIDs that break the cook, so a collapsed stub with the same names is kept."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
if not ELib.find_source_widget_by_name(wbp, 'Row_Sel_RefuelDock'):
    row = add(wbp, U.HorizontalBox, 'Row_Sel_RefuelDock', 'Row_Scroll_Page_Ship')
    add(wbp, U.TextBlock, 'Txt_ShipRefuelDockLabel', 'Row_Sel_RefuelDock')
    add(wbp, U.SizeBox, 'Row_Sz_Sel_RefuelDock', 'Row_Sel_RefuelDock')
    add(wbp, U.Button, 'Sel_RefuelDock', 'Row_Sz_Sel_RefuelDock')
    row.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
row = ELib.find_source_widget_by_name(wbp, 'Row_Sel_RefuelDock')
row.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
print('HIDDEN', row.get_parent().get_name(), row.get_editor_property('visibility'))
save(wbp)
print('SAVED')
