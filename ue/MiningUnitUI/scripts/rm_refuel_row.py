"""Remove the redundant top 'Refuel dock' select row from the Ship page (the Refuel docks list replaces it)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
lab = ELib.find_source_widget_by_name(wbp, 'Txt_ShipRefuelDockLabel')
if lab:
    row = lab.get_parent()
    print('ROW', row.get_name(), row.get_class().get_name(), 'in', row.get_parent().get_name())
    print('REMOVED', remove(wbp, row.get_name()), 'parent now', row.get_parent())
save(wbp)
print('SAVED')
