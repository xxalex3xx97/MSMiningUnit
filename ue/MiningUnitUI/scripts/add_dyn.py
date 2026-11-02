import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
if not ELib.find_source_widget_by_name(wbp, 'Dyn_RDock'):
    d = add(wbp, U.DynamicEntryBox, 'Dyn_RDock', 'Row_CardInnerRDock')
    d.set_editor_property('entry_widget_class', U.load_class(None, ASSETS + '/WBP_DockRow.WBP_DockRow_C'))
    d.set_editor_property('entry_spacing', U.Vector2D(0.0, 6.0))
    print('ADDED', d, d.get_editor_property('entry_widget_class'))
save(wbp)
print('SAVED')
