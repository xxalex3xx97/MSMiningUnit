# WBP_MiningUnit spec

Asset: `Content/MiningUnitUI/WBP_MiningUnit.uasset`, path `/Game/MiningUnitUI/WBP_MiningUnit`, class `WBP_MiningUnit_C`.
Parent class: **UserWidget** (plain, no game C++ parent).
Every named widget below: **Is Variable = checked** (Lua reads them with GetWidgetFromName).
Art: all textures live in our own package /Game/MiningUnitUI/Art/ (T_UI_Header_L, T_UI_Header_Bar, T_UI_Header_R = copies of the game's UI textures; T_Item_<name> = copies of the game's item icons at 64x64). Nothing may reference /Game/Materials or any other game package (checked by tests/test_asset_refs.py). Sources art/textures/*.png, art/items/*.png imported by scripts/make_assets.py.

## Look (hand-matched, from mods/SandboxMenu/Scripts/main.lua colors)
- Native-style windows (sampled from the game's Ship Inventory): gold `#C38800` header tab + line, body `#3A3A3A`, inset `#2B2B2B`, outline `#1C1C1C`, cargo bar green `#42DA41`
- Tab off `#1E1E1E`, alpha 0.95; tab on = gold `#C38800`
- Buttons `#4A4A4A`; Close/danger `#9C2222`; emergency stop strong red `#C81414` (armed `#FF3B3B`); pending start/stop amber `#E69A00` (Lua colours)
- Emergency stop (art, default): hazard frame = our own 32x32 diagonal stripe tile `/Game/MiningUnitUI/T_HazardStripes` (black #151515 / yellow #E8B800, made by scripts\make_assets.py), padding 9; yellow rim padding 3; button red rounded box, label white bold 16. Lua does NOT repaint this button or its label (paintEstop removed). `--flat` = plain red square button, no frame.
- Text: default UMG font, size 13-14 (main.lua smallFont), title size 18
- Size and centering: WBP Panel canvas slot = Anchors Min 0.15/0.15, Max 0.85/0.85, offsets 0 (centered 70% of screen). Lua does not size Panel's canvas slot.

## Hierarchy (three side-by-side windows, Fill weights 0.2 / 0.55 / 0.25, 16px gaps; each = gold header tab + gold line, outline > body > darker inset; layout-only widgets are named Row_*/Lbl_* and are not variables)
```
WBP_MiningUnit (UserWidget)
└─ Root            CanvasPanel           Is Variable
   ├─ Panel        Border                Is Variable   (transparent, anchors 0.15-0.85 both axes, offsets 0 = centered 70%)
   │  └─ Row_Cols  HorizontalBox
   │     ├─ LEFT column (VerticalBox Row_LeftCol)
   │     │  ├─ Pnl_Fleet  VerticalBox  Is Variable  (top box; Lua collapses it in Mining Unit mode)
   │     │  │  └─ window "Fleet Control" (Lbl_TitleFleet) > Row_FleetBody > Lbl_RemoteControl "Remote Control" + Row_RemoteRow > Sel_Remote (select field, Txt_SelRemote shows the ship) + Btn_RemoteAll (Button, Is Variable) with Txt_RemoteAll "X": right of Sel_Remote in Row_RemoteRow. Click = All ships mode.
   │     │  ├─ window "Control" (top to bottom)
   │     │  │  ├─ Row_EstopFrame  Border  hazard stripes (T_HazardStripes, art only)
   │     │  │  │  └─ Row_EstopRim  Border  yellow rim #E8B800 (art only)
   │     │  │  │     └─ Btn_EmergencyStop  Button  Is Variable  red rounded #C81414 (hover #E01818, pressed #7A0E0E)  └─ Txt_EmergencyStop TextBlock Is Variable "EMERGENCY STOP" (flat: "Emergency stop (all ships)")
   │     │  │  ├─ Btn_LoopStart  Button  Is Variable  └─ "Start loop"
   │     │  │  └─ Btn_LoopStop   Button  Is Variable  └─ "Stop loop"
   │     │  └─ window "Actions" (own gold tab, same art; full-width flat buttons, 6px apart)
   │     │     ├─ Btn_Dock    Button  Is Variable  └─ Txt_Dock    TextBlock Is Variable "Dock"
   │     │     ├─ Btn_Undock  Button  Is Variable  └─ Txt_Undock  TextBlock Is Variable "Undock"
   │     │     ├─ Btn_Refuel  Button  Is Variable  └─ Txt_Refuel  TextBlock Is Variable "Refuel"
   │     │     └─ Btn_Unload  Button  Is Variable  └─ Txt_Unload  TextBlock Is Variable "Unload"
   │     ├─ CENTER (header title = Txt_Title, min width 260)
   │     │  ├─ Txt_Title    TextBlock      Is Variable    "Mining Unit" (Lua sets "Mining Unit - <ship>")
   │     │  ├─ Btn_Close     Button         Is Variable    small gold X square at header right, hover/pressed tint  └─ Txt_Close TextBlock "X"
   │     │  ├─ TabBar        HorizontalBox  Is Variable
   │     │  │  ├─ Btn_TabShip       Button  Is Variable   └─ Txt_TabShip      TextBlock "Ship" (active/gold first)
   │     │  │  ├─ Btn_TabItems      Button  Is Variable   └─ Txt_TabItems     TextBlock "Items"
   │     │  │  ├─ Btn_TabOperation  Button  Is Variable   └─ Txt_TabOperation TextBlock "Operation"
   │     │  │  └─ Btn_TabLog        Button  Is Variable   └─ Txt_TabLog       TextBlock "Log"
   │     │  ├─ Pnl_AllWarn  Border  Is Variable  starts Collapsed  > Row_AllWarnBody > Txt_AllWarn: gold/red banner, visible only in All ships mode.
   │     │  ├─ PageSwitcher  WidgetSwitcher Is Variable   (page index 0..3 = Ship, Items, Operation, Log)
   │     │     ├─ Page_Ship       VerticalBox  Is Variable
   │     │     ├─ Page_Items      VerticalBox  Is Variable
   │     │     ├─ Page_Operation  VerticalBox  Is Variable
   │     │     └─ Pnl_PageLog     VerticalBox  Is Variable  (index 3)
   │     └─ RIGHT "Status" (placeholders only; Lua feeds later)
   │        ├─ row 1: Txt_StatusAction  TextBlock  Is Variable  "Idle" (Mining/Docking/Refueling/Unloading/Idle), left
   │        │         Txt_Eta           TextBlock  Is Variable  "ETA: --" right-aligned, same row (Lua shows only while unloading/refueling, else Collapsed)
   │        ├─ row 2: Txt_NextState     TextBlock  Is Variable  "Next: -"
   │        ├─ row 3: Lbl_Cargo "Cargo" + Txt_CargoPct TextBlock Is Variable "0%", then PB_Cargo ProgressBar Is Variable  cargo fill, green
   │        ├─ row 4: Txt_Speed         TextBlock  Is Variable  "Speed: 0 m/s"
   │        └─ footer: Txt_Status       TextBlock  Is Variable  footer/message line (Lua sets)
   └─ Pnl_Modal    Overlay               Is Variable   (2nd child of Root, Collapsed; see Modal select)
```
Removed (do not recreate): the Loop tab and page with its header/status/rows texts, the Start all and Stop all buttons, the old Body box, the header ship dropdown and the old select2 panels.
Keep Lua-set text blocks empty in the WBP; Lua fills them. Field rows below use one Txt_<Field> label TextBlock + one value widget.

