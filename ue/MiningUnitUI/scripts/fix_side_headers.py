"""Side panel headers (Fleet/Control/Actions/Status): the gold line is a Box-drawn texture (Header_Bar) with left/right margins 0.49.
On a short line (side panels, ~150px) Slate shrinks ALL margins uniformly, so the bar gets hair-thin; the wide main line is fine.
Give the side lines small left/right margins (vertical margins unchanged) so the bar keeps the main window's thickness. Idempotent.
Also resets the SizeBox height pins an earlier attempt of this script set on the tab boxes.
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

LR = 0.08
w = U.load_asset(ASSETS + '/WBP_MiningUnit')
for k in ('Fleet', 'Control', 'Actions', 'Status'):
    s = ELib.find_source_widget_by_name(w, 'Row_Sz_Row_Tab' + k)
    if s:
        s.set_editor_property('height_override', 0.0); s.set_editor_property('min_desired_height', 0.0)
    x = ELib.find_source_widget_by_name(w, 'Row_Line' + k)
    if not x:
        print('FH missing', k); continue
    b = x.get_editor_property('background')
    m = b.get_editor_property('margin')
    m.set_editor_property('left', LR); m.set_editor_property('right', LR)
    b.set_editor_property('margin', m)
    x.set_editor_property('background', b)
    print('FH set', k, b.get_editor_property('margin'))
save(w)
