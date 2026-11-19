"""Side panel headers (Fleet/Control/Actions/Status): the gold line is a stretched texture, so it is as thick as the header row.
Main window row is taller (close cap), side rows follow the small tab pill -> thin line. Pin the side tab SizeBoxes to the main row height. Idempotent.
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

H = 22.0
w = U.load_asset(ASSETS + '/WBP_MiningUnit')
for k in ('Fleet', 'Control', 'Actions', 'Status'):
    s = ELib.find_source_widget_by_name(w, 'Row_Sz_Row_Tab' + k)
    if not s:
        print('FH missing', k); continue
    s.set_editor_property('height_override', H)
    print('FH set', k, s.get_editor_property('height_override'))
save(w)