## Per page contents (from the old Mining tab, main.lua keys)

Ship select = key Remote in the Fleet Control box (Sel_Remote, Txt_SelRemote). Lua fills from ships.txt (has_bench=1, sorted by name) through the modal. No bench ship = Txt_SelRemote "No ship".

Page_Ship (select fields, keys: dock_id, refuel_dock_id; dock_name_filter key is not touched):
- Single Unload dock row removed. Txt_ShipWarn (yellow F2D21E, Collapsed; Lua text). Per ore <ok> (7 ore keys): card Pnl_UDockCard_<ok> (Collapsed), rows m=1..5: Pnl_UDockRow_<ok>_<m> (Collapsed) = select field Sel_UDock_<ok>_<m>, Txt_SelUDock_<ok>_<m> + trash Btn_UDockDel_<ok>_<m>; Btn_UDockAdd_<ok>. Refuel list: Pnl_RDockCard (Collapsed), Pnl_RDockRow_<m> = Sel_RDock_<m>, Txt_SelRDock_<m> + Btn_RDockDel_<m>; Btn_RDockAdd.
- Operation tab "Warp management" card (Lbl_HdrWarp): Txt_OpBeltLabel "Sector Selector" / select field "Belt": Sel_Belt (Button), Txt_SelBelt; Btn_BeltPinned "Use pinned sector"; Btn_BeltWarp "Warp to belt"
- Select field = Sel_<key> (Button, shows the current choice in Txt_Sel<key>). Click opens the shared modal Pnl_Modal (no panel or options per field). Click a row = select + close + save to MiningBench.ini.
- Edt_* style: black text, font size 11, light grey bg kept (WidgetStyle).

