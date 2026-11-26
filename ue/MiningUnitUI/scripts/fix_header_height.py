"""Side-window header rows were thin: measured in game, the header row wanted only 21 units (tab SizeBox desired 152x21) although
MinDesiredHeight=40 and its override flag are set in the asset (checked in the editor), so the min height is not applied at runtime.
Pin the header tab SizeBoxes (Row_Sz_Row_Tab*) with HeightOverride=40 instead (SetHeightOverride sets the flag too). Idempotent.
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor + game closed)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

HDR_H = 40.0


def kids(w):
    if isinstance(w, U.PanelWidget):
        return [w.get_child_at(i) for i in range(w.get_children_count())]
    if isinstance(w, U.ContentWidget):
        c = w.get_content()
        return [c] if c else []
    return []


def walk(w, out):
    if isinstance(w, U.SizeBox) and w.get_name().startswith('Row_Sz_Row_Tab'):
        out.append(w)
    for k in kids(w):
        walk(k, out)


def flag(w):
    for n in ('b_override_min_desired_height', 'override_min_desired_height'):
        try:
            return n, w.get_editor_property(n)
        except Exception:
            pass
    return None, None


wbp = U.load_asset('%s/WBP_MiningUnit' % ASSETS)
root = ELib.find_source_widget_by_name(wbp, 'Txt_TabDocks')
while root and root.get_parent():
    root = root.get_parent()
boxes = []
walk(root, boxes)
print('FHH boxes=%d' % len(boxes))
for sb in boxes:
    print('FHH %s before min_h=%s flag=%s attrs=%s' % (sb.get_name(), sb.get_editor_property('min_desired_height'), flag(sb),
          [a for a in dir(sb) if 'override' in a or 'min_desired' in a][:12]))
    sb.set_height_override(HDR_H)   # UFunction: sets HeightOverride AND its override flag
    print('FHH %s after  h_ovr=%s' % (sb.get_name(), sb.get_editor_property('height_override')))
save(wbp)
print('FHH DONE')
