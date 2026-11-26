"""Compact every text in the mining window to the Fleet window sizes (body 11 / small 10 / header 13). Walks the widget trees of
WBP_MiningUnit + row WBPs (python-built and pasted alike), rescales font sizes, tightens vertical paddings of text slots and
text SizeBox heights. Idempotent: an asset tag 'FontCompact' marks a processed WBP (set DRY=1 to only list).
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor + game closed)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

DRY = os.environ.get('DRY') == '1'
WBPS = ['WBP_MiningUnit', 'WBP_DockRow', 'WBP_ItemRow', 'WBP_LogLine', 'WBP_NetDockCard', 'WBP_NetDockRow', 'WBP_NetQueueRow']
ANCHORS = {'WBP_MiningUnit': 'Txt_TabDocks'}
MIN_H = 22.0


def in_button(w):
    p = w.get_parent()
    while p:
        if isinstance(p, U.Button):
            return True
        p = p.get_parent()
    return False


def target(s, w):
    """old size -> Fleet size (buttons + tabs 11, titles 13, row names 12, body 11, labels 10)"""
    s = int(round(s))
    if in_button(w) or w.get_name().startswith('Txt_Tab'):
        return 11
    if s >= 17:
        return 13   # window / box titles
    if s >= 15:
        return 12   # card / row names
    if s >= 13:
        return 11   # body
    return 10       # labels


def root_of(wbp):
    w = None
    for n in ('Txt_TabDocks', 'Row_Root', 'Col_Root', 'Bdr_DCard', 'Pnl_PageDocks', 'Txt_LogName', 'Txt_Name', 'Txt_QName', 'Txt'):
        w = ELib.find_source_widget_by_name(wbp, n)
        if w:
            break
    while w and w.get_parent():
        w = w.get_parent()
    return w


def kids(w):
    if isinstance(w, U.PanelWidget):
        return [w.get_child_at(i) for i in range(w.get_children_count())]
    if isinstance(w, U.ContentWidget) and w.get_content():
        return [w.get_content()]
    return []


def has_text(w):
    return isinstance(w, U.TextBlock) or any(has_text(c) for c in kids(w))


def has_edit(w):
    return isinstance(w, (U.EditableText, U.EditableTextBox, U.ComboBoxString, U.SpinBox)) or any(has_edit(c) for c in kids(w))


def walk(w, out):
    out.append(w)
    for c in kids(w):
        walk(c, out)


def fonts_of(w):
    """(property, FontStruct) pairs for text-bearing widgets"""
    if isinstance(w, U.TextBlock):
        return [('font', w.get_editor_property('font'))]
    if isinstance(w, (U.EditableText, U.EditableTextBox, U.SpinBox, U.ComboBoxString)):
        try:
            if isinstance(w, U.EditableTextBox):
                return [('font', w.get_editor_property('widget_style').get_editor_property('text_style').get_editor_property('font'))] if False else []
        except Exception:
            pass
    return []


for name in WBPS:
    path = '%s/%s' % (ASSETS, name)
    if not U.EditorAssetLibrary.does_asset_exist(path):
        print('CF skip (missing)', name); continue
    wbp = U.load_asset(path)
    if U.EditorAssetLibrary.get_metadata_tag(wbp, 'FontCompact') == '1' and not DRY:
        print('CF already done', name); continue
    r = root_of(wbp)
    if not r:
        print('CF NO ROOT', name); continue
    ws = []; walk(r, ws)
    print('CF %s: %d widgets' % (name, len(ws)))
    for w in ws:
        cls = w.get_class().get_name(); wn = w.get_name()
        if isinstance(w, U.TextBlock):
            f = w.get_editor_property('font'); old = f.get_editor_property('size'); new = target(old, w)
            s = w.slot; pad = None
            try:
                pad = s.get_editor_property('padding')
            except Exception:
                pass
            print('CF  TEXT %s %s -> %s pad=%s' % (wn, old, new, pad and (pad.left, pad.top, pad.right, pad.bottom)))
            if not DRY:
                f.set_editor_property('size', new); w.set_editor_property('font', f)
                if pad and old > new:
                    k = new / float(old)
                    s.set_editor_property('padding', U.Margin(round(pad.left * max(k, .8)), round(pad.top * k), round(pad.right * max(k, .8)), round(pad.bottom * k)))
        elif isinstance(w, U.SizeBox):
            hv = w.get_editor_property('height_override'); ov = w.get_editor_property('override_height') if hasattr(w, 'override_height') else None
            print('CF  SIZEBOX %s h=%s text=%s edit=%s' % (wn, hv, has_text(w), has_edit(w)))
            if not DRY and hv and hv > 0 and has_text(w):
                nh = max(hv * 0.8, MIN_H if has_edit(w) else 0)
                w.set_editor_property('height_override', float(round(min(hv, nh))))
        elif isinstance(w, (U.EditableText, U.EditableTextBox, U.ComboBoxString, U.SpinBox)):
            print('CF  EDIT %s %s' % (wn, cls))
    if not DRY:
        U.EditorAssetLibrary.set_metadata_tag(wbp, 'FontCompact', '1')
        save(wbp)
print('CF DONE')