Page_Items (key: items):
- Pool of 20 rows, r = 1..20: Pnl_ItemRow_<r> HorizontalBox (collapsed; Lua shows used rows only)
  - Img_ItemIcon_<r> Image Hidden (32x32; Lua sets brush from Item.Icon)
  - select field key Item<r>: Sel_Item<r> (Button), Txt_SelItem<r>. Click opens the shared modal (option icon + text; only minable items listed)
  - Edt_ItemAmount_<r> EditableTextBox
  - Btn_ItemUp_<r> / Txt_ItemUp_<r>, Btn_ItemDown_<r> / Txt_ItemDown_<r>, Btn_ItemDel_<r> / Txt_ItemDel_<r>. Art (default): our icons /Game/MiningUnitUI/T_IconUp, T_IconDown, T_IconTrash (64x64 white glyph, made by scripts\make_icons.py + make_assets.py). Up/Down = 20x20 transparent buttons, icon as Normal/Hovered/Pressed brush tinted #B9B9D2 / #E4E4F6 / #8A8AA6, stacked in one column (Row_ItemUD_<r>) vertically centred. Del = 40x40 button, dark rounded box #2E2E2E (outline #1A1A1A, radius 6), trash icon 24x24 tinted #B9B9D2 (Row_ItemDelOverlay_<r> > Row_ItemDelIcon_<r> + Txt_ItemDel_<r>). Txt_Item* are empty and Collapsed in art. --flat keeps text labels "^" "v" "X".
- Btn_ItemAdd Button "Add resource" / Txt_ItemAdd, wide, below the list (appends a row)
- The list scrolls with the page. Rows are a fixed pool of 20: Lua never creates widgets.

Page_Operation (keys: radius, mining_distance, max_radius, idle_seconds, wait_seconds, dock_hold_limit, enabled, repeat, fuel_<k>_threshold, fuel_<k>_amount, 7 ore unload toggles):
Save: per-ship keys (radius, mining_distance, idle_seconds, dock_hold_limit, enabled, repeat) -> [ship.<id>]; global-only keys (max_radius, wait_seconds, fuel_<k>_on/_threshold/_amount, ore_<k>) -> top level (C++ ignores them per ship). Text boxes saved on change, checkboxes polled. Applied at next loop start. Invalid input shows "error: ..." in Txt_Status and is not saved.
Layout (names below unchanged; only Row_*/Lbl_* layout widgets are added, none are variables): bordered cards (Row_CardOutline<K> > Row_CardBody<K> > Row_CardInner<K> = gold title Lbl_Hdr<K> + 2px gold Row_CardLine<K> + rows). Row_OpTop = two columns: left card Mining (Radius, Mining distance, Max radius), right cards Timing (Idle, Wait, Dock time limit) and Run (Enabled, Repeat). Then full-width card Fuel (3 rows: icon | checkbox | "min" box | "max" box), full-width card Unload ores (4 columns x 2 rows, icon + checkbox per cell), Btn_ApplyGlobal bottom right. Columns 60/40 (fill 1.2 / 0.8). The Mining card stretches to the height of the Timing+Run column (no gap). Number boxes are fixed width 80. Display labels Lbl_Fuel<K>Threshold / Lbl_Fuel<K>Amount read "min" / "max" (widget names and Lua keys fuel_<k>_threshold/_amount unchanged).
- Txt_OpRadiusLabel "Radius" / Edt_Radius
- Txt_OpMiningDistLabel "Mining distance" / Edt_MiningDistance
- Txt_OpMaxRadiusLabel "Max radius (global)" / Edt_MaxRadius
- Warp management card: Txt_OpWarpRangeLabel "Warp range" / Edt_WarpRange (belt mode: a rock farther than this is not flown to, the ship warps instead; empty = 50km; per ship)
- Txt_OpIdleLabel "Idle seconds" / Edt_IdleSeconds
- Txt_OpWaitLabel "Wait seconds" / Edt_WaitSeconds
- Txt_OpDockLimitLabel "Dock time limit (s)" / Edt_DockHoldLimit (max seconds at the dock for unload/refuel before undock + error stop; 0 = no limit; per ship)
- Chk_Enabled "Enabled", Chk_Repeat "Repeat"
- Per fuel (Deuterium, Hydrogen, Warp): Chk_Fuel<K>On, Edt_Fuel<K>Threshold, Edt_Fuel<K>Amount (K = Deuterium|Hydrogen|Warp)
- Per ore (Cobalt, Graphite, Iridium, Nickel, Silica, Titanium, Water Ice): Chk_Unload<K> (7 toggles, K = ore name without space, e.g. WaterIce)
- Per fuel icon: Img_Fuel<K> Image Hidden 24x24, Is Variable (Lua sets brush from Item.Icon; Deuterium=ITEM_EnrichedFuel, Hydrogen=ITEM_HydrogenFuel, Warp=ITEM_WarpFuel).
- Per ore icon: Img_Unload<K> Image Hidden 24x24, Is Variable (K = Cobalt|Graphite|Iridium|Nickel|Silica|Titanium|WaterIce; ids ITEM_CobaltOre, ITEM_Graphite, ITEM_IridiumOre, ITEM_NickelOre, ITEM_Silica, ITEM_TitaniumOre, ITEM_WaterIce).
- Btn_ApplyGlobal "Set as default" (old plain Apply button removed; every value saves instantly).

