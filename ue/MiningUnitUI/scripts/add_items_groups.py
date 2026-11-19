"""Item groups + presets: group header strip Row_IGroup in WBP_ItemRow, and the preset bar Row_PresetBar (name edit + save/apply/delete
buttons) at the end of Row_Scroll_Page_Items in WBP_MiningUnit. Idempotent. Run inside the UE editor AFTER build_lists.py
(it recreates WBP_ItemRow). Not run by the build."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out_add_items_groups.txt')


def log(m):
    with open(OUT, 'a') as fh:
        fh.write(str(m) + '\n')


try:
    c = U.load_asset(ASSETS + '/WBP_ItemRow')
    item_group_header(c)
    save(c)
    log('row hdr ok')

    wbp = U.load_asset(ASSETS + '/WBP_MiningUnit')
    find = ELib.find_source_widget_by_name
    ref_btn = find(wbp, 'Btn_ItemAdd')
    ref_txt = find(wbp, 'Txt_ItemAdd')
    if not ref_btn:
        log('MISSING Btn_ItemAdd')
    if not ref_txt:
        log('MISSING Txt_ItemAdd')
    if not find(wbp, 'Row_PresetBar'):
        bar = add(wbp, U.HorizontalBox, 'Row_PresetBar', 'Row_Scroll_Page_Items', var=False)
        slot(bar, pad=(0, 8, 0, 0), h='Fill')
        eb = add(wbp, U.EditableTextBox, 'Edt_PresetName', 'Row_PresetBar')
        slot(eb, fill=1.0, h='Fill', v='Center')
        blk = U.SlateColor(specified_color=U.LinearColor(0, 0, 0, 1))
        st = eb.get_editor_property('widget_style')
        ts = st.get_editor_property('text_style')
        f = ts.get_editor_property('font'); f.set_editor_property('size', 11); ts.set_editor_property('font', f)
        ts.set_editor_property('color_and_opacity', blk)
        st.set_editor_property('text_style', ts)
        for pn in ('foreground_color', 'focused_foreground_color', 'read_only_foreground_color'):
            st.set_editor_property(pn, blk)
        eb.set_editor_property('widget_style', st)
        eb.set_editor_property('hint_text', U.Text('Preset name (empty = Preset N)'))
        for bn, ln, lt in (('Btn_PresetSave', 'Lbl_PresetSave', 'Save preset'),
                           ('Btn_PresetApply', 'Lbl_PresetApply', 'Apply preset'),
                           ('Btn_PresetDel', 'Lbl_PresetDel', 'Delete preset')):
            b = add(wbp, U.Button, bn, 'Row_PresetBar')
            if ref_btn:
                b.set_editor_property('background_color', ref_btn.get_editor_property('background_color'))
                b.set_editor_property('widget_style', ref_btn.get_editor_property('widget_style'))
            slot(b, pad=(6, 0, 0, 0), v='Fill')
            t = text(wbp, ln, bn, size=15, var=False, slot_kw=dict(pad=(12, 6, 12, 6), h='Center', v='Center'))
            if ref_txt:
                t.set_editor_property('font', ref_txt.get_editor_property('font'))
                t.set_editor_property('color_and_opacity', ref_txt.get_editor_property('color_and_opacity'))
            f = t.get_editor_property('font'); f.set_editor_property('size', 15); t.set_editor_property('font', f)
            t.set_editor_property('text', U.Text(lt))
        log('preset bar built')
    sb = find(wbp, 'Row_Scroll_Page_Items')
    bar = find(wbp, 'Row_PresetBar')
    if isinstance(sb, U.ScrollBox):
        sb.remove_child_at(sb.get_child_index(bar))
        sb.add_child(bar)
    else:
        move_last('Row_Scroll_Page_Items', wbp, 'Row_PresetBar')
    save(wbp)
    log('SAVED')
except Exception:
    import traceback
    log('ERROR ' + traceback.format_exc())
    raise
