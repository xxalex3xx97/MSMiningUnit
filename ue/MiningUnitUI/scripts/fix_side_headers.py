"""Side panel headers (Fleet/Control/Actions/Status): the main window's header tab SizeBox has min_desired_height 40 (HDR_H), the side ones
had 0, so the side header row (and the gold line texture, drawn as tall as the row) was ~half as thick. Give the side tab boxes the main value.
Idempotent. Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

main = ELib.find_source_widget_by_name(U.load_asset(ASSETS + '/WBP_MiningUnit'), 'Row_Sz_Row_TabCenter')
w = U.load_asset(ASSETS + '/WBP_MiningUnit')
H = ELib.find_source_widget_by_name(w, 'Row_Sz_Row_TabCenter').get_editor_property('min_desired_height')
for k in ('Fleet', 'Control', 'Actions', 'Status'):
    s = ELib.find_source_widget_by_name(w, 'Row_Sz_Row_Tab' + k)
    s.set_editor_property('height_override', 0.0)
    s.set_editor_property('min_desired_height', H)
    x = ELib.find_source_widget_by_name(w, 'Row_Line' + k)   # undo the earlier margin experiment: same brush as the main line
    b = x.get_editor_property('background'); m = b.get_editor_property('margin')
    m.set_editor_property('left', 0.49); m.set_editor_property('right', 0.49)
    b.set_editor_property('margin', m); x.set_editor_property('background', b)
    print('FH set', k, H)
save(w)