Pnl_PageLog (Log tab, index 3): Txt_LogHint TextBlock Is Variable "No log yet" (Lua shows only when empty) + ScrollBox Row_LogScroll Is Variable holding Txt_Log_<l> (l = 1..60) TextBlock Is Variable (size 14, AutoWrapText, pad 6/2/12/2, Collapsed; Lua sets "hh:mm:ss text", ship-name prefix in All mode, newest first, shows used lines). Pool LOG_N=60 in gen_paste = LN in wbp_loader.lua.

Status box mining line (single ship only; Lua hides in All mode): Row_StatusMine HorizontalBox Is Variable ( under Txt_NextState) = Txt_StatusMineLbl TextBlock Is Variable "Mining" + Img_StatusOre Image Is Variable (24x24, Hidden; Lua sets brush with the same ore icon helper as Img_Unload<K>) + Txt_StatusMine TextBlock Is Variable "-" (ore display name, "-" when none).

Footer: Txt_Status lives at the bottom of the Status window; Btn_Close is the X in the center header.

## Modal select (one shared widget set)
Pnl_Modal = Overlay, Is Variable, Collapsed, 2nd child of Root (anchors like Panel, Min/Max 0.15-0.85, offsets 0). Opened by any select field, shared by all of them.
- Btn_ModalOutside  Button  Is Variable  full-canvas dim layer, empty; click outside the panel = close.
- centred panel 720x640 (Row_ModalSize > Row_ModalOutline > Row_ModalBorder > Row_ModalBody, layout only):
  - Pnl_ModalHeadP  HorizontalBox  Is Variable  (plain header): Txt_ModalTitle TextBlock Is Variable (title, Lua sets) + Btn_ModalClose Button Is Variable > Txt_ModalClose TextBlock Is Variable "X"
  - Pnl_ModalHeadG  HorizontalBox  Is Variable, Collapsed  (game-style header for ship cards: gold tab + gold bar + X cap): Img_ModalHeadIcon Image Is Variable (ship icon) + Txt_ModalTitleG TextBlock Is Variable (title, Lua sets) + Btn_ModalCloseG Button Is Variable > Txt_ModalCloseG TextBlock Is Variable (empty, collapsed)
  - Edt_ModalSearch  EditableTextBox  Is Variable  (view-only filter, black text)
  - Pnl_ModalColHead  HorizontalBox  Is Variable  (column titles; Lua collapses it when the select has no `heads`) > Txt_ModalH1, Txt_ModalH2, Txt_ModalH3, Txt_ModalH4 TextBlock Is Variable (same widths as the row columns)
  - Row_ModalScroll  ScrollBox  Is Variable (Lua collapses it in card mode) of 40 rows
