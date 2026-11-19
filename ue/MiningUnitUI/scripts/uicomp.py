"""Generic UMG factories for the fleet UI (Direction A: fonts 11/10/13). NO hardcoded naming: every widget name is a parameter (callers pass the spec names), or derived from the given `name` + a fixed suffix (_Row/_Col/_In/_Sz/_Cell).
Idempotent (existing name -> returned). Lua-found widgets are var=True; SizeBox gets b_override flags. Colours = hex strings, no #. Chips default neutral grey (Lua tints).
Usage: `from uicomp import *` in an UnrealEditor-Cmd -ExecutePythonScript run (editor closed). Not run by the build."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *

FONTS = (11, 10, 13)  # body, small, header
OUTLINE, BODY, INSET, GOLD, ACCENT, BUTTON, DANGER, BARGREEN = '1C1C1C', '3A3A3A', '2B2B2B', 'C38800', 'F2B01E', '4A4A4A', '9C2222', '42DA41'
TEXT, DIM, NEUTRAL, DARKTXT, SELBG, TRACK = "E6E6E6", "BBBBBB", "A0A0A0", "111111", "D9D9D9", "1A1A1A"


def _find(w, name):
    return ELib.find_source_widget_by_name(w, name)


def _sz(w, name, parent, wd=None, ht=None, **slot_kw):
    """SizeBox with override flags set; wd/ht in px"""
    s = add(w, U.SizeBox, name, parent, var=False)
    for v, p in ((wd, 'width'), (ht, 'height')):
        if v is not None:
            s.set_editor_property(p + '_override', float(v))
            for flag in ('b_override_' + p, 'b_override_%s_override' % p):
                try:
                    s.set_editor_property(flag, True)
                    break
                except Exception:
                    pass
    if slot_kw:
        slot(s, **slot_kw)
    return s


def _img(w, name, parent, icon, size, hex_=TEXT):
    """Image `name`; icon = texture asset name under /Game/MiningUnitUI (e.g. 'T_IconTrash') or None (Lua sets it)"""
    im = add(w, U.Image, name, parent)
    if icon:
        im.set_editor_property('brush', image_brush('%s/%s.%s' % (ASSETS, icon, icon), lin(hex_), float(size)))
    return im


def _btn(w, name, parent, hex_=BUTTON, var=True, **slot_kw):
    """flat rounded Button in hex_"""
    b = add(w, U.Button, name, parent, var)
    b.set_editor_property('widget_style', button_style(round_brush(lin(hex_), 3.0), round_brush(lin(GOLD), 3.0), round_brush(lin(ACCENT), 3.0)))
    if slot_kw:
        slot(b, **slot_kw)
    return b


def _bdr(w, name, parent, hex_, pad, var=False, **slot_kw):
    b = add(w, U.Border, name, parent, var)
    b.set_editor_property('brush_color', lin(hex_))
    b.set_editor_property('padding', U.Margin(*pad))
    if slot_kw:
        slot(b, **slot_kw)
    return b


def _t(w, name, parent, size=FONTS[0], hex_=TEXT, var=True, label=None, **slot_kw):
    t = text(w, name, parent, size=size, color=lin(hex_), var=var, slot_kw=slot_kw or None)
    if label is not None:
        t.set_editor_property('text', U.Text(label))
    return t



def _hb(w, name, parent, var=False, **slot_kw):
    h = add(w, U.HorizontalBox, name, parent, var)
    if slot_kw:
        slot(h, **slot_kw)
    return h


def _vb(w, name, parent, var=False, **slot_kw):
    v = add(w, U.VerticalBox, name, parent, var)
    if slot_kw:
        slot(v, **slot_kw)
    return v


def _collapse(widget):
    widget.set_editor_property('visibility', U.SlateVisibility.COLLAPSED)


def _sel_btn(w, name, txt_name, parent, label='', **slot_kw):
    """select field: Button name (bg D9D9D9) > HBox { Txt txt_name (11, black, fill) ; 'v' (10, 555555) }"""
    b = _btn(w, name, parent, SELBG, **slot_kw)
    _hb(w, name + '_Row', name)
    _t(w, txt_name, name + '_Row', FONTS[0], '000000', label=label, pad=(6, 3, 4, 3), fill=1.0, h='Left', v='Center')
    _t(w, name + '_V', name + '_Row', FONTS[1], '555555', var=False, label='v', pad=(0, 0, 6, 0), v='Center')
    return b


def box(w, name, parent, title=None, title_name=None, body=BODY, pad=5):
    """Spec box: Border `name`(1C1C1C pad2) > VBox `name`_Col { Border `name`_HdrBdr(gold, Txt title_name 13 111111); Border `name`_Line(2px F2B01E); Border `name`_Body(body, pad) > VBox `name`_In }. Children go in parent=`name`_In. title=None -> no header/line."""
    if _find(w, name):
        return _find(w, name)
    r = _bdr(w, name, parent, OUTLINE, (2, 2, 2, 2), var=True)
    _vb(w, name + '_Col', name)
    if title is not None:
        _bdr(w, name + '_HdrBdr', name + '_Col', GOLD, (8, 2, 8, 2), h='Fill')
        _t(w, title_name or name + '_Title', name + '_HdrBdr', FONTS[2], DARKTXT, label=title)
        _bdr(w, name + '_Line', name + '_Col', ACCENT, (0, 1, 0, 1), h='Fill')
    _bdr(w, name + '_Body', name + '_Col', body, (pad,) * 4, h='Fill', v='Fill')
    _vb(w, name + '_In', name + '_Body', var=True)
    return r


def chip(w, name, parent, icon_name, text_name, text='', hex=None, icon=None, px=None):
    """Border `name`(hex or neutral) > HBox `name`_Row > Img icon_name (12x12) + Txt text_name (10 bold 111111); px = fixed width via SizeBox `name`_Sz."""
    if _find(w, name):
        return _find(w, name)
    cont = parent
    if px:
        _sz(w, name + '_Sz', parent, px, None, v='Center')
        cont = name + '_Sz'
    b = _bdr(w, name, cont, hex or NEUTRAL, (6, 1, 6, 1), var=True, v='Center')
    _hb(w, name + '_Row', name)
    im = _img(w, icon_name, name + '_Row', icon, 12, DARKTXT)
    slot(im, pad=(0, 0, 3, 0), v='Center')
    _t(w, text_name, name + '_Row', FONTS[1], DARKTXT, label=text, v='Center')
    return b


def bar(w, name, parent, hex, h=6, pct_name=None, track=TRACK):
    """ProgressBar `name` (var, fill hex, track 1A1A1A) in SizeBox `name`_Sz of height h (fill 1 in parent HBox); pct_name adds Txt (10 BBBBBB, w26) after it."""
    if _find(w, name):
        return _find(w, name)
    _sz(w, name + '_Sz', parent, None, h, fill=1.0, h='Fill', v='Center')
    b = add(w, U.ProgressBar, name, name + '_Sz', True)
    st = b.get_editor_property('widget_style')
    st.set_editor_property('background_image', round_brush(lin(track), 2.0))
    st.set_editor_property('fill_image', round_brush(lin(hex or BARGREEN), 2.0))
    b.set_editor_property('widget_style', st)
    b.set_editor_property('percent', 0.0)
    if pct_name:
        _sz(w, pct_name + '_Sz', parent, 26, None, pad=(4, 0, 0, 0), v='Center')
        _t(w, pct_name, pct_name + '_Sz', FONTS[1], DIM, h='Right', v='Center')
    return b


def icon_button(w, name, parent, icon, img_name=None, txt_name=None, label=None, size=12, hex=BUTTON, w_px=None, h_px=None, **slot_kw):
    """Button `name`(hex) > HBox `name`_Row > Img img_name (icon, size px) + Txt txt_name (label); each omitted when its name is None; w_px/h_px wrap it in SizeBox `name`_Sz; slot_kw = slot() args."""
    if _find(w, name):
        return _find(w, name)
    cont = parent
    if w_px or h_px:
        _sz(w, name + '_Sz', parent, w_px, h_px, **slot_kw)
        cont, slot_kw = name + '_Sz', {}
    b = _btn(w, name, cont, hex, **slot_kw)
    _hb(w, name + '_Row', name, h='Center', v='Center')
    if img_name:
        slot(_img(w, img_name, name + '_Row', icon, size), pad=(8, 3, 0, 3), v='Center')
    if txt_name:
        _t(w, txt_name, name + '_Row', FONTS[0], DARKTXT if hex in (GOLD, ACCENT) else TEXT, label=label or '', pad=(6 if img_name else 8, 3, 8, 3), v='Center')
    return b


def tab(main, name, txt_name, label, parent='TabBar', clone='Btn_TabLog', clone_txt='Txt_TabLog', hex=None, txt_hex=TEXT, pad=(3, 0, 0, 0)):
    """Button `name` + Txt txt_name (both var) in `parent`; clone copies bg/font/colour from the existing tab clone/clone_txt (main TabBar); clone=None -> plain hex/txt_hex (sub-tabs)."""
    if _find(main, name):
        return _find(main, name)
    b = add(main, U.Button, name, parent)
    t = text(main, txt_name, name, size=FONTS[0], var=True, slot_kw=dict(pad=(12, 3, 12, 3), h='Center', v='Center'))
    t.set_editor_property('text', U.Text(label))
    lg = _find(main, clone) if clone else None
    if lg:
        slot(lg, pad=(3, 0, 3, 0), fill=1.0, h='Fill', v='Fill')
        b.set_editor_property('background_color', lg.get_editor_property('background_color'))
        slot(b, pad=pad, fill=1.0, h='Fill', v='Fill')
        lt = _find(main, clone_txt)
        t.set_editor_property('font', lt.get_editor_property('font'))
        t.set_editor_property('color_and_opacity', lt.get_editor_property('color_and_opacity'))
        slot(t, pad=(16, 8, 16, 8), h='Center', v='Center')
    else:
        b.set_editor_property('background_color', lin(hex or BODY))
        t.set_editor_property('color_and_opacity', slate_color(lin(txt_hex)))
    return b


def page(main, name, parent='PageSwitcher'):
    """VerticalBox `name` (var) appended as the next child of `parent` (PageSwitcher / WidgetSwitcher)."""
    if _find(main, name):
        return _find(main, name)
    p = add(main, U.VerticalBox, name, parent, True)
    slot(p, h='Fill', v='Fill')
    return p


def group_header(w, name, parent, hdr_btn, arrow_img, name_txt, summary_txt=None, collapsed=True, arrow_icon=None):
    """HBox `name`(var, collapsed) > Button hdr_btn(4A4A4A fill) > HBox `hdr_btn`_Row { Img arrow_img 12x12 + Txt name_txt (12) + Txt summary_txt (10, fill, right) }."""
    if _find(w, name):
        return _find(w, name)
    r = _hb(w, name, parent, var=True, h='Fill')
    _btn(w, hdr_btn, name, BUTTON, fill=1.0, h='Fill', v='Fill')
    _hb(w, hdr_btn + '_Row', hdr_btn)
    slot(_img(w, arrow_img, hdr_btn + '_Row', arrow_icon, 12), pad=(8, 2, 6, 2), v='Center')
    _t(w, name_txt, hdr_btn + '_Row', FONTS[0] + 1, TEXT, pad=(0, 2, 6, 2), v='Center')
    if summary_txt:
        _t(w, summary_txt, hdr_btn + '_Row', FONTS[1], DIM, pad=(0, 2, 8, 2), fill=1.0, h='Right', v='Center')
    if collapsed:
        _collapse(r)
    return r


def field(w, key, parent, label, kind, row=None, lbl=None, ctl=None, txt=None, w_px=70):
    """HBox row (default Row_<key>, var) > Txt lbl (default Txt_<key>_Lbl, static) + control by kind: select -> Button ctl(D9D9D9)+Txt txt; number/text -> EditableTextBox ctl; check -> CheckBox ctl. Defaults: ctl Sel_/Edt_/Chk_<key>, txt Txt_Sel<key>."""
    row = row or 'Row_' + key
    if _find(w, row):
        return _find(w, row)
    lbl = lbl or 'Txt_%s_Lbl' % key
    r = _hb(w, row, parent, var=True, pad=(0, 2, 0, 2), h='Fill')
    _t(w, lbl, row, FONTS[0], TEXT, var=False, label=label, pad=(0, 0, 8, 0), fill=1.0, h='Left', v='Center')
    if kind == 'select':
        _sel_btn(w, ctl or 'Sel_' + key, txt or 'Txt_Sel' + key, row, v='Center')
    elif kind in ('number', 'text'):
        n = ctl or 'Edt_' + key
        _sz(w, n + '_Sz', row, w_px, None, v='Center')
        add(w, U.EditableTextBox, n, n + '_Sz', True)
    elif kind == 'check':
        slot(add(w, U.CheckBox, ctl or 'Chk_' + key, row, True), v='Center')
    return r


def class_strip(w, prefix, parent, keys, px=78, no_img=('All',)):
    """Per key K into HBox parent: SizeBox px > Btn_<prefix>K(2B2B2B) > Overlay { Bdr_<prefix>SelK(transparent, Lua tints) ; VBox { Img_<prefix>K 34x22 (no_img keys: Txt_<prefix>AllK 13) ; HBox Txt_<prefix>NameK + Txt_<prefix>CntK } }. All var."""
    for k in keys:
        bn = 'Btn_' + prefix + k
        if _find(w, bn):
            continue
        _sz(w, bn + '_Sz', parent, px, None, pad=(0, 0, 4, 0), v='Fill')
        _btn(w, bn, bn + '_Sz', INSET, h='Fill', v='Fill')
        add(w, U.Overlay, bn + '_Ov', bn, False)
        sb = _bdr(w, 'Bdr_%sSel%s' % (prefix, k), bn + '_Ov', '000000', (0, 0, 0, 0), var=True, h='Fill', v='Fill')
        sb.set_editor_property('brush_color', lin('000000', 0.0))
        _vb(w, bn + '_Col', bn + '_Ov', h='Center', v='Center', pad=(2, 4, 2, 4))
        if k in no_img:
            _t(w, 'Txt_%sAll%s' % (prefix, k), bn + '_Col', FONTS[2], TEXT, label=k.upper(), h='Center')
        else:
            _sz(w, 'Sz_%s%s' % (prefix, k), bn + '_Col', 34, 22, h='Center')
            _img(w, 'Img_%s%s' % (prefix, k), 'Sz_%s%s' % (prefix, k), None, 22)
        _hb(w, bn + '_Lines', bn + '_Col', h='Center')
        _t(w, 'Txt_%sName%s' % (prefix, k), bn + '_Lines', FONTS[1], TEXT, label=k, pad=(0, 0, 3, 0))
        _t(w, 'Txt_%sCnt%s' % (prefix, k), bn + '_Lines', FONTS[1], 'CCCCCC', label='0')
    return _find(w, parent)


def action_bar(w, bar_name, row_name, parent, items=()):
    """Border bar_name(262626) > HBox row_name; items=[(btn, txt, label, icon, img) | None]: Button btn(333333) + optional Img img(icon; icon=None skips it) + Txt txt; None = fill spacer."""
    if _find(w, bar_name):
        return _find(w, bar_name)
    b = _bdr(w, bar_name, parent, '262626', (8, 4, 8, 4), var=True, h='Fill')
    _hb(w, row_name, bar_name, var=True)
    for i, it in enumerate(items):
        if it is None:
            _sz(w, '%s_Sp%d' % (row_name, i), row_name, None, None, fill=1.0)
            continue
        btn, txt, label, icon, img = it
        icon_button(w, btn, row_name, icon, img if icon else None, txt, label, 12, '333333', pad=(0, 0, 6, 0), v='Center')
    return b


def selection_bar(w, bdr_name, row_name, parent, count_name, label_name=None, label='', sel_name=None, sel_txt=None, buttons=()):
    """Border bdr_name(33291A pad 6,4; var) > HBox row_name { Txt count_name (12, F2B01E) ; Txt label_name ; select sel_name+sel_txt ; buttons=[(btn, txt, label, gold)] }."""
    if _find(w, bdr_name):
        return _find(w, bdr_name)
    b = _bdr(w, bdr_name, parent, '33291A', (6, 4, 6, 4), var=True, h='Fill')
    _hb(w, row_name, bdr_name, var=True)
    _t(w, count_name, row_name, FONTS[0] + 1, ACCENT, label='0 selected', pad=(0, 0, 8, 0), v='Center')
    if label_name:
        _t(w, label_name, row_name, FONTS[0], TEXT, var=False, label=label, pad=(0, 0, 6, 0), v='Center')
    if sel_name:
        _sel_btn(w, sel_name, sel_txt, row_name, pad=(0, 0, 6, 0), v='Center')
    for btn, txt, lab, gold in buttons:
        _btn(w, btn, row_name, GOLD if gold else BUTTON, pad=(0, 0, 6, 0), v='Center')
        _t(w, txt, btn, FONTS[0], DARKTXT if gold else TEXT, label=lab, pad=(10, 3, 10, 3), h='Center', v='Center')
    return b


def tile(w, name, parent, txt_name, text, hex=None):
    """Border `name` (hex or inset) > Txt txt_name (11): small label tile."""
    if _find(w, name):
        return _find(w, name)
    b = _bdr(w, name, parent, hex or INSET, (6, 2, 6, 2), var=True)
    _t(w, txt_name, name, FONTS[0], TEXT, label=text, h='Center', v='Center')
    return b


def _cell(w, c, parent):
    name, kind, fill = c[0], c[1], c[2]
    extra = c[3] if len(c) > 3 else None
    px = c[4] if len(c) > 4 else None
    cell = name + '_Cell'
    if px:
        _sz(w, cell, parent, px, None, v='Center')
        _hb(w, cell + 'In', cell)
        cell = cell + 'In'
    else:
        _hb(w, cell, parent, fill=fill or 1.0, h='Fill', v='Center')
    if kind == 'text':
        t = _t(w, name, cell, FONTS[0], TEXT, v='Center', fill=1.0, h='Fill')
        try:
            t.set_editor_property('text_overflow_policy', U.TextOverflowPolicy.ELLIPSIS)
        except Exception:
            pass
    elif kind == 'chip':
        chip(w, name, cell, extra[0], extra[1])
    elif kind == 'bar':
        bar(w, name, cell, BARGREEN, 6, extra)
    elif kind == 'img':
        sz = extra or (20, 20)
        _sz(w, name + '_Sz', cell, sz[0], sz[1], v='Center')
        _img(w, name, name + '_Sz', None, sz[1])
    elif kind == 'btn':
        _btn(w, name, cell, BUTTON, v='Center')
        _t(w, extra, name, FONTS[1], TEXT, pad=(6, 2, 6, 2), h='Center', v='Center')
    elif kind == 'check':
        slot(add(w, U.CheckBox, name, cell, True), v='Center')
    elif kind == 'icontext':
        slot(_img(w, extra[0], cell, None, 12), pad=(0, 0, 4, 0), v='Center')
        _t(w, extra[1], cell, FONTS[0], TEXT, v='Center', fill=1.0, h='Fill')


def list_row_wbp(wbp_name, root, data_row, columns, root_kind='vbox', group=None, frame=None, hex=INSET):
    """Build row WBP wbp_name (returns the WBP). root (var) = 'vbox' VerticalBox or 'border'(hex). group=dict(name,hdr,arrow,name_txt,summary) adds a collapsed group_header first (vbox only). frame=(frame_border, sel_border, row_button) wraps the data row as Border>Border>Button>HBox. columns=[(name,kind,fill[,extra[,px]])], kind text|chip|bar|img|btn|check|icontext, extra = chip (icon,text) / bar pct name / img (w,h) / btn txt name / icontext (img,txt), px = fixed width; cell wrapper `<name>_Cell`."""
    w = new_wbp(wbp_name)
    if root_kind == 'border':
        _bdr(w, root, '', hex, (0, 0, 0, 0), var=True)
        par = root
    else:
        _vb(w, root, '', var=True)
        par = root
        if group:
            group_header(w, group['name'], root, group['hdr'], group['arrow'], group['name_txt'], group.get('summary'))
        if frame:
            _bdr(w, frame[0], root, hex, (0, 0, 0, 0), var=True)
            _bdr(w, frame[1], frame[0], '000000', (0, 0, 0, 0), var=True).set_editor_property('brush_color', lin('000000', 0.0))
            b = _btn(w, frame[2], frame[1], '000000')
            nb = U.SlateBrush(); nb.set_editor_property('draw_as', U.SlateBrushDrawType.NO_DRAW_TYPE)
            b.set_editor_property('widget_style', button_style(nb, nb, nb))
            par = frame[2]
    _hb(w, data_row, par, var=True, h='Fill', v='Center', pad=(6, 2, 6, 2))
    for c in columns:
        _cell(w, c, data_row)
    save(w)
    return w


def form_row_wbp(wbp_name='WBP_FormRow'):
    """Build form row WBP: Txt_Lbl + Btn_Sel/Txt_Sel + Edt_Val + Chk_Val (all var; Lua shows the one control it needs)."""
    w = new_wbp(wbp_name)
    _hb(w, 'Row_Root', '', var=True)
    _t(w, 'Txt_Lbl', 'Row_Root', FONTS[0], DIM, pad=(0, 0, 8, 0), fill=1.0, h='Left', v='Center')
    _sel_btn(w, 'Btn_Sel', 'Txt_Sel', 'Row_Root', fill=1.5, h='Fill', v='Center')
    e = add(w, U.EditableTextBox, 'Edt_Val', 'Row_Root', True)
    slot(e, fill=1.5, h='Fill', v='Center')
    add(w, U.CheckBox, 'Chk_Val', 'Row_Root', True)
    save(w)
    return w
