"""Idempotent: Dyn_<name> (DynamicEntryBox of WBP_DockRow) in each dock card: vertical, full width, above the Add button."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
ORES = ["Cobalt", "Graphite", "Iridium", "Nickel", "Silica", "Titanium", "WaterIce"]
# (dyn name, inner VerticalBox of the card, Add button size box)
LISTS = [("RDock", "Row_CardInnerRDock", "Row_Sz_Btn_RDockAdd")] + [
    ("UDock_" + k, "Row_CardInnerUD" + k, "Row_Sz_Btn_UDockAdd_" + k) for k in ORES]
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
cls = U.load_class(None, ASSETS + '/WBP_DockRow.WBP_DockRow_C')
for name, inner_n, add_n in LISTS:
    dn = 'Dyn_' + name
    d = ELib.find_source_widget_by_name(wbp, dn)
    if not d:
        d = add(wbp, U.DynamicEntryBox, dn, inner_n)
    d.set_editor_property('entry_widget_class', cls)
    d.set_editor_property('entry_box_type', U.DynamicBoxType.VERTICAL)
    d.set_editor_property('entry_spacing', U.Vector2D(0.0, 6.0))
    d.set_editor_property('entry_horizontal_alignment', U.HorizontalAlignment.H_ALIGN_FILL)
    slot(d, h='Fill')
    inner = ELib.find_source_widget_by_name(wbp, inner_n)
    addsz = ELib.find_source_widget_by_name(wbp, add_n)
    if inner.get_child_index(addsz) < inner.get_child_index(d):   # move the Add button after the list
        inner.remove_child(addsz)
        inner.add_child_to_vertical_box(addsz)
        slot(addsz, pad=(0, 4, 0, 0), h='Fill')
    print('ORDER', name, [inner.get_child_at(i).get_name() for i in range(inner.get_children_count())][-3:])
save(wbp)
print('SAVED')
