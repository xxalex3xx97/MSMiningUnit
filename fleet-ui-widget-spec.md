# Fleet UI widget BUILD SPEC (Direction A)

Sources: vault plans/fleet-ui-designs.html, tasks/2026-11-20-fleet-ui-stubs.md, ue/MiningUnitUI/SPEC.md. Phase 1 = layout + empty stubs only. Untracked file, do not commit.

## 0. Conventions (read first)
- Helpers (scripts/uew.py): `new_wbp(name)`, `add(wbp, cls, name, parent, var=True|False)`, `slot(w, pad=(l,t,r,b), fill=, h='Fill|Left|Center|Right', v=...)`, `text(wbp,name,parent,size,color,wrap,var,slot_kw)`, `icon_button(wbp,name,parent,icon,px,tint,box=(w,h))`, `button_style(n,h,p)`, `round_brush(hex,r)`, `image_brush(path,tint,px)`, `lin('RRGGBB')`, `dyn_list(wbp,name,parent,row_wbp,spacing=)`, `save(wbp)`. SizeBox: also set `bOverride_WidthOverride/HeightOverride=True`. Never delete widgets (collapse a stub). Scripts idempotent.
- IV = Is Variable. Python-built widgets are often NOT found by name walk in Lua (SPEC: Lua uses WALK_NAMES from an IV anchor). So set `var=True` on everything marked IV, plus every layout widget Lua must show/hide, AND register the names in WALK_NAMES (Lua task). Row_* layout widgets that Lua hides MUST be IV.
- Unmarked = layout/static (var=False). Static labels have fixed text set in python.
- Colours (hex, no #): gold C38800, gold-light F2B01E, outline 1C1C1C, body 3A3A3A, inset 2B2B2B, btn 4A4A4A, danger 9C2222, estop C81414, select-field bg D9D9D9 (text 000000), tab off 1E1E1E, sub-tab off 262626 / on 3A3A3A, text main E6E6E6, dim text BBBBBB / AAAAAA, selected-row tint 33291A, bar track 1A1A1A (border 111111).
- Role colours: Mine 42DA41, Haul 5FD0E6, Escort EE66BB, Defend FFAA55, Attack FF6666, None A0A0A0. Chip text 111111 on role colour.
- State colours: Working 8FE08E, Waiting CFCFCF, Fleeing FFCC33, Lost FF7777. Hull bar 6AA8FF (low <30%: FFCC33), cargo bar 42DA41.
- Text sizes: body 11, secondary/hint 10, title 13. Row ship name 12 (design exception). Big buttons 12.
- Box = `Bx_<n>`: Border outline 1C1C1C pad 2 > VerticalBox { Border gold C38800 header (pad 8,2; title 13, colour 111111) ; Border 2px line F2B01E ; Border body 3A3A3A pad 5 > content }.
- Inset = Border 2B2B2B pad 5. Button = Button (bg 4A4A4A, hover 5A5A5A, pressed 333333) > TextBlock 11 centered, pad (8,3,8,3). Gold button bg C38800 text 111111. Danger bg 9C2222. Big button text 12, pad (8,5,8,5).
- Select field Sel_* = Button bg D9D9D9 > HorizontalBox { Txt (11, 000000, fill) ; Txt "v" (10, 555555) }; click opens existing modal (selInit/selSet).
- Bars = ProgressBar 6 px high (SizeBox height 6), fill colours above, track 1A1A1A.
- Hologram = Image 44x28 in rows, 34x22 in strip, 150x100 Status box, 40x26 mini selection. Uses existing ship-card hologram mechanism (Lua sets texture).
- Icons (own textures to generate): i_mine i_haul i_escort i_defend i_attack i_none, s_go s_idle s_flee s_lost, i_plus i_x i_pin i_home i_call i_sel i_arrow i_idle; Images 12x12 tinted.
- Hazard stripe E-stop: reuse T_HazardStripes tile.
- Row WBP inner names are unique only inside that WBP; prefixed `Fr_` (ship row), `Fg_` (group header, inside ship row), `Fe_` (escort row), `Ft_` (target row), `Fm_` (role modal row, only if needed).

## 1. Integration into WBP_MiningUnit (script add_fleet_tab.py)
- TabBar: add `Btn_TabFleet` (IV) > `Txt_TabFleet` (IV) "Fleet"; `Btn_TabRole` (IV) > `Txt_TabRole` (IV) "Role". Style as other tabs (off 1E1E1E, on C38800). Order: Ship Items Operation Log Docks Fleet Role.
- PageSwitcher: add children index 5, 6: `Pnl_PageFleet` (VerticalBox, IV), `Pnl_PageRole` (VerticalBox, IV).
- LEFT column (Row_LeftCol): add `Pnl_FleetLeft` (VerticalBox, IV, Collapsed; shown on Fleet/Role tab, existing Pnl_Fleet/Control/Actions hidden then).
- RIGHT column (Row_StatusCol): add `Pnl_FleetStatus` (VerticalBox, IV, Collapsed); existing Status window collapsed on Fleet/Role tab.
- Modal: reuse Pnl_Modal; role picker via selInit rows (section 5).

## 2. FLEET TAB

### 2.1 Left column Pnl_FleetLeft (VerticalBox, gap 6)
```
Pnl_FleetLeft VerticalBox IV Collapsed
 Bx_FlCtl "Fleet Control"
  Row_FlGroup HorizontalBox pad 0,2
   Txt_FlGroupLbl TextBlock 11 "Group"
   Sel_FlGroup Button IV fill 1 bg D9D9D9
    Txt_SelFlGroup TextBlock IV 11 000000 "All ships"
 Bx_FlControl "Control"
  Row_FlEstopFrame Border (T_HazardStripes tiled, pad 9) > Row_FlEstopRim Border F2C500 pad 3
   Btn_FlEmergencyStop Button IV bg C81414 hover E01818 pressed 7A0E0E
    Txt_FlEmergencyStop TextBlock IV 13 white "EMERGENCY STOP"
  Btn_FlStart Button IV big > Txt_FlStart IV 12 "Start selected"
  Btn_FlHold Button IV big > Txt_FlHold IV 12 "Hold selected"
 Bx_FlActions "Actions"
  Btn_FlDock > Txt_FlDock "Dock"
  Btn_FlUndock > Txt_FlUndock "Undock"
  Btn_FlRefuel > Txt_FlRefuel "Refuel"
  Btn_FlRecallL gold > Row HorizontalBox: Img_FlRecallL (i_call 12x12) + Txt_FlRecallL "Recall to me"
```
(all Btn_/Txt_ above IV, text 11, buttons full width, 2 px apart.)

### 2.2 Centre page Pnl_PageFleet
```
Pnl_PageFleet VerticalBox IV
 Bx_FlMain "Fleet Control - N selected" (Txt_FlMainTitle IV 13)
  Row_FlSubTabs HorizontalBox IV (gap 2, mb 6)
   Btn_FlSubOverview IV bg 3A3A3A text F2B01E bold, 2px F2B01E line under (on) > Txt_FlSubOverview IV "Overview"
   Btn_FlSubEscort IV bg 262626 text BBBBBB > Txt_FlSubEscort IV "Escort"
   (pad 12,3)
  Pnl_FlSubSwitcher WidgetSwitcher IV (0 Overview, 1 Escort)
   Pnl_FlOverview VerticalBox IV
    Row_FlStrip HorizontalBox IV (gap 4, mb 4)  class strip, 6 cells, K = All, Miner, Hauler, Fighter, Frigate, Capital
     Btn_FlCls<K> Button IV, 78 wide SizeBox, bg 2B2B2B
      VerticalBox centered: Img_FlCls<K> Image IV 34x22 (All: Txt "ALL" 13 bold instead)
       Row HorizontalBox: Txt_FlClsName<K> IV 10 + Txt_FlClsCnt<K> IV 10 CCCCCC
      Bdr_FlClsSel<K> Border IV (selected: 2px F2B01E + bg 3B3220, text F2B01E; off = transparent)
    Bdr_FlSelBar Border IV bg 33291A, 2px F2B01E, pad (6,4)  Collapsed unless >=1 selected
     Row_FlSelBar HorizontalBox gap 6 vcentered
      Txt_FlSelCount IV 12 bold F2B01E "3 selected"
      Txt_FlSetRoleLbl 11 "Set role for selected:"
      Sel_FlRole IV bg D9D9D9 min 90 > Txt_SelFlRole IV 11 000000 + "v"
      Btn_FlApply IV gold pad(10,3) > Txt_FlApply IV "Apply"
      Btn_FlNewGroup IV pad(10,3) > Txt_FlNewGroup IV "New group from selection"
    Row_FlColHead HorizontalBox  (10 AAAAAA; same widths as row): "" 18, "" 44, "Ship" fill1.1, "Role" 66, "Doing now" fill1.7, "Hull" 78, "Cargo" 78
    Row_FlListScroll ScrollBox IV (fill 1)
     Dyn_FlList DynamicEntryBox IV (vertical, spacing 2) of WBP_FleetShipRow
    Pnl_FlEmpty (state tile, section 6)
    Bdr_FlActionBar Border bg 262626 border 111111 pad (8,4)
     Row_FlActionBar HorizontalBox gap 6
      Btn_FlSelect IV (bg 333333, pad 14,4) > Img i_sel + Txt_FlSelect IV 12 "Select ship"
      Btn_FlRecall IV > i_call + Txt_FlRecall IV "Recall to me"
      Btn_FlHold2 IV > i_idle + Txt_FlHold2 IV "Hold"
      Btn_FlDockBtn IV > i_home + Txt_FlDockBtn IV "Return to dock"
      Btn_FlSend IV > i_pin + Txt_FlSend IV "Send ship"
      Spacer fill 1
      Btn_FlUnselectAll IV > Txt_FlUnselectAll IV 11 "Unselect all"
   Pnl_FlEscort VerticalBox IV (section 3)
```

### 2.3 Dynamic row WBPs
**WBP_FleetShipRow** (root `Fr_Root` VerticalBox IV; decision: the group header is the optional first child, ONE dynamic list). Row cells px/fill: check 18, holo 44, name fill 1.1, role chip 66, doing-now fill 1.7, hull 78, cargo 78; gap 8, pad 6, min height 30; bg 2B2B2B, selected 33291A + border F2B01E.
```
Fr_Root VerticalBox IV
 Fg_Root HorizontalBox IV (group header, Collapsed; Lua shows on first ship of a group)
  Btn_FgHdr Button IV bg 4A4A4A pad(8,2) fill 1  (click = fold)
   Row_FgHdrRow HorizontalBox gap 6: Img_FgArrow IV 12x12 (i_arrow, Lua rotates 0/-90) + Txt_FgName IV 12 bold E6E6E6 "Miners (3)" + Txt_FgSummary IV 10 BBBBBB fill 1 right
 Fr_RowFrame Border IV bg 2B2B2B  (Lua tints selected/lost/fleeing)
  Bdr_FrSel Border IV (transparent -> F2B01E 1px)
   Btn_FrRow Button IV transparent NoDraw (full row click)
    Row_FrRow HorizontalBox gap 8 vcentered
     Chk_FrSel CheckBox IV 13x13 [18]
     Img_FrHolo Image IV 44x28 [44]
     Txt_FrName TextBlock IV 12 fill 1.1
     Bdr_FrChip Border IV role colour pad(6,0) h16 [66] > Row: Img_FrRoleIcon IV 12x12 (tint 111111) + Txt_FrRole IV 10 bold 111111
     Row_FrDoing HorizontalBox fill 1.7 gap 4: Img_FrState IV 12x12 + Txt_FrDoing IV 11 (state colour)
     Row_FrHull HorizontalBox [78] gap 4: PB_FrHull ProgressBar IV h6 fill 1 + Txt_FrHullPct IV 10 BBBBBB w26
     Row_FrCargo HorizontalBox [78] gap 4: PB_FrCargo IV h6 + Txt_FrCargoPct IV 10 w26; Txt_FrNoCargo IV 10 AAAAAA "no cargo" (Collapsed)
     Btn_FrRemove Button IV Collapsed (i_x) > Txt_FrRemove IV 10 "Remove from list"
     Btn_FrGoto Button IV Collapsed (i_pin) > Txt_FrGoto IV 10 "Go to last position"
 Txt_FrLostHint TextBlock IV 10 AAAAAA Collapsed
```
**WBP_FleetEscortRow** (Assigned + Free lists). Cells: holo 44, name fill 1.1, chip 66, doing fill 1.7, hull 78, distance 70, pin 28, action 28; gap 8.
```
Fe_Root Border IV bg 2B2B2B
 Row_FeRow HorizontalBox gap 8 vcentered min h30
  Img_FeHolo Image IV 44x28
  Txt_FeName IV 12 fill 1.1
  Bdr_FeChip Border IV > Img_FeRoleIcon IV + Txt_FeRole IV 10 [66]
  Row_FeDoing: Img_FeState IV + Txt_FeDoing IV 11 fill 1.7
  Row_FeHull: PB_FeHull IV h6 + Txt_FeHullPct IV 10 [78]
  Txt_FeDist IV 10 AAAAAA [70]
  Btn_FePin IV 28x24 (i_pin)
  Btn_FeAction IV 28x24 > Img_FeActionIcon IV 12x12 (Lua sets: i_x + bg 9C2222 on Assigned, i_plus + bg C38800 on Free)
```
**WBP_FleetTargetRow**: `Ft_Root` Border 2B2B2B > Row gap 8: `Img_FtHolo` IV 44x28 + `Txt_FtName` IV 12 fill 1 + `Btn_FtPick` IV 28x24 (i_sel).

**WBP_FleetGroupHeader**: not separate (= `Fg_Root` inside WBP_FleetShipRow). Folding = Lua collapses entries of ships in that group.

### 2.4 Right column Pnl_FleetStatus
```
Pnl_FleetStatus VerticalBox IV Collapsed gap 6
 Bx_FlStatus "Status"
  Img_FlStatHolo Image IV 150x100 centered
  Row: Txt_FlStatName IV 13 bold fill 1 + Bdr_FlStatChip Border IV > Img_FlStatRoleIcon IV + Txt_FlStatRole IV 10 bold
  Row: Txt_FlStatActionLbl 10 "Action" + Img_FlStatState IV 12x12 + Txt_FlStatAction IV 11
  Row: Txt_FlStatNextLbl "Next" + Txt_FlStatNext IV 11
  Row: Txt_FlStatSpeedLbl "Speed" + Txt_FlStatSpeed IV 11
  Row_FlStatMine IV Collapsed (Mine role only): Txt_FlStatMineLbl "Mining" + Img_FlStatOre IV 24x24 + Txt_FlStatMine IV 11
  Row_FlStatCargo IV: Txt_FlStatCargoLbl 10 + Txt_FlStatCargoPct IV 10 ; PB_FlStatCargo IV h6 42DA41
  Txt_FlStatHullLbl + Txt_FlStatHullPct IV ; PB_FlStatHull IV h6 6AA8FF
  Txt_FlStatShieldLbl + Txt_FlStatShieldPct IV ; PB_FlStatShield IV h6 6AA8FF
  Btn_FlSetRole IV gold big > Txt_FlSetRole IV 12 "Set role"
  Btn_FlRecallS IV big > Img i_call + Txt_FlRecallS IV 12 "Recall to me"
  Btn_FlHoldS IV big > Img i_idle + Txt_FlHoldS IV 12 "Hold position"
 Bx_FlSelected IV Collapsed (>=2 selected)
  Txt_FlSelTitle IV 13 "Selected (3)"
  Row_FlSelHolos HorizontalBox gap 3: Img_FlSelHolo_1..5 IV 40x26 (Collapsed)
  Row: Txt_FlSelRoleLbl 10 "Role" + Txt_FlSelRole IV 11 bold
  Txt_FlSelNote IV 10 AAAAAA "Mixed roles: only common actions shown."
```

## 3. ESCORT PAGE (Pnl_FlEscort, switcher index 1)
```
Pnl_FlEscort VerticalBox IV
 Row_FlEscStrip HorizontalBox IV  class strip; cells Btn_FlECls<K>, Img_FlECls<K>, Txt_FlEClsName<K>, Txt_FlEClsCnt<K>, Bdr_FlEClsSel<K> (all IV; Fighter selected default)
 Row_FlEscBody HorizontalBox gap 16
  Row_FlEscLeft VerticalBox fill 0.2 gap 6
   Bx_FlFollowing "Following"
    Bdr_FlFollowCard Border IV 2B2B2B, left stripe 3px EE66BB
     Row: Img_FlFollowHolo IV 44x28 + VBox: Txt_FlFollowName IV 13 bold "You (Captain)" + Txt_FlFollowCount IV 10 AAAAAA
    Txt_FlFollowWhoLbl 10 "Follow who"
    Sel_FlFollow IV bg D9D9D9 > Txt_SelFlFollow IV 11 000000
    Txt_FlDistLbl 10 "Keep distance"
    Sel_FlDist IV bg D9D9D9 > Txt_SelFlDist IV 11 000000 "Close, 200 m"
    Btn_FlStopAll IV bg 9C2222 > Txt_FlStopAll IV 11 "Stop escorting all"
   Bx_FlOtherTargets "Other targets": Dyn_FlTargets DynamicEntryBox IV of WBP_FleetTargetRow
  Row_FlEscRight VerticalBox fill 0.8 gap 6
   Bx_FlAssigned "Escorting you - N ships" (Txt_FlAssignedTitle IV 13): Row_FlAsgHead (10 AAAAAA headers) + Dyn_FlAssigned IV of WBP_FleetEscortRow
   Bx_FlFree "Add to escort - free ships": class strip Btn_FlFCls<K> (+Img_FlFCls<K>, Txt_FlFClsName<K>, Txt_FlFClsCnt<K>, Bdr_FlFClsSel<K>) + Row_FlFreeHead + Dyn_FlFree IV of WBP_FleetEscortRow
 Bdr_FlEscActionBar (bg 262626): Btn_FlESelect, Btn_FlEDock, Btn_FlERecall, Btn_FlEHold, Btn_FlESend, spacer, Btn_FlEUnselect "Unselect target"; each Button IV + Txt_<same name> IV
```

## 4. ROLE PAGE (Pnl_PageRole): 4 cards, one data-driven form per role
`Row_RoleCards` HorizontalBox IV, 4 columns fill 1 gap 10. Lua collapses non-matching cards when a ship is selected.
Card style: outline 1C1C1C pad 2; header gold C38800, title 12 111111; 2px line F2B01E; body 2B2B2B pad 5 gap 3. Field row: label 11 E6E6E6 fill 1 + value widget (Sel_ Button bg D9D9D9 min w70 text 000000 11 + "v"; EditableTextBox black text 11 bg D9D9D9 w70; CheckBox). Pad 2 vertical.
Card header (every card, C = Defend|Haul|Attack|Escort), root `Pnl_Rl<C>` VerticalBox IV, title `Txt_Rl<C>Title` IV 12 ("Role: Defend", "Role: Haul", "Role: Attack", "Role: Escort (see screen 2)"):
```
Row HorizontalBox gap 8: Img_Rl<C>Holo IV 70x47 ; VBox: Bdr_Rl<C>Chip Border IV > Img_Rl<C>RoleIcon IV + Txt_Rl<C>Role IV 10 bold ; Row: Img_Rl<C>State IV 12x12 + Txt_Rl<C>StateTxt IV 11 8FE08E
Txt_Rl<C>DoingLbl 10 AAAAAA "Doing now" ; Txt_Rl<C>Doing IV 11 wrap
(field rows)
Row_Rl<C>Btns HorizontalBox gap 5 equal fill: Btn_Rl<C>Recall (i_call, Txt_Rl<C>Recall "Recall to me"), Btn_Rl<C>Hold (i_idle, Txt_Rl<C>Hold "Hold"), Btn_Rl<C>Default (Txt_Rl<C>Default "Set as default")  all IV
```
Fields (label = static `Txt_Rl<C><F>Lbl`; value widget IV; Sel_ has `Txt_Sel<name>` IV):
- Defend: Sel_RlDAnchor "Anchor"; Edt_RlDRadius "Engage radius R (km)" 15; Edt_RlDGiveUp "Give up past (km)" 25; Edt_RlDFlee "Flee at hull below (%)" 30; Sel_RlDPick "Pick target" Closest first; Chk_RlDReturn "Return to anchor when clear".
- Haul: Sel_RlHFrom "Load at"; Sel_RlHTo "Unload at"; Sel_RlHItem "Item" (+ Img_RlHItem 24x24 IV); Edt_RlHAmount "Amount (0 = all)"; Chk_RlHRepeat "Repeat"; Btn_RlHAddRow > Txt_RlHAddRow "Add item row".
- Attack: Sel_RlASearch "Search area"; Sel_RlAPick "Pick target"; Edt_RlAFlee "Flee at hull below (%)" 30; Sel_RlAIdle "When no enemies".
- Escort: Sel_RlEFollow "Follow"; Sel_RlESlot "Slot"; Edt_RlEDist "Distance (m)" 300; Chk_RlEWarp "Warp with target".
Left column + right Status box on Role tab = Pnl_FleetLeft + Pnl_FleetStatus.

## 5. ROLE PICKER (existing Pnl_Modal, 720x640, title "Choose role for N selected")
`selSet("role", opts, currentId)`, 6 options Mine/Haul/Escort/Defend/Attack/None; Pnl_ModalColHead heads: "", "Role", "What it does", "Then". Row via existing rowRender: icon Img_ModalIcon_<n> (role colour), C1 role word (role colour text), C2 description, C3 "Asks: ..." (hint). Selected row = existing border F2B01E. Done/Cancel = existing multi-mode Btn_ModalDone.
Rows: Mine "mine ore, unload, refuel" / "Asks: ore type"; Haul "carry items between two docks" / "Asks: from, to, item"; Escort "follow you or a ship" / "Asks: who to follow"; Defend "guard a place, engage inside radius" / "Asks: where, radius"; Attack "hunt enemies in the sector" / "Asks: nothing"; None "release the ship (stops it)" / "Asks: nothing".
Optional later: `WBP_FleetRoleRow` (Btn_FmRow, Img_FmIcon, Bdr_FmChip, Txt_FmRole, Txt_FmDesc, Txt_FmAsks, Bdr_FmSel) for a real chip.

## 6. STATE TILES
```
Pnl_FlEmpty VerticalBox IV Collapsed (replaces list in Pnl_FlOverview)
 Bx_FlEmpty "Empty fleet"
  Img_FlEmptyHolo IV 90x60 alpha .25 in 1B4B5E border
  Txt_FlEmptyTitle IV 13 bold "No ships yet"
  Txt_FlEmptyHint IV 10 AAAAAA "Build a ship at a station and it appears here." wrap
  Btn_FlOpenShip IV gold big > Txt_FlOpenShip IV 12 "Open Ship tab"
```
- Lost = WBP_FleetShipRow variant: holo alpha .35, name strike AAAAAA, state s_lost + "Lost: destroyed 2 min ago" FF7777, chip None, border AA3333, Btn_FrRemove + Btn_FrGoto + Txt_FrLostHint shown.
- Fleeing = border FFCC33, holo alpha pulse (Lua tween, optional), s_flee + "Fleeing, hull low" FFCC33, hull bar FFCC33.
- Mixed = Bx_FlSelected (2.4) with "Mixed (Mine, Haul, Defend)"; Lua collapses Row_FlStatCargo and role cards.

## 7. Widget -> stub function
| widget | stub |
|---|---|
| Btn_FlApply (+ Sel_FlRole value) | set_role |
| Sel_FlRole, Btn_FlSetRole (open picker), Sel_Rl* field selects | pick_role (arg = field key); picker Done then set_role |
| Btn_FlRecallL, Btn_FlRecall, Btn_FlRecallS, Btn_FlERecall, Btn_Rl<C>Recall | recall |
| Btn_FlHold, Btn_FlHold2, Btn_FlHoldS, Btn_FlEHold, Btn_Rl<C>Hold | hold |
| Btn_FlEmergencyStop | emergency_stop |
| Btn_FlStart | start |
| Btn_FlNewGroup | new_group |
| Btn_FlDock, Btn_FlDockBtn, Btn_FlEDock | dock |
| Btn_FlUndock | undock |
| Btn_FlRefuel | refuel |
| Btn_FlSelect, Btn_FlESelect, Btn_FrRow, Chk_FrSel, Btn_FePin, Btn_FlSend, Btn_FlESend, Btn_FrGoto | select_ship |
| Btn_FlUnselectAll, Btn_FlEUnselect | unselect_all |
| Btn_FlCls<K>, Btn_FlECls<K>, Btn_FlFCls<K>, Sel_FlGroup | set_filter |
| Btn_FgHdr | toggle_group |
| Btn_FeAction (Free list, +) | escort_add |
| Btn_FeAction (Assigned list, x), Btn_FlStopAll | escort_remove |
| Sel_FlFollow, Sel_FlDist, Btn_FtPick | escort_set |
| Btn_RlEscortDefault, Sel_RlESlot, Edt_RlEDist, Chk_RlEWarp, Sel_RlEFollow | escort_set |
| Btn_RlDefendDefault, Edt_RlDRadius, Edt_RlDGiveUp, Edt_RlDFlee, Chk_RlDReturn, Sel_RlDAnchor, Sel_RlDPick | defend_set |
| Btn_RlHaulDefault, Btn_RlHAddRow, Edt_RlHAmount, Chk_RlHRepeat, Sel_RlHFrom/To/Item | haul_set |
| Btn_RlAttackDefault, Edt_RlAFlee, Sel_RlASearch/Pick/Idle | attack_set |
| Btn_FrRemove | set_role (id, "None") |
| Btn_TabFleet, Btn_TabRole, Btn_FlSubOverview, Btn_FlSubEscort, Btn_FlOpenShip | none (navigation only, no stub) |
Stubs log one line "FLEET STUB <name> <args>" and do nothing else.

## 8. Not buildable / replaced
Hazard stripe = tile image; hologram scan = static image + tint; no gradient/blur/clip-path; pulse = Lua alpha tween; fold arrow = rotated Image or "v"/">" text; role chip = Border + Image + Text; sub-tab underline = 2px Border.

## 9. Assumptions
- One WBP_FleetEscortRow for both escort lists; WBP_FleetTargetRow for Other targets.
- Group header inside WBP_FleetShipRow (one Dyn list).
- Class strip = 3 copies with prefixes Btn_FlCls / Btn_FlECls / Btn_FlFCls (generated by one factory, section 10).
- Role cards: Lua collapses non-matching ones when a ship is selected.

## 10. Mapping to shared generic components (planner's uicomp.py / ui_core.lua)
Python factories (uicomp.py): each factory takes (wbp, name, parent, ...) and builds the names above by prefix + suffix; names stay as listed in 2-6, so the factory only generates them.

| factory | use in this spec |
|---|---|
| `box(title)` | Bx_FlCtl, Bx_FlControl, Bx_FlActions, Bx_FlMain, Bx_FlStatus, Bx_FlSelected, Bx_FlFollowing, Bx_FlOtherTargets, Bx_FlAssigned, Bx_FlFree, Bx_FlEmpty, role card frame (variant body 2B2B2B) |
| `label_field_row(label, kind=sel/edit/check)` | Role page fields (all Sel_Rl*/Edt_Rl*/Chk_Rl*), Row_FlGroup, Follow/Distance fields, Row_FlSetRole in selection bar |
| `chip(role)` | Bdr_FrChip, Bdr_FeChip, Bdr_FlStatChip, Bdr_Rl<C>Chip (Border + Img + Txt, role colour table) |
| `bar(kind)` | PB_FrHull, PB_FrCargo, PB_FeHull, PB_FlStat*, with pct Txt (Row_FrHull/Cargo, Row_FeHull) |
| `icon_button(icon, size)` | Btn_FePin, Btn_FeAction, Btn_FtPick, Btn_FrRemove, Btn_FrGoto, class strip cells, action-bar icons |
| `tab(text)` | Btn_TabFleet, Btn_TabRole, Btn_FlSubOverview, Btn_FlSubEscort |
| `group_header()` | Fg_Root in WBP_FleetShipRow (Btn_FgHdr, Img_FgArrow, Txt_FgName, Txt_FgSummary) |
| `list_row(wbp_name, cells)` | WBP_FleetShipRow, WBP_FleetEscortRow, WBP_FleetTargetRow shells + `dyn_list` for Dyn_FlList, Dyn_FlAssigned, Dyn_FlFree, Dyn_FlTargets |
| `ship_row()` | the Fr_RowFrame/Row_FrRow cell set; reused by Fe_ rows with a different cell list (no checkbox, + distance, pin, action) |
| `action_bar(buttons)` | Bdr_FlActionBar, Bdr_FlEscActionBar (button list per page) |
| `selection_bar()` | Bdr_FlSelBar (count, set-role select, Apply, New group) |
Class strip = `icon_button` cells x6 built by a loop (prefix param), not a separate factory.

Lua helpers (ui_core.lua):
| helper | use |
|---|---|
| `list{anchor=Dyn_*, rows=, bind=, paint=}` | Dyn_FlList (ship rows + Fg group header), Dyn_FlAssigned, Dyn_FlFree, Dyn_FlTargets |
| `chip(widgets, role)` | sets Bdr_*Chip colour + Img role icon + Txt role word |
| `bar(pb, txt, pct, kind)` | sets PB_* percent/colour (hull low = FFCC33) + pct text |
| `fold(group_key)` | Btn_FgHdr -> toggle_group, rotates Img_FgArrow, collapses group entries |
| `form_row(card, field)` | Role page: binds each field row (Sel/Edt/Chk) to its role's setter (defend_set, haul_set, attack_set, escort_set) |
| `bind_button(btn, stub, argfn)` | every Btn_* in section 7 -> named fleet stub; nav buttons use plain handlers |

Role page = ONE data-driven form: Lua holds a FIELDS table per role (key, label, kind, default, stub); python builds the four cards from the same table (same list drives `label_field_row` calls), so adding a field = one table entry in both. Field lists (source of truth for section 4): defend{anchor sel, radius edit, giveup edit, flee edit, pick sel, return chk}, haul{from sel, to sel, item sel, amount edit, repeat chk, addrow btn}, attack{search sel, pick sel, flee edit, idle sel}, escort{follow sel, slot sel, dist edit, warp chk}.
