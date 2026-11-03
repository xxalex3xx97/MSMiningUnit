"""Hide the 'Unload ores' card on the Operation page (ore toggles removed: an ore unloads when it has an unload dock).
Deleting widgets breaks the cook, so the card is only collapsed."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
card = ELib.find_source_widget_by_name(wbp, 'Row_CardOutlineOres')
if not card:
    print('ERROR card Row_CardOutlineOres not found'); sys.exit(1)
card.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)
print('HIDDEN', card.get_parent().get_name(), card.get_editor_property('visibility'))
save(wbp)
print('SAVED')