- Row = Opt_Modal_<n> Button Is Variable (n = 1..40, Collapsed until Lua fills) > Row_ModalCol_<n> (VerticalBox, layout only) > [Row_ModalRow_<n>, Row_ModalOres_<n> (HorizontalBox, Is Variable, Collapsed; Lua shows it only when the option has ore icons) > Img_ModalOre_<n>_<k> (k = 1..6; Image Is Variable, 24x24, Collapsed)]. Row_ModalRow_<n> > Img_ModalIcon_<n> Image Is Variable (Collapsed unless the option has an icon) + Txt_ModalC1_<n>, Txt_ModalC2_<n>, Txt_ModalC3_<n>, Txt_ModalC4_<n> TextBlock Is Variable (up to 4 columns; Lua sets text and visibility)

## How to add a select
1. gen_paste.py: put `select_field("<key>")` where the field goes (e.g. inside a row with a label). Creates Sel_<key> + Txt_Sel<key>.
2. SPEC.md: list Sel_<key> and Txt_Sel<key> (this section and the tree).
3. Lua M.open bind block: `selInit("<key>", bind, function(id) ... end, "Title"[, rowRender[, build]])`. When data is known: `selSet("<key>", opts, currentId)`. opts = {{id=, text=, key= (extra search text), icon= (texture), ...}}. rowRender(opt) returns {cols={max 4 strings}, icon=tex}. Optional heads() returns the column titles (max 4), e.g. selInit(..., rowRender, build, heads). build() = fresh options on every click. Or call `M.modalSelect{title=, options=, id=, onPick=, rowRender=}` directly.
4. Run gen_paste (art and --flat), re-paste the whole tree, cook.

## Notes
- Names above are the contract. Lua uses them exactly. Changing a name = change wbp_loader.lua too.
- Labels on Txt_*Label are static text in the WBP. Value widgets are empty; Lua fills.
- "Cleanup" (danger) button from the old plan: NOT in this spec (not confirmed). Ask before adding.
- Ship cards (second row set, same Pnl_Modal): Row_ModalCardScroll ScrollBox Is Variable, Collapsed, sibling of Row_ModalScroll. Lua switches on option field `style="card"`: card mode shows Pnl_ModalHeadG + Row_ModalCardScroll and collapses Pnl_ModalHeadP, Pnl_ModalColHead, Row_ModalScroll; plain mode is the reverse. Both headers' X close the modal.
- Card = Opt_Card_<c> Button Is Variable (c = 1..20 = CN, Collapsed until Lua fills) > Bdr_CardSel_<c> Border Is Variable (selected tint, transparent when not selected) > Row_CardRow_<c> > Img_CardHolo_<c> Image Is Variable (ship hologram, Lua sets texture) + Row_CardCol_<c> > Row_CardTop_<c> (Txt_CardName_<c>, Txt_CardDist_<c>, Img_CardPin_<c>: TextBlock/TextBlock/Image, all Is Variable) + Row_CardStat_<c> (Img_CardStat_<c> Image, Txt_CardStat_<c> TextBlock, Is Variable) + Row_CardCargo_<c> (Txt_CardCargo_<c> TextBlock, PB_Card_<c> ProgressBar, Is Variable).
- Status icon source: Row_StatSrc (HorizontalBox, layout only, Collapsed, inside Pnl_Modal) holds Img_StatSrc_idle, Img_StatSrc_mining, Img_StatSrc_docking, Img_StatSrc_undocking, Img_StatSrc_refueling, Img_StatSrc_unloading (Image Is Variable). Lua reads Brush.ResourceObject of the one matching the status key and copies it to Img_CardStat_<c>. Status keys: idle, mining, docking, undocking, refueling, unloading.
- Item icon source: Row_StatSrc also holds Img_ItemSrc_<I> (Image, Is Variable; I = the 13 ITEM ids, e.g. Img_ItemSrc_ITEM_CobaltOre). Brush = our texture /Game/MiningUnitUI/Art/T_Item_<name>. Lua reads Brush.ResourceObject once per window open (loadItems) and uses it as the item icon; the game's Item.Icon is a fallback only when the bundled one is missing. A missing icon leaves the Image hidden.

