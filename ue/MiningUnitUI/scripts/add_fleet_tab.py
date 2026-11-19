"""Fleet tab for WBP_MiningUnit, composed ONLY from uicomp factories; every widget name = fleet-ui-widget-spec.md.
Builds: row WBPs WBP_FleetShipRow / WBP_FleetEscortRow / WBP_FleetTargetRow (uicomp.list_row_wbp; escort+target rows are also used by the Escort page, slice d),
tabs Btn_TabFleet/Btn_TabRole, pages Pnl_PageFleet (5) / Pnl_PageRole (6, EMPTY, slice d), Pnl_FleetLeft, Pnl_FleetStatus,
Fleet page: Bx_FlMain > sub-tabs, Pnl_FlSubSwitcher (Pnl_FlOverview built, Pnl_FlEscort EMPTY), class strip, selection bar, Dyn_FlList, Pnl_FlEmpty, action bar.
Idempotent: row WBPs only built when missing (new_wbp deletes an existing asset); every other part is skipped when its name exists. Never deletes widgets.
Icons: existing T_Icon* textures reused (spec i_* icons not generated). Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=<this file> -unattended -nullrhi (editor closed). Not run by the build."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uicomp import *
from uicomp import _find, _vb, _hb, _t, _sz, _img, _bdr, _collapse

CLASSES = ['All', 'Miner', 'Hauler', 'Fighter', 'Frigate', 'Capital']


def exists(n):
    return U.EditorAssetLibrary.does_asset_exist(ASSETS + '/' + n)


def size(w, name, px):
    t = _find(w, name)
    f = t.get_editor_property('font')
    f.set_editor_property('size', px)
    t.set_editor_property('font', f)


def txt(main, name, parent, label, sz=FONTS[0], hex_=TEXT, var=False, **kw):
    return _t(main, name, parent, sz, hex_, var=var, label=label, **kw)


# ---- 1. row WBPs FIRST (slice d uses the names by string)
if not exists('WBP_FleetShipRow'):
    w = list_row_wbp('WBP_FleetShipRow', 'Fr_Root', 'Row_FrRow', [
        ('Chk_FrSel', 'check', 0, None, 18), ('Img_FrHolo', 'img', 0, (44, 28), 44), ('Txt_FrName', 'text', 1.1),
        ('Bdr_FrChip', 'chip', 0, ('Img_FrRoleIcon', 'Txt_FrRole'), 66), ('Row_FrDoing', 'icontext', 1.7, ('Img_FrState', 'Txt_FrDoing')),
        ('PB_FrHull', 'bar', 0, 'Txt_FrHullPct', 78), ('PB_FrCargo', 'bar', 0, 'Txt_FrCargoPct', 78),
        ('Btn_FrRemove', 'btn', 0, 'Txt_FrRemove'), ('Btn_FrGoto', 'btn', 0, 'Txt_FrGoto')],
        group=dict(name='Fg_Root', hdr='Btn_FgHdr', arrow='Img_FgArrow', name_txt='Txt_FgName', summary='Txt_FgSummary'),
        frame=('Fr_RowFrame', 'Bdr_FrSel', 'Btn_FrRow'))
    _collapse(_find(w, 'Btn_FrRemove')); _collapse(_find(w, 'Btn_FrGoto'))
    _t(w, 'Txt_FrNoCargo', 'PB_FrCargo_CellIn', FONTS[1], DIM, label='no cargo', v='Center'); _collapse(_find(w, 'Txt_FrNoCargo'))
    _t(w, 'Txt_FrLostHint', 'Fr_Root', FONTS[1], DIM); _collapse(_find(w, 'Txt_FrLostHint'))
    save(w)
if not exists('WBP_FleetEscortRow'):
    list_row_wbp('WBP_FleetEscortRow', 'Fe_Root', 'Row_FeRow', [
        ('Img_FeHolo', 'img', 0, (44, 28), 44), ('Txt_FeName', 'text', 1.1), ('Bdr_FeChip', 'chip', 0, ('Img_FeRoleIcon', 'Txt_FeRole'), 66),
        ('Row_FeDoing', 'icontext', 1.7, ('Img_FeState', 'Txt_FeDoing')), ('PB_FeHull', 'bar', 0, 'Txt_FeHullPct', 78),
        ('Txt_FeDist', 'text', 0, None, 70), ('Btn_FePin', 'btn', 0, 'Txt_FePin', 28), ('Btn_FeAction', 'btn', 0, 'Txt_FeAction', 28)],
        root_kind='border')
if not exists('WBP_FleetTargetRow'):
    list_row_wbp('WBP_FleetTargetRow', 'Ft_Root', 'Row_FtRow', [
        ('Img_FtHolo', 'img', 0, (44, 28), 44), ('Txt_FtName', 'text', 1.0), ('Btn_FtPick', 'btn', 0, 'Txt_FtPick', 28)], root_kind='border')

main = U.load_asset(ASSETS + '/WBP_MiningUnit')

# ---- 2. tabs + pages (PageSwitcher index 5, 6)
tab(main, 'Btn_TabFleet', 'Txt_TabFleet', 'Fleet', clone='Btn_TabDocks', clone_txt='Txt_TabDocks')
tab(main, 'Btn_TabRole', 'Txt_TabRole', 'Role', clone='Btn_TabFleet', clone_txt='Txt_TabFleet')
page(main, 'Pnl_PageFleet')
page(main, 'Pnl_PageRole')  # empty: slice d

# ---- 3. left column Pnl_FleetLeft
if not _find(main, 'Pnl_FleetLeft'):
    _collapse(_vb(main, 'Pnl_FleetLeft', 'Row_LeftCol', var=True, h='Fill'))
    box(main, 'Bx_FlCtl', 'Pnl_FleetLeft', 'Fleet Control', 'Txt_FlCtlTitle')
    field(main, 'FlGroup', 'Bx_FlCtl_In', 'Group', 'select', row='Row_FlGroup', lbl='Txt_FlGroupLbl', ctl='Sel_FlGroup', txt='Txt_SelFlGroup')
    _find(main, 'Txt_SelFlGroup').set_editor_property('text', U.Text('All ships'))
    box(main, 'Bx_FlControl', 'Pnl_FleetLeft', 'Control', 'Txt_FlControlTitle')
    # E-stop: hazard-stripe frame (T_HazardStripes tiled) > yellow rim > red button
    fr = _bdr(main, 'Row_FlEstopFrame', 'Bx_FlControl_In', 'FFFFFF', (9, 9, 9, 9), h='Fill')
    try:
        br = image_brush('%s/T_HazardStripes.T_HazardStripes' % ASSETS, lin('FFFFFF'), 32.0)
        br.set_editor_property('tiling', U.SlateBrushTileType.BOTH)
        fr.set_editor_property('brush', br)
    except Exception as e:
        print('hazard brush not set:', e)
    _bdr(main, 'Row_FlEstopRim', 'Row_FlEstopFrame', 'F2C500', (3, 3, 3, 3), h='Fill')
    icon_button(main, 'Btn_FlEmergencyStop', 'Row_FlEstopRim', None, None, 'Txt_FlEmergencyStop', 'EMERGENCY STOP', hex='C81414', h='Fill')
    _find(main, 'Txt_FlEmergencyStop').set_editor_property('color_and_opacity', slate_color(lin('FFFFFF')))
    size(main, 'Txt_FlEmergencyStop', FONTS[2])
    for k, lb, bn in (('Start', 'Start selected', 'Btn_FlStart'), ('Hold', 'Hold selected', 'Btn_FlHold')):
        icon_button(main, bn, 'Bx_FlControl_In', None, None, 'Txt_Fl' + k, lb, pad=(0, 2, 0, 0), h='Fill')
        size(main, 'Txt_Fl' + k, FONTS[0] + 1)
    box(main, 'Bx_FlActions', 'Pnl_FleetLeft', 'Actions', 'Txt_FlActionsTitle')
    for k, lb, bn in (('Dock', 'Dock', 'Btn_FlDock'), ('Undock', 'Undock', 'Btn_FlUndock'), ('Refuel', 'Refuel', 'Btn_FlRefuel')):
        icon_button(main, bn, 'Bx_FlActions_In', None, None, 'Txt_Fl' + k, lb, pad=(0, 2, 0, 0), h='Fill')
    icon_button(main, 'Btn_FlRecallL', 'Bx_FlActions_In', 'T_IconPin', 'Img_FlRecallL', 'Txt_FlRecallL', 'Recall to me', hex=GOLD, pad=(0, 2, 0, 0), h='Fill')

# ---- 4. right column Pnl_FleetStatus
if not _find(main, 'Pnl_FleetStatus'):
    _collapse(_vb(main, 'Pnl_FleetStatus', 'Row_StatusCol', var=True, h='Fill'))
    box(main, 'Bx_FlStatus', 'Pnl_FleetStatus', 'Status', 'Txt_FlStatusTitle')
    _sz(main, 'Sz_FlStatHolo', 'Bx_FlStatus_In', 150, 100, h='Center')
    _img(main, 'Img_FlStatHolo', 'Sz_FlStatHolo', None, 100)
    _hb(main, 'Row_FlStatHead', 'Bx_FlStatus_In')
    txt(main, 'Txt_FlStatName', 'Row_FlStatHead', '', FONTS[2], var=True, fill=1.0, v='Center')
    chip(main, 'Bdr_FlStatChip', 'Row_FlStatHead', 'Img_FlStatRoleIcon', 'Txt_FlStatRole')
    _hb(main, 'Row_FlStatAction', 'Bx_FlStatus_In', var=True)
    txt(main, 'Txt_FlStatActionLbl', 'Row_FlStatAction', 'Action', FONTS[1], DIM, pad=(0, 0, 8, 0), v='Center')
    _img(main, 'Img_FlStatState', 'Row_FlStatAction', None, 12)
    txt(main, 'Txt_FlStatAction', 'Row_FlStatAction', '', var=True, fill=1.0, v='Center')
    for k, lb in (('Next', 'Next'), ('Speed', 'Speed')):  # Txt_FlStatNext, Txt_FlStatSpeed
        _hb(main, 'Row_FlStat' + k, 'Bx_FlStatus_In', var=True)
        txt(main, 'Txt_FlStat%sLbl' % k, 'Row_FlStat' + k, lb, FONTS[1], DIM, pad=(0, 0, 8, 0), v='Center')
        txt(main, {'Next': 'Txt_FlStatNext', 'Speed': 'Txt_FlStatSpeed'}[k], 'Row_FlStat' + k, '', var=True, fill=1.0, v='Center')
    _hb(main, 'Row_FlStatMine', 'Bx_FlStatus_In', var=True)
    txt(main, 'Txt_FlStatMineLbl', 'Row_FlStatMine', 'Mining', FONTS[1], DIM, pad=(0, 0, 8, 0), v='Center')
    _sz(main, 'Sz_FlStatOre', 'Row_FlStatMine', 24, 24, v='Center')
    _img(main, 'Img_FlStatOre', 'Sz_FlStatOre', None, 24)
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
    box(main, 'Bx_FlSelected', 'Pnl_FleetStatus', 'Selected (0)', 'Txt_FlSelTitle')
    _collapse(_find(main, 'Bx_FlSelected'))
    _hb(main, 'Row_FlSelHolos', 'Bx_FlSelected_In')
    for i in range(1, 6):
        _sz(main, 'Sz_FlSelHolo_%d' % i, 'Row_FlSelHolos', 40, 26, pad=(0, 0, 3, 0))
        _img(main, 'Img_FlSelHolo_%d' % i, 'Sz_FlSelHolo_%d' % i, None, 26)
        _collapse(_find(main, 'Sz_FlSelHolo_%d' % i))
    _hb(main, 'Row_FlSelRole', 'Bx_FlSelected_In')
    txt(main, 'Txt_FlSelRoleLbl', 'Row_FlSelRole', 'Role', FONTS[1], DIM, pad=(0, 0, 8, 0))
    txt(main, 'Txt_FlSelRole', 'Row_FlSelRole', '', var=True)
    txt(main, 'Txt_FlSelNote', 'Bx_FlSelected_In', 'Mixed roles: only common actions shown.', FONTS[1], DIM, var=True)

# ---- 5. Fleet page
if not _find(main, 'Bx_FlMain'):
    box(main, 'Bx_FlMain', 'Pnl_PageFleet', 'Fleet Control - 0 selected', 'Txt_FlMainTitle')
    slot(_find(main, 'Bx_FlMain'), fill=1.0, h='Fill', v='Fill')
    _hb(main, 'Row_FlSubTabs', 'Bx_FlMain_In', var=True, pad=(0, 0, 0, 6))
    tab(main, 'Btn_FlSubOverview', 'Txt_FlSubOverview', 'Overview', parent='Row_FlSubTabs', clone=None, hex='3A3A3A', txt_hex=ACCENT)
    tab(main, 'Btn_FlSubEscort', 'Txt_FlSubEscort', 'Escort', parent='Row_FlSubTabs', clone=None, hex='262626', txt_hex=DIM)
    slot(_find(main, 'Btn_FlSubOverview'), pad=(0, 0, 2, 0))
    sw = add(main, U.WidgetSwitcher, 'Pnl_FlSubSwitcher', 'Bx_FlMain_In', True)
    slot(sw, fill=1.0, h='Fill', v='Fill')
    page(main, 'Pnl_FlOverview', 'Pnl_FlSubSwitcher')    # index 0
    page(main, 'Pnl_FlEscort', 'Pnl_FlSubSwitcher')      # index 1, EMPTY (slice d)
    _hb(main, 'Row_FlStrip', 'Pnl_FlOverview', var=True, h='Fill', pad=(0, 0, 0, 4))
    class_strip(main, 'FlCls', 'Row_FlStrip', CLASSES)
    selection_bar(main, 'Bdr_FlSelBar', 'Row_FlSelBar', 'Pnl_FlOverview', 'Txt_FlSelCount', 'Txt_FlSetRoleLbl', 'Set role for selected:', 'Sel_FlRole', 'Txt_SelFlRole',
                  [('Btn_FlApply', 'Txt_FlApply', 'Apply', True), ('Btn_FlNewGroup', 'Txt_FlNewGroup', 'New group from selection', False)])
    _collapse(_find(main, 'Bdr_FlSelBar'))
    _hb(main, 'Row_FlColHead', 'Pnl_FlOverview', pad=(6, 0, 6, 0), h='Fill')
    for i, (lb, px, fl) in enumerate([('', 18, 0), ('', 44, 0), ('Ship', 0, 1.1), ('Role', 66, 0), ('Doing now', 0, 1.7), ('Hull', 78, 0), ('Cargo', 78, 0)]):
        if px:
            _sz(main, 'Sz_FlColHead%d' % i, 'Row_FlColHead', px, None, pad=(0, 0, 8, 0))
            txt(main, 'Txt_FlColHead%d' % i, 'Sz_FlColHead%d' % i, lb, FONTS[1], '999999')
        else:
            txt(main, 'Txt_FlColHead%d' % i, 'Row_FlColHead', lb, FONTS[1], '999999', fill=fl, h='Fill', pad=(0, 0, 8, 0))
    sb = add(main, U.ScrollBox, 'Row_FlListScroll', 'Pnl_FlOverview', True)
    slot(sb, fill=1.0, h='Fill', v='Fill')
    dyn_list(main, 'Dyn_FlList', 'Row_FlListScroll', 'WBP_FleetShipRow', spacing=2.0)
    _collapse(_vb(main, 'Pnl_FlEmpty', 'Pnl_FlOverview', var=True, h='Fill'))
    box(main, 'Bx_FlEmpty', 'Pnl_FlEmpty', 'Empty fleet', 'Txt_FlEmptyHdr')
    _sz(main, 'Sz_FlEmptyHolo', 'Bx_FlEmpty_In', 90, 60, h='Center')
    _img(main, 'Img_FlEmptyHolo', 'Sz_FlEmptyHolo', None, 60)
    txt(main, 'Txt_FlEmptyTitle', 'Bx_FlEmpty_In', 'No ships yet', FONTS[2], var=True, h='Center')
    t = txt(main, 'Txt_FlEmptyHint', 'Bx_FlEmpty_In', 'Build a ship at a station and it appears here.', FONTS[1], DIM, var=True)
    t.set_editor_property('auto_wrap_text', True)
    icon_button(main, 'Btn_FlOpenShip', 'Bx_FlEmpty_In', None, None, 'Txt_FlOpenShip', 'Open Ship tab', hex=GOLD, h='Center')
    size(main, 'Txt_FlOpenShip', FONTS[0] + 1)
    action_bar(main, 'Bdr_FlActionBar', 'Row_FlActionBar', 'Pnl_FlOverview', [
        ('Btn_FlSelect', 'Txt_FlSelect', 'Select ship', 'T_IconShip', 'Img_FlSelect'), ('Btn_FlRecall', 'Txt_FlRecall', 'Recall to me', 'T_IconPin', 'Img_FlRecall'),
        ('Btn_FlHold2', 'Txt_FlHold2', 'Hold', 'T_IconPause', 'Img_FlHold2'), ('Btn_FlDockBtn', 'Txt_FlDockBtn', 'Return to dock', 'T_IconStDocking', 'Img_FlDockBtn'),
        ('Btn_FlSend', 'Txt_FlSend', 'Send ship', 'T_IconUp', 'Img_FlSend'), None, ('Btn_FlUnselectAll', 'Txt_FlUnselectAll', 'Unselect all', None, None)])

save(main)
print('SAVED')
