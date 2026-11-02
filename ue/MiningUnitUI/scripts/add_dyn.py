"""Idempotent: Dyn_RDock (DynamicEntryBox of WBP_DockRow) in the refuel card, vertical, full width, above the Add button."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
d = ELib.find_source_widget_by_name(wbp, 'Dyn_RDock')
if not d:
    d = add(wbp, U.DynamicEntryBox, 'Dyn_RDock', 'Row_CardInnerRDock')
d.set_editor_property('entry_widget_class', U.load_class(None, ASSETS + '/WBP_DockRow.WBP_DockRow_C'))
d.set_editor_property('entry_box_type', U.DynamicBoxType.VERTICAL)
d.set_editor_property('entry_spacing', U.Vector2D(0.0, 6.0))
d.set_editor_property('entry_horizontal_alignment', U.HorizontalAlignment.H_ALIGN_FILL)
slot(d, h='Fill')
inner = ELib.find_source_widget_by_name(wbp, 'Row_CardInnerRDock')
addsz = ELib.find_source_widget_by_name(wbp, 'Row_Sz_Btn_RDockAdd')
if inner.get_child_index(addsz) < inner.get_child_index(d):   # move the Add button after the list
    inner.remove_child(addsz)
    inner.add_child_to_vertical_box(addsz)
    slot(addsz, pad=(0, 4, 0, 0), h='Fill')
print('ORDER', [inner.get_child_at(i).get_name() for i in range(inner.get_children_count())])
save(wbp)
print('SAVED')