## Fleet list + card lines (Part 1)
- Right column Row_StatusCol (VerticalBox): Status window on top, Pnl_FleetList (VerticalBox Is Variable, Collapsed; window "Ships" > Row_FleetScroll) below. Lua shows it only in All ships mode.
- Pnl_FleetList holds FN=20 compact cards, f = 1..20 (gen_paste FN must equal wbp_loader.lua FN): Pnl_Fleet_<f> (Border, Collapsed) > Img_FleetHolo_<f>, Txt_FleetName_<f>, Txt_FleetDist_<f>, Img_FleetPin_<f>, Img_FleetStat_<f>, Txt_FleetStat_<f>, Txt_FleetMine_<f>, Txt_FleetNext_<f>, Txt_FleetCargo_<f>, PB_Fleet_<f>. PB_Fleet_<f> is the cargo bar. No selection border, no button.
- Modal ship cards also get two lines between status and cargo: Txt_CardMine_<c> ("Mining: ..."), Txt_CardNext_<c> ("Next: ..."). Both Collapsed until Lua sets text. Other Card names unchanged.
- Txt_AmountHint: static TextBlock at the top of the Items page: "0 = unlimited (mine until cargo is full)".

## Ore icon on mining lines + per-ship log cards (2026-10-28)
- Every ship card (Card = modal, Fleet = right column; P in {Card,Fleet}, i = 1..CN / 1..FN) has the mining line as one HorizontalBox Row_<P>MineRow_<i> = Txt_<P>MineLbl_<i> ("Mining:", Collapsed) + Img_<P>Ore_<i> (Image, 24x24, Hidden; same icon source as Img_StatusOre) + Txt_<P>Mine_<i> (ore name only, Collapsed). Txt_<P>Mine_<i> keeps its old name.
- Log tab All ships mode: Pnl_LogAll VerticalBox (Collapsed, sibling of Row_LogScroll inside Pnl_PageLog) > ScrollBox Row_LogAllScroll > Pnl_LogCard_<f> (Border, f = 1..FN, Collapsed) > header row Txt_LogCardName_<f> (size 16) + Txt_LogCardStatus_<f> (size 14, dimmer), then Txt_LogCard_<f>_<r> (r = 1..10, size 13, AutoWrapText). LOG_ROWS=10 in gen_paste = LOG_ROWS in wbp_loader.lua. Lua: All mode shows Pnl_LogAll and hides Row_LogScroll; single mode the reverse.
- Placeholder names (validator): Img_CardOre_<c>, Txt_CardMineLbl_<c> (c = 1..CN); Img_FleetOre_<f>, Txt_FleetMineLbl_<f> (f = 1..FN).

## Multi-select card modal (Part 7, 2026-10-29)
- CN = 40 now (FN stays 20). Modal card list Row_ModalCardScroll interleaves per slot: Row_CardHdr_<c> (HorizontalBox, Is Variable, Collapsed) > Btn_CardHdr_<c> > Txt_CardHdr_<c> (fleet/All header, gold-or-grey bg by Lua) + Btn_CardFold_<c> > Txt_CardFold_<c> (">"/"v", Collapsed), then Opt_Card_<c>.
- Btn_ModalDone > Txt_ModalDone (Done button, Collapsed, below Row_ModalCardScroll; multi mode only).
- Dock card header icons (Image, Hidden, 24x24, Is Variable, Lua sets brush): Img_UDockOre_<ok> (ore icon) in each unload card; Img_RDockFuelDeuterium, Img_RDockFuelHydrogen, Img_RDockFuelWarp in the refuel card. Unload cards are ordered Water Ice, Titanium, Silica, Nickel, Iridium, Graphite, Cobalt.

## Lists (all dynamic, 2026-11-03)
Every variable-length list is a `DynamicEntryBox` `Dyn_<name>` of a small row Widget Blueprint, built headless by `scripts/build_lists.py` (helpers in `scripts/uew.py`: `dyn_list`). No row limits; rows exist only as the data needs.
Row WBPs: `WBP_DockRow` (refuel + 7 unload lists), `WBP_ItemRow` (Items queue), `WBP_LogLine` (Log). Select-style rows share one child-naming convention (see build_lists.py); this file is parsed for widget names, so none are listed here.
Lua side: one function `dynList{key, anchor, box, n, bind, paint}` in `wbp_loader.lua` (`dynDocks` is a thin wrapper). Python-built widgets are not Is Variable, so Lua finds them by walking from a variable anchor. Never delete a widget from Python (cook fails on stale variable GUIDs): collapse a stub instead.
