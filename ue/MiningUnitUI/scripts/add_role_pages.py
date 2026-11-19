"""Role page + Escort page content for the Fleet window /Game/FleetUI/WBP_FleetWindow (spec fleet-ui-widget-spec.md sections 3 and 4). Runs AFTER add_fleet_window.py
(imported by it at the end; tabs, Pnl_PageRole, Pnl_PageEscort, row WBPs). Only composes uicomp.py factories; role cards = ONE loop over ROLES (FIELDS table).
Idempotent (uicomp factories return existing widgets; nothing is deleted). Missing widgets -> printed and skipped.
Run: normally via add_fleet_window.py (UnrealEditor-Cmd <uproject> -ExecutePythonScript=add_fleet_window.py -unattended -nullrhi, editor closed). Not run by the build."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uew; uew.OUT = '/Game/FleetUI'
from uicomp import *
from uicomp import _find, _hb, _vb, _sz, _img, _ship_img, _bdr, _t

KINDS = {'sel': ('select', 'Sel_'), 'edit': ('number', 'Edt_'), 'check': ('check', 'Chk_')}
# role -> (letter, [(key, label, kind)]); widget names: Sel_/Edt_/Chk_Rl<letter><key>, Row_Rl<letter><key>, Txt_Rl<letter><key>Lbl, Txt_SelRl<letter><key>
ROLES = [
    ('Defend', 'D', [('Anchor', 'Anchor', 'sel'), ('Radius', 'Engage radius R (km)', 'edit'), ('GiveUp', 'Give up past (km)', 'edit'),
                     ('Flee', 'Flee at hull below (%)', 'edit'), ('Pick', 'Pick target', 'sel'), ('Return', 'Return to anchor when clear', 'check')]),
    ('Haul', 'H', [('From', 'Load at', 'sel'), ('To', 'Unload at', 'sel'), ('Item', 'Item', 'sel'), ('Amount', 'Amount (0 = all)', 'edit'),
                   ('Repeat', 'Repeat', 'check'), ('AddRow', 'Add item row', 'btn')]),
    ('Attack', 'A', [('Search', 'Search area', 'sel'), ('Pick', 'Pick target', 'sel'), ('Flee', 'Flee at hull below (%)', 'edit'), ('Idle', 'When no enemies', 'sel')]),
    ('Escort', 'E', [('Follow', 'Follow', 'sel'), ('Slot', 'Slot', 'sel'), ('Dist', 'Distance (m)', 'edit'), ('Warp', 'Warp with target', 'check')]),
]
BTNS = {'RlHAddRow': ('Btn_RlHAddRow', 'Txt_RlHAddRow')}  # 'btn' fields: explicit literal names
TITLES = {'Defend': 'Role: Defend', 'Haul': 'Role: Haul', 'Attack': 'Role: Attack', 'Escort': 'Role: Escort (see screen 2)'}
CLASSES = ['All', 'Miner', 'Hauler', 'Fighter', 'Frigate', 'Capital']
# icon textures that exist in Content/MiningUnitUI; i_call/i_home/i_sel have no texture yet -> nearest existing (GUESS, swap when i_* are generated)
IC_CALL, IC_HOLD, IC_SEL, IC_DOCK, IC_SEND = 'T_IconUp', 'T_IconStIdle', 'T_IconShip', 'T_IconDown', 'T_IconPin'
ESC_BAR = [('Btn_FlESelect', 'Txt_FlESelect', 'Select ship', IC_SEL, 'Img_FlESelect'), ('Btn_FlEDock', 'Txt_FlEDock', 'Return to dock', IC_DOCK, 'Img_FlEDock'),
           ('Btn_FlERecall', 'Txt_FlERecall', 'Recall to me', IC_CALL, 'Img_FlERecall'), ('Btn_FlEHold', 'Txt_FlEHold', 'Hold', IC_HOLD, 'Img_FlEHold'),
           ('Btn_FlESend', 'Txt_FlESend', 'Send ship', IC_SEND, 'Img_FlESend'), None,
           ('Btn_FlEUnselect', 'Txt_FlEUnselect', 'Unselect target', None, None)]
# escort list header cells: (key, label, px or None, fill)
ESC_HEAD = [('Holo', '', 44, None), ('Name', 'Ship', None, 1.1), ('Role', 'Role', 66, None), ('Doing', 'Doing now', None, 1.7),
            ('Hull', 'Hull', 78, None), ('Dist', 'Distance', 70, None), ('Pin', '', 28, None), ('Act', '', 28, None)]


def head(w, row, parent, prefix):
    """column header row (10 AAAAAA) with the same widths as WBP_FleetEscortRow"""
    _hb(w, row, parent, var=True, pad=(6, 0, 6, 2), h='Fill')
    for k, label, px, fill in ESC_HEAD:
        if px:
            _sz(w, 'Sz_%s%s' % (prefix, k), row, px, None, pad=(0, 0, 8, 0))
            _t(w, 'Txt_%s%s' % (prefix, k), 'Sz_%s%s' % (prefix, k), FONTS[1], 'AAAAAA', var=False, label=label)
        else:
            _t(w, 'Txt_%s%s' % (prefix, k), row, FONTS[1], 'AAAAAA', var=False, label=label, pad=(0, 0, 8, 0), fill=fill, h='Fill')


def role_card(w, parent, role, letter, fields):
    c = 'Rl' + role
    p = _vb(w, 'Pnl_' + c, parent, var=True, pad=(0, 0, 5, 0), fill=1.0, h='Fill', v='Fill')
    box(w, 'Bx_' + c, 'Pnl_' + c, TITLES[role], 'Txt_%sTitle' % c)
    inn = 'Bx_%s_In' % c
    _hb(w, 'Row_%sTop' % c, inn)
    _ship_img(w, 'Img_%sHolo' % c, 'Row_%sTop' % c, 47)
    _vb(w, 'Col_%sTop' % c, 'Row_%sTop' % c, pad=(8, 0, 0, 0))
    chip(w, 'Bdr_%sChip' % c, 'Col_%sTop' % c, 'Txt_%sRole' % c, 'None')
    _hb(w, 'Row_%sState' % c, 'Col_%sTop' % c, pad=(0, 3, 0, 0))
    _t(w, 'Txt_%sStateTxt' % c, 'Row_%sState' % c, FONTS[0], '8FE08E')
    _t(w, 'Txt_%sDoingLbl' % c, inn, FONTS[1], 'AAAAAA', var=False, label='Doing now', pad=(0, 4, 0, 0))
    _t(w, 'Txt_%sDoing' % c, inn, FONTS[0], TEXT).set_editor_property('auto_wrap_text', True)
    for key, label, kind in fields:
        k = 'Rl%s%s' % (letter, key)
        if kind == 'btn':
            bn, tn = BTNS[k]
            icon_button(w, bn, inn, None, None, tn, label, pad=(0, 4, 0, 0), h='Left')
        else:
            fk, pre = KINDS[kind]
            field(w, k, inn, label, fk, lbl='Txt_%sLbl' % k, ctl=pre + k)
    if letter == 'H':
        _ship_img(w, 'Img_RlHItem', 'Row_RlHItem', 24)  # placeholder, Lua sets the item texture
    _hb(w, 'Row_%sBtns' % c, inn, var=True, pad=(0, 6, 0, 0), h='Fill')
    for key, label, icon in (('Recall', 'Recall to me', IC_CALL), ('Hold', 'Hold', IC_HOLD), ('Default', 'Set as default', None)):
        n = c + key
        icon_button(w, 'Btn_' + n, 'Row_%sBtns' % c, icon, 'Img_' + n if icon else None, 'Txt_' + n, label, fill=1.0, h='Fill', pad=(0, 0, 5, 0))
    return p


def main_build():
    main = U.load_asset(uew.OUT + '/WBP_FleetWindow')
    pr, pe = _find(main, 'Pnl_PageRole'), _find(main, 'Pnl_PageEscort')
    for n, x in (('Pnl_PageRole', pr), ('Pnl_PageEscort', pe)):
        if not x:
            print('MISSING (run add_fleet_window.py first):', n)

    if pr and not _find(main, 'Row_RoleCards'):
        _hb(main, 'Row_RoleCards', 'Pnl_PageRole', var=True, h='Fill', v='Fill')
        for role, letter, fields in ROLES:
            role_card(main, 'Row_RoleCards', role, letter, fields)

    if pe and not _find(main, 'Row_FlEscBody'):
        _hb(main, 'Row_FlEscStrip', 'Pnl_PageEscort', var=True, h='Fill')
        class_strip(main, 'FlE', 'Row_FlEscStrip', CLASSES)
        _hb(main, 'Row_FlEscBody', 'Pnl_PageEscort', var=True, pad=(0, 6, 0, 0), fill=1.0, h='Fill', v='Fill')
        _vb(main, 'Row_FlEscLeft', 'Row_FlEscBody', var=True, pad=(0, 0, 16, 0), fill=0.2, h='Fill')
        _vb(main, 'Row_FlEscRight', 'Row_FlEscBody', var=True, fill=0.8, h='Fill')
        box(main, 'Bx_FlFollowing', 'Row_FlEscLeft', 'Following')
        i = 'Bx_FlFollowing_In'
        _bdr(main, 'Bdr_FlFollowCard', i, INSET, (0, 0, 6, 0), var=True, h='Fill')
        _hb(main, 'Row_FlFollowCard', 'Bdr_FlFollowCard')
        _sz(main, 'Sz_FlFollowStripe', 'Row_FlFollowCard', 3, None, v='Fill')
        _bdr(main, 'Bdr_FlFollowStripe', 'Sz_FlFollowStripe', 'EE66BB', (0, 0, 0, 0), h='Fill', v='Fill')
        _ship_img(main, 'Img_FlFollowHolo', 'Row_FlFollowCard', 28)
        _vb(main, 'Col_FlFollowCard', 'Row_FlFollowCard', pad=(8, 0, 0, 0))
        _t(main, 'Txt_FlFollowName', 'Col_FlFollowCard', FONTS[2], TEXT, label='You (Captain)')
        _t(main, 'Txt_FlFollowCount', 'Col_FlFollowCard', FONTS[1], 'AAAAAA')
        field(main, 'FlFollow', i, 'Follow who', 'select', row='Row_FlFollow', lbl='Txt_FlFollowWhoLbl', ctl='Sel_FlFollow', txt='Txt_SelFlFollow')
        field(main, 'FlDist', i, 'Keep distance', 'select', row='Row_FlDist', lbl='Txt_FlDistLbl', ctl='Sel_FlDist', txt='Txt_SelFlDist')
        icon_button(main, 'Btn_FlStopAll', i, None, None, 'Txt_FlStopAll', 'Stop escorting all', hex=DANGER, pad=(0, 4, 0, 0), h='Fill')
        box(main, 'Bx_FlOtherTargets', 'Row_FlEscLeft', 'Other targets')
        box(main, 'Bx_FlAssigned', 'Row_FlEscRight', 'Escorting you - 0 ships', 'Txt_FlAssignedTitle')
        head(main, 'Row_FlAsgHead', 'Bx_FlAssigned_In', 'FlAsgH')
        box(main, 'Bx_FlFree', 'Row_FlEscRight', 'Add to escort - free ships')
        _hb(main, 'Row_FlFStrip', 'Bx_FlFree_In', var=True, h='Fill')
        class_strip(main, 'FlF', 'Row_FlFStrip', CLASSES)
        head(main, 'Row_FlFreeHead', 'Bx_FlFree_In', 'FlFreeH')
        for dn, parent, row in (('Dyn_FlTargets', 'Bx_FlOtherTargets_In', 'WBP_FleetTargetRow'), ('Dyn_FlAssigned', 'Bx_FlAssigned_In', 'WBP_FleetEscortRow'),
                                ('Dyn_FlFree', 'Bx_FlFree_In', 'WBP_FleetEscortRow')):
            if U.EditorAssetLibrary.does_asset_exist('%s/%s' % (uew.OUT, row)):
                dyn_list(main, dn, parent, row, spacing=2.0)
            else:
                print('MISSING row WBP:', row)
        action_bar(main, 'Bdr_FlEscActionBar', 'Row_FlEscActionBar', 'Pnl_PageEscort', ESC_BAR)
    save(main)


main_build()
