"""Standalone Fleet window /Game/FleetUI/WBP_FleetWindow (replaces add_fleet_tab.py), composed ONLY from uicomp factories; widget names = fleet-ui-widget-spec.md + plan 2026-11-20-fleet-window-plan.md.
Builds: row WBPs WBP_FleetShipRow / WBP_FleetEscortRow / WBP_FleetTargetRow (Border > HBox, no Button wrapper), then WBP_FleetWindow:
Root CanvasPanel > Panel (transparent Border, anchors 0.10/0.10-0.90/0.90) > Body VBox > Row_Cols (fill, 16 px gaps) = SizeBox 230 Pnl_FleetLeft | Col_Centre (TitleBar: Txt_FlTitle, Btn_FlClose ; outline > body > inset > Col_CentreIn { Row_TabBar ; PageSwitcher: Pnl_PageFleet / Pnl_PageRole / Pnl_PageEscort }) | SizeBox 260 Pnl_FleetStatus (mining frame look),
plus collapsed modal Pnl_Modal. Every ancestor of Row_FlListScroll has fill 1. Finally imports add_role_pages (role cards + escort page).
Idempotent: row WBPs built only when missing (new_wbp deletes an existing asset: delete them to rebuild), every other part skipped when its name exists. Never deletes widgets.
Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed). Not run by the build."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uew; uew.OUT = '/Game/FleetUI'
from uicomp import *
from uicomp import _find, _vb, _hb, _t, _sz, _img, _ship_img, _bdr, _btn, _collapse, _clear, tex_border, hazard_border, HDR_H, M_TAB, M_CAP, M_LINE, TAB_OFF

CLASSES = ['All', 'Miner', 'Hauler', 'Fighter', 'Frigate', 'Capital']
# ship row columns (name, kind, fill[, extra[, px]]): used by WBP_FleetShipRow AND the column header (col_head)
FLEET_COLS = [
    ('Chk_FrSel', 'check', 0, None, 18), ('Img_FrHolo', 'img', 0, (44, 28), 44), ('Txt_FrName', 'text', 1.1, FONTS[0] + 1),
    ('Bdr_FrChip', 'chip', 0, (None, 'Txt_FrRole'), 66), ('Row_FrDoing', 'icontext', 1.7, ('Txt_FrState', 'Txt_FrDoing')),
    ('PB_FrHull', 'bar', 0, 'Txt_FrHullPct', 78), ('PB_FrCargo', 'bar', 0, 'Txt_FrCargoPct', 78),
    ('Btn_FrRemove', 'btn', 0, 'Txt_FrRemove', 56), ('Btn_FrGoto', 'btn', 0, 'Txt_FrGoto', 56)]
FLEET_LABELS = {'Txt_FrName': 'Ship', 'Bdr_FrChip': 'Role', 'Row_FrDoing': 'Doing now', 'PB_FrHull': 'Hull', 'PB_FrCargo': 'Cargo'}


def exists(n):
    return U.EditorAssetLibrary.does_asset_exist(uew.OUT + '/' + n)


def size(w, name, px):
    t = _find(w, name)
    f = t.get_editor_property('font')
    f.set_editor_property('size', px)
    t.set_editor_property('font', f)


def txt(main, name, parent, label, sz=FONTS[0], hex_=TEXT, var=False, **kw):
    return _t(main, name, parent, sz, hex_, var=var, label=label, **kw)


def anchor(w, x0, y0, x1, y1):
    """CanvasPanel child: anchors (x0,y0)-(x1,y1), zero offsets"""
    w.slot.set_editor_property('layout_data', U.AnchorData(offsets=U.Margin(0, 0, 0, 0), anchors=U.Anchors(minimum=U.Vector2D(x0, y0), maximum=U.Vector2D(x1, y1)),
                                                           alignment=U.Vector2D(0, 0)))


# ---- 1. row WBPs FIRST
if not exists('WBP_FleetShipRow'):
    w = list_row_wbp('WBP_FleetShipRow', 'Fr_Root', 'Row_FrRow', FLEET_COLS,
                     group=dict(name='Fg_Root', hdr='Btn_FgHdr', arrow='Txt_FgArrow', name_txt='Txt_FgName', summary='Txt_FgSummary'), frame='Fr_RowFrame')
    _collapse(_find(w, 'Btn_FrRemove')); _collapse(_find(w, 'Btn_FrGoto'))
    _t(w, 'Txt_FrNoCargo', 'PB_FrCargo_CellIn', FONTS[1], DIM, label='no cargo', v='Center'); _collapse(_find(w, 'Txt_FrNoCargo'))
    _t(w, 'Txt_FrLostHint', 'Fr_Root', FONTS[1], DIM); _collapse(_find(w, 'Txt_FrLostHint'))
    save(w)
if not exists('WBP_FleetEscortRow'):
    list_row_wbp('WBP_FleetEscortRow', 'Fe_Root', 'Row_FeRow', [
        ('Img_FeHolo', 'img', 0, (44, 28), 44), ('Txt_FeName', 'text', 1.1, FONTS[0] + 1), ('Bdr_FeChip', 'chip', 0, (None, 'Txt_FeRole'), 66),
        ('Row_FeDoing', 'icontext', 1.7, ('Txt_FeState', 'Txt_FeDoing')), ('PB_FeHull', 'bar', 0, 'Txt_FeHullPct', 78),
        ('Txt_FeDist', 'text', 0, None, 70), ('Btn_FePin', 'btn', 0, 'Txt_FePin', 28), ('Btn_FeAction', 'btn', 0, 'Txt_FeAction', 28)],
        root_kind='border')
if not exists('WBP_FleetTargetRow'):
    list_row_wbp('WBP_FleetTargetRow', 'Ft_Root', 'Row_FtRow', [
        ('Img_FtHolo', 'img', 0, (44, 28), 44), ('Txt_FtName', 'text', 1.0), ('Btn_FtPick', 'btn', 0, 'Txt_FtPick', 28)], root_kind='border')

main = U.load_asset(uew.OUT + '/WBP_FleetWindow') if exists('WBP_FleetWindow') else new_wbp('WBP_FleetWindow')

# ---- 2. shell (mining look): transparent Panel > Body > Row_Cols { left column | Col_Centre | right column }, 16 px gaps, NO enclosing dark panel
if not _find(main, 'Root'):
    add(main, U.CanvasPanel, 'Root', '', var=False)
if not _find(main, 'Panel'):
    anchor(_clear(_bdr(main, 'Panel', 'Root', '000000', (0, 0, 0, 0), var=True)), 0.10, 0.10, 0.90, 0.90)
    _vb(main, 'Body', 'Panel', var=True, h='Fill', v='Fill')
    _hb(main, 'Row_Cols', 'Body', var=True, fill=1.0, h='Fill', v='Fill')

# ---- 3. left column Pnl_FleetLeft (SizeBox 230)
if not _find(main, 'Pnl_FleetLeft'):
    _sz(main, 'Sz_FleetLeft', 'Row_Cols', 230, None, pad=(0, 0, 16, 0), v='Fill')
    _vb(main, 'Pnl_FleetLeft', 'Sz_FleetLeft', var=True, h='Fill', v='Fill')
    box(main, 'Bx_FlCtl', 'Pnl_FleetLeft', 'Fleet Control', 'Txt_FlCtlTitle', side=True)
    field(main, 'FlGroup', 'Bx_FlCtl_In', 'Group', 'select', row='Row_FlGroup', lbl='Txt_FlGroupLbl', ctl='Sel_FlGroup', txt='Txt_SelFlGroup')
    _find(main, 'Txt_SelFlGroup').set_editor_property('text', U.Text('All ships'))
    slot(box(main, 'Bx_FlControl', 'Pnl_FleetLeft', 'Control', 'Txt_FlControlTitle', side=True), pad=(0, 10, 0, 0))
    # E-stop copied from mining: hazard-stripe frame (T_HazardStripes tiled, Border.background) > yellow rim E8B800 > red rounded button (hover E01818, pressed 7A0E0E)
    hazard_border(main, 'Row_FlEstopFrame', 'Bx_FlControl_In', (9, 9, 9, 9), h='Fill')
    _bdr(main, 'Row_FlEstopRim', 'Row_FlEstopFrame', 'E8B800', (3, 3, 3, 3), h='Fill', v='Fill')
    icon_button(main, 'Btn_FlEmergencyStop', 'Row_FlEstopRim', None, None, 'Txt_FlEmergencyStop', 'EMERGENCY STOP', hex='C81414', h='Fill', v='Fill')
    _find(main, 'Btn_FlEmergencyStop').set_editor_property('widget_style', button_style(round_brush(lin('C81414'), 6.0), round_brush(lin('E01818'), 6.0), round_brush(lin('7A0E0E'), 6.0)))
    _find(main, 'Txt_FlEmergencyStop').set_editor_property('color_and_opacity', slate_color(lin('FFFFFF')))
    size(main, 'Txt_FlEmergencyStop', FONTS[2])
    for k, lb, bn in (('Start', 'Start selected', 'Btn_FlStart'), ('Hold', 'Hold selected', 'Btn_FlHold')):
        icon_button(main, bn, 'Bx_FlControl_In', None, None, 'Txt_Fl' + k, lb, pad=(0, 2, 0, 0), h='Fill')
        size(main, 'Txt_Fl' + k, FONTS[0] + 1)
    slot(box(main, 'Bx_FlActions', 'Pnl_FleetLeft', 'Actions', 'Txt_FlActionsTitle', side=True), pad=(0, 10, 0, 0))
    for k, lb, bn in (('Dock', 'Dock', 'Btn_FlDock'), ('Undock', 'Undock', 'Btn_FlUndock'), ('Refuel', 'Refuel', 'Btn_FlRefuel')):
        icon_button(main, bn, 'Bx_FlActions_In', None, None, 'Txt_Fl' + k, lb, pad=(0, 2, 0, 0), h='Fill')
    icon_button(main, 'Btn_FlRecallL', 'Bx_FlActions_In', 'T_IconPin', 'Img_FlRecallL', 'Txt_FlRecallL', 'Recall to me', hex=GOLD, pad=(0, 2, 0, 0), h='Fill')

# ---- 4. centre column: title bar (Header_L tab + Header_Bar line + Header_R cap with X) > outline > body > inset > tab bar + PageSwitcher
if not _find(main, 'Col_Centre'):
    _vb(main, 'Col_Centre', 'Row_Cols', var=True, fill=1.0, h='Fill', v='Fill')
    _clear(_bdr(main, 'TitleBar', 'Col_Centre', '000000', (0, 0, 0, 0), var=True, h='Fill'))
    _hb(main, 'Row_Title', 'TitleBar')
    _sz(main, 'Sz_FlTitleTab', 'Row_Title', None, HDR_H, v='Fill')
    tex_border(main, 'Bdr_FlTitleTab', 'Sz_FlTitleTab', 'Header_L', M_TAB, (22, 0, 22, 0), h='Fill', v='Fill')
    txt(main, 'Txt_FlTitle', 'Bdr_FlTitleTab', 'Fleet', FONTS[2], TEXT, var=True, v='Center')
    tex_border(main, 'Bdr_FlTitleLine', 'Row_Title', 'Header_Bar', M_LINE, fill=1.0, h='Fill', v='Fill')
    tex_border(main, 'Bdr_FlTitleCap', 'Row_Title', 'Header_R', M_CAP, (14, 2, 8, 4), h='Fill', v='Fill')
    _sz(main, 'Sz_FlClose', 'Bdr_FlTitleCap', 24, None, h='Fill', v='Fill')
    cb = _btn(main, 'Btn_FlClose', 'Sz_FlClose', BUTTON, h='Fill', v='Fill')
    cb.set_editor_property('widget_style', button_style(round_brush(lin('FFFFFF', 0.0), 2.0), round_brush(lin('FFFFFF', 0.25), 2.0), round_brush(lin('000000', 0.25), 2.0)))
    txt(main, 'Txt_FlClose', 'Btn_FlClose', 'X', FONTS[2], DARKTXT, var=True, h='Center', v='Center')
    _bdr(main, 'Bdr_CentreEdge', 'Col_Centre', OUTLINE, (2, 2, 2, 2), fill=1.0, h='Fill', v='Fill')
    _bdr(main, 'Bdr_CentreBody', 'Bdr_CentreEdge', BODY, (8, 8, 8, 8), h='Fill', v='Fill')
    _bdr(main, 'Bdr_CentreInset', 'Bdr_CentreBody', INSET, (8, 8, 8, 8), h='Fill', v='Fill')
    _vb(main, 'Col_CentreIn', 'Bdr_CentreInset', h='Fill', v='Fill')
    _hb(main, 'Row_TabBar', 'Col_CentreIn', var=True, h='Fill', pad=(0, 0, 0, 8))
    for i, (bn, tn, lb) in enumerate((('Btn_TabFleet', 'Txt_TabFleet', 'Fleet'), ('Btn_TabRole', 'Txt_TabRole', 'Role'), ('Btn_TabEscort', 'Txt_TabEscort', 'Escort'))):
        tab(main, bn, tn, lb, parent='Row_TabBar', clone=None, hex='D49A00' if i == 0 else TAB_OFF, txt_hex=DARKTXT if i == 0 else TEXT)
        slot(_find(main, bn + '_Frm'), pad=(0 if i == 0 else 3, 0, 0, 0), fill=1.0, h='Fill', v='Fill')
    sw = add(main, U.WidgetSwitcher, 'PageSwitcher', 'Col_CentreIn', True)
    slot(sw, fill=1.0, h='Fill', v='Fill')
    page(main, 'Pnl_PageFleet'); page(main, 'Pnl_PageRole'); page(main, 'Pnl_PageEscort')  # role + escort content: add_role_pages.py

# ---- 5. right column Pnl_FleetStatus (SizeBox 260)
if not _find(main, 'Pnl_FleetStatus'):
    _sz(main, 'Sz_FleetStatus', 'Row_Cols', 260, None, pad=(16, 0, 0, 0), v='Fill')
    _vb(main, 'Pnl_FleetStatus', 'Sz_FleetStatus', var=True, h='Fill', v='Fill')
    box(main, 'Bx_FlStatus', 'Pnl_FleetStatus', 'Status', 'Txt_FlStatusTitle', side=True)
    _sz(main, 'Sz_FlStatHolo', 'Bx_FlStatus_In', 150, 100, h='Center')
    _ship_img(main, 'Img_FlStatHolo', 'Sz_FlStatHolo', 100)
    _hb(main, 'Row_FlStatHead', 'Bx_FlStatus_In')
    txt(main, 'Txt_FlStatName', 'Row_FlStatHead', '', FONTS[2], var=True, fill=1.0, v='Center')
    chip(main, 'Bdr_FlStatChip', 'Row_FlStatHead', 'Txt_FlStatRole')
    _hb(main, 'Row_FlStatAction', 'Bx_FlStatus_In', var=True)
    txt(main, 'Txt_FlStatActionLbl', 'Row_FlStatAction', 'Action', FONTS[1], DIM, pad=(0, 0, 8, 0), v='Center')
    txt(main, 'Txt_FlStatAction', 'Row_FlStatAction', '', var=True, fill=1.0, v='Center')
    for k, lb in (('Next', 'Next'), ('Speed', 'Speed')):  # Txt_FlStatNext, Txt_FlStatSpeed
        _hb(main, 'Row_FlStat' + k, 'Bx_FlStatus_In', var=True)
        txt(main, 'Txt_FlStat%sLbl' % k, 'Row_FlStat' + k, lb, FONTS[1], DIM, pad=(0, 0, 8, 0), v='Center')
        txt(main, 'Txt_FlStat' + k, 'Row_FlStat' + k, '', var=True, fill=1.0, v='Center')
    _hb(main, 'Row_FlStatMine', 'Bx_FlStatus_In', var=True)
    txt(main, 'Txt_FlStatMineLbl', 'Row_FlStatMine', 'Mining', FONTS[1], DIM, pad=(0, 0, 8, 0), v='Center')
    txt(main, 'Txt_FlStatMine', 'Row_FlStatMine', '', var=True, fill=1.0, v='Center')
    _collapse(_find(main, 'Row_FlStatMine'))
    for k, lb, hx, pct, pb in (('Cargo', 'Cargo', BARGREEN, 'Txt_FlStatCargoPct', 'PB_FlStatCargo'), ('Hull', 'Hull', '6AA8FF', 'Txt_FlStatHullPct', 'PB_FlStatHull'),
                               ('Shield', 'Shield', '6AA8FF', 'Txt_FlStatShieldPct', 'PB_FlStatShield')):
        _hb(main, 'Row_FlStat' + k, 'Bx_FlStatus_In', var=True)
        txt(main, 'Txt_FlStat%sLbl' % k, 'Row_FlStat' + k, lb, FONTS[1], DIM, fill=1.0, v='Center')
        txt(main, pct, 'Row_FlStat' + k, '', FONTS[1], DIM, var=True, v='Center')
        bar(main, pb, 'Bx_FlStatus_In', hx, 6)
    for k, lb, ic, hx, bn in (('SetRole', 'Set role', None, GOLD, 'Btn_FlSetRole'), ('RecallS', 'Recall to me', 'T_IconPin', BUTTON, 'Btn_FlRecallS'),
                              ('HoldS', 'Hold position', 'T_IconPause', BUTTON, 'Btn_FlHoldS')):
        icon_button(main, bn, 'Bx_FlStatus_In', ic, 'Img_Fl' + k if ic else None, 'Txt_Fl' + k, lb, hex=hx, pad=(0, 2, 0, 0), h='Fill')
        size(main, 'Txt_Fl' + k, FONTS[0] + 1)
    box(main, 'Bx_FlSelected', 'Pnl_FleetStatus', 'Selected (0)', 'Txt_FlSelTitle', side=True)
    _collapse(_find(main, 'Bx_FlSelected'))
    _hb(main, 'Row_FlSelHolos', 'Bx_FlSelected_In')
    for i in range(1, 6):
        _sz(main, 'Sz_FlSelHolo_%d' % i, 'Row_FlSelHolos', 40, 26, pad=(0, 0, 3, 0))
        _ship_img(main, 'Img_FlSelHolo_%d' % i, 'Sz_FlSelHolo_%d' % i, 26)
        _collapse(_find(main, 'Sz_FlSelHolo_%d' % i))
    _hb(main, 'Row_FlSelRole', 'Bx_FlSelected_In')
    txt(main, 'Txt_FlSelRoleLbl', 'Row_FlSelRole', 'Role', FONTS[1], DIM, pad=(0, 0, 8, 0))
    txt(main, 'Txt_FlSelRole', 'Row_FlSelRole', '', var=True)
    txt(main, 'Txt_FlSelNote', 'Bx_FlSelected_In', 'Mixed roles: only common actions shown.', FONTS[1], DIM, var=True)

# ---- 6. Fleet page (every ancestor of Row_FlListScroll has fill 1: Panel > Body > Row_Cols > Col_Centre > PageSwitcher > Pnl_PageFleet > Bx_FlMain > _Col > _Body > _In)
if not _find(main, 'Bx_FlMain'):
    box(main, 'Bx_FlMain', 'Pnl_PageFleet', 'Fleet Control - 0 selected', 'Txt_FlMainTitle', fill=True)
    slot(_find(main, 'Bx_FlMain'), fill=1.0, h='Fill', v='Fill')
    P = 'Bx_FlMain_In'
    _hb(main, 'Row_FlStrip', P, var=True, h='Fill', pad=(0, 0, 0, 4))
    class_strip(main, 'FlCls', 'Row_FlStrip', CLASSES)
    selection_bar(main, 'Bdr_FlSelBar', 'Row_FlSelBar', P, 'Txt_FlSelCount', 'Txt_FlSetRoleLbl', 'Set role for selected:', 'Sel_FlRole', 'Txt_SelFlRole',
                  [('Btn_FlApply', 'Txt_FlApply', 'Apply', True), ('Btn_FlNewGroup', 'Txt_FlNewGroup', 'New group from selection', False)])
    _collapse(_find(main, 'Bdr_FlSelBar'))
    col_head(main, P, FLEET_COLS, FLEET_LABELS, row='Row_FlColHead')
    sb = add(main, U.ScrollBox, 'Row_FlListScroll', P, True)
    slot(sb, fill=1.0, h='Fill', v='Fill')
    dyn_list(main, 'Dyn_FlList', 'Row_FlListScroll', 'WBP_FleetShipRow', spacing=2.0)
    _collapse(_vb(main, 'Pnl_FlEmpty', P, var=True, h='Fill'))
    box(main, 'Bx_FlEmpty', 'Pnl_FlEmpty', 'Empty fleet', 'Txt_FlEmptyHdr')
    _sz(main, 'Sz_FlEmptyHolo', 'Bx_FlEmpty_In', 90, 60, h='Center')
    _ship_img(main, 'Img_FlEmptyHolo', 'Sz_FlEmptyHolo', 60)
    txt(main, 'Txt_FlEmptyTitle', 'Bx_FlEmpty_In', 'No ships yet', FONTS[2], var=True, h='Center')
    t = txt(main, 'Txt_FlEmptyHint', 'Bx_FlEmpty_In', 'Build a ship at a station and it appears here.', FONTS[1], DIM, var=True)
    t.set_editor_property('auto_wrap_text', True)
    action_bar(main, 'Bdr_FlActionBar', 'Row_FlActionBar', P, [
        ('Btn_FlSelect', 'Txt_FlSelect', 'Select ship', 'T_IconShip', 'Img_FlSelect'), ('Btn_FlRecall', 'Txt_FlRecall', 'Recall to me', 'T_IconPin', 'Img_FlRecall'),
        ('Btn_FlHold2', 'Txt_FlHold2', 'Hold', 'T_IconPause', 'Img_FlHold2'), ('Btn_FlDockBtn', 'Txt_FlDockBtn', 'Return to dock', 'T_IconStDocking', 'Img_FlDockBtn'),
        ('Btn_FlSend', 'Txt_FlSend', 'Send ship', 'T_IconUp', 'Img_FlSend'), None, ('Btn_FlUnselectAll', 'Txt_FlUnselectAll', 'Unselect all', None, None)])

# ---- 7. modal (collapsed; Lua pools Opt_Modal_1..40): full-canvas dim button + centred box
if not _find(main, 'Pnl_Modal'):
    m = add(main, U.CanvasPanel, 'Pnl_Modal', 'Root', True)
    anchor(m, 0.0, 0.0, 1.0, 1.0)
    dim = _btn(main, 'Btn_ModalOutside', 'Pnl_Modal', '000000')
    dim.set_editor_property('widget_style', button_style(round_brush(lin('000000', 0.55), 0.0), round_brush(lin('000000', 0.55), 0.0), round_brush(lin('000000', 0.55), 0.0)))
    anchor(dim, 0.0, 0.0, 1.0, 1.0)
    box(main, 'Bx_Modal', 'Pnl_Modal', 'Select', 'Txt_ModalTitle', fill=True)
    anchor(_find(main, 'Bx_Modal'), 0.30, 0.15, 0.70, 0.85)
    icon_button(main, 'Btn_ModalClose', 'Bx_Modal_In', None, None, 'Txt_ModalClose', 'Close', hex=DANGER, pad=(0, 0, 0, 4), h='Right')
    sb = add(main, U.ScrollBox, 'Row_ModalScroll', 'Bx_Modal_In', True)
    slot(sb, fill=1.0, h='Fill', v='Fill')
    for i in range(1, 41):
        _btn(main, 'Opt_Modal_%d' % i, 'Row_ModalScroll', BUTTON, h='Fill', pad=(0, 0, 0, 2))
        _t(main, 'Txt_ModalC1_%d' % i, 'Opt_Modal_%d' % i, FONTS[0], TEXT, label='', pad=(10, 5, 10, 5), h='Left', v='Center')
    _collapse(m)

save(main)
import add_role_pages  # role cards + escort page into the same WBP (loads it, saves it)
print('SAVED')
