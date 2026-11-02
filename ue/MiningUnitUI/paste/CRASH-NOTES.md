# Ctrl+V crash in UMG Designer: diagnosis

## Root cause (high confidence for trigger, medium-high for the exact crashing frame)
gen_paste.py emit() writes the `Slots(i)=XSlot'"S_child"'` lines BEFORE the nested
`Begin Object ... Name="S_child"` blocks. The importer (ImportObjectProperties) reads a widget body
line by line, so at `Slots(0)=` the slot sub-object does not exist yet -> every Slots[] entry is NULL.
Real UE export order (UExporter::ExportObjectInner, Runtime/Engine/Private/UnrealExporter.cpp:600-640):
1. sub-objects first (declare pass, then define pass), 2. THEN the object's own properties (Slots(0)=..., bIsVariable...).

## Evidence
- Crash context: Saved\Crashes\UECC-Windows-*\CrashContext.runtime-xml: EXCEPTION_ACCESS_VIOLATION reading 0x38,
  stack UMG x4 -> UMGEditor x6 -> Slate (3 crashes, identical). No symbols, frames unnamed.
- Log Saved\Crashes\UECC-Windows-CBAA...\MiningUnitUI.log lines 1217-1327, right before the crash:
  111 x `LogProperty: Warning: ObjectProperty /Script/UMG.PanelWidget:Slots.Slots: unresolved reference to 'CanvasPanelSlot'"S_Panel"'`
  111 = exactly the number of `Slots(` lines in WBP_MiningUnit_tree.txt (ALL slots unresolved).
  Then `Ambiguous object name S_Panel ... Transient.CanvasPanel_0:S_Panel but could also be ...CanvasPanel_1` (stale
  transient packages from earlier attempts; side effect only).
- Engine path: PasteWidgetsInternal (WidgetBlueprintEditorUtils.cpp:1941) -> ProcessImportedText (:2223) ->
  FCustomizableTextObjectFactory::ProcessBuffer (EditorFactories.cpp): creates all top-level objects first, then imports each
  body in line order. Then ImportWidgetsFromText (:2247) walks panel children. Some UMG code does not null-check slots, e.g.
  UPanelWidget::GetAllChildren (PanelWidget.cpp:60-63) `ChildSlot->Content`; Content is UPanelSlot's 2nd member, so a null slot
  reads a tiny offset (0x38). Exact crashing function not provable without symbols; null Slots[] is the certain defect.

## Differences vs real UE text (ground truth from user's Border copy, plus source)
Real:
```
Begin Object Class=/Script/UMG.Border Name="Border_47" ExportPath="/Script/UMG.Border'/Game/MiningUnitUI/WBP_MiningUnit.WBP_MiningUnit:WidgetTree.Border_47'"
End Object
Begin Object Class=/Script/UMGEditor.WidgetSlotPair Name="WidgetSlotPair_0" ExportPath="..."
   WidgetName="Border_47"
End Object
```
1. Slot blocks must precede `Slots(i)=` (the bug). Real is two-phase: declare stub (`Begin Object Class=X Name="S"` / `End Object`),
   define (`Begin Object Name="S"` + props), then Slots(i). One-phase (Class+Name+props) should also import.
2. Real has ExportPath= attrs: optional for import (parsing only reads Class/Name/Archetype).
3. Real appends a WidgetSlotPair per ROOT-MOST pasted widget (parent null / not in set). Ours emits none. Not a crash cause:
   importer handles empty MissingSlotData (it only preserves canvas LayoutData offsets). Optional to add for the root widget:
   `Begin Object Class=/Script/UMGEditor.WidgetSlotPair Name="WidgetSlotPair_0"` / `   WidgetName="Root"` / `End Object`.
4. Real children are top-level siblings, slots nested in the panel: ours matches that.
Not certain: exact real nested slot text (no panel sample available). Ask user for a Border>TextBlock or VerticalBox>TextBlock
copy (paste into Notepad) to confirm; diff against test B/C below.

## Fix for gen_paste.py emit()
```python
    if slot:
        for c in n.kids:
            lines += ['   Begin Object Class=/Script/UMG.%s Name="S_%s"' % (slot, c.name),
                      "      Parent=%s'\"%s\"'" % (n.cls, n.name),
                      "      Content=%s'\"%s\"'" % (c.cls, c.name),
                      "   End Object"]
        for i, c in enumerate(n.kids):
            lines.append("   Slots(%d)=%s'\"S_%s\"'" % (i, slot, c.name))
```
(then bIsVariable/other props can stay before or after.) If one-phase still fails, switch to declare+define.
Make validate() fail when a Slots( line precedes the nested block it names.
Paste into an EMPTY hierarchy (default CanvasPanel deleted, nothing selected); restart editor after each crash.

## Bisect: paste one by one into a fresh emptied Hierarchy (corrected order)
A single TextBlock (no slots; if this crashes, slots are not the problem):
```
Begin Object Class=/Script/UMG.TextBlock Name="T1"
   bIsVariable=True
   Text=INVTEXT("hello")
End Object
```
B Border + TextBlock:
```
Begin Object Class=/Script/UMG.Border Name="B1"
   Begin Object Class=/Script/UMG.BorderSlot Name="S_T1"
      Parent=Border'"B1"'
      Content=TextBlock'"T1"'
   End Object
   Slots(0)=BorderSlot'"S_T1"'
End Object
Begin Object Class=/Script/UMG.TextBlock Name="T1"
   Text=INVTEXT("in border")
End Object
```
C VerticalBox, 2 children:
```
Begin Object Class=/Script/UMG.VerticalBox Name="V1"
   Begin Object Class=/Script/UMG.VerticalBoxSlot Name="S_Ta"
      Parent=VerticalBox'"V1"'
      Content=TextBlock'"Ta"'
   End Object
   Begin Object Class=/Script/UMG.VerticalBoxSlot Name="S_Tb"
      Parent=VerticalBox'"V1"'
      Content=TextBlock'"Tb"'
   End Object
   Slots(0)=VerticalBoxSlot'"S_Ta"'
   Slots(1)=VerticalBoxSlot'"S_Tb"'
End Object
Begin Object Class=/Script/UMG.TextBlock Name="Ta"
   Text=INVTEXT("a")
End Object
Begin Object Class=/Script/UMG.TextBlock Name="Tb"
   Text=INVTEXT("b")
End Object
```
D Button + TextBlock:
```
Begin Object Class=/Script/UMG.Button Name="Bt1"
   Begin Object Class=/Script/UMG.ButtonSlot Name="S_Tbt"
      Parent=Button'"Bt1"'
      Content=TextBlock'"Tbt"'
   End Object
   Slots(0)=ButtonSlot'"S_Tbt"'
End Object
Begin Object Class=/Script/UMG.TextBlock Name="Tbt"
   Text=INVTEXT("btn")
End Object
```
E WidgetSwitcher + 2 pages:
```
Begin Object Class=/Script/UMG.WidgetSwitcher Name="W1"
   Begin Object Class=/Script/UMG.WidgetSwitcherSlot Name="S_P1"
      Parent=WidgetSwitcher'"W1"'
      Content=VerticalBox'"P1"'
   End Object
   Begin Object Class=/Script/UMG.WidgetSwitcherSlot Name="S_P2"
      Parent=WidgetSwitcher'"W1"'
      Content=VerticalBox'"P2"'
   End Object
   Slots(0)=WidgetSwitcherSlot'"S_P1"'
   Slots(1)=WidgetSwitcherSlot'"S_P2"'
End Object
Begin Object Class=/Script/UMG.VerticalBox Name="P1"
End Object
Begin Object Class=/Script/UMG.VerticalBox Name="P2"
End Object
```
F CheckBox + label (content slot class PanelSlot is the riskiest guess; UPanelSlot may be non-instantiable):
```
Begin Object Class=/Script/UMG.CheckBox Name="C1"
   Begin Object Class=/Script/UMG.PanelSlot Name="S_Lc"
      Parent=CheckBox'"C1"'
      Content=TextBlock'"Lc"'
   End Object
   Slots(0)=PanelSlot'"S_Lc"'
End Object
Begin Object Class=/Script/UMG.TextBlock Name="Lc"
   Text=INVTEXT("chk")
End Object
```
If F misbehaves while B-E pass: hand-build a CheckBox with a TextBlock child, Ctrl+C, paste to Notepad and copy the slot class UE writes.
Best ground truth: hand-make Border>TextBlock, Ctrl+C, Notepad, diff against B.

## UPDATE: gen_paste.py rewritten to the real format (ground truth: user's Border>ListView copy)
- Slot = empty declare block, then define block (`Begin Object Name=".."` with Parent/Content), both inside the panel,
  BEFORE `Slots(i)=`; then bIsVariable/other props.
- Refs use the quoted form `"/Script/UMG.Class'Name'"`; ExportPath= on every widget/slot.
- One WidgetSlotPair (WidgetName="Root") right after the root widget; children are later top-level objects.
- Validator updated: parses the new form, fails if Slots( precedes the slot's define block, requires the Root pair.
- Regenerated paste\WBP_MiningUnit_tree.txt (112 widgets, 111 slots, validation OK).
- CheckBox slot class PanelSlot confirmed (UContentWidget::GetSlotClass returns UPanelSlot).
- First paste check: paste\test_small.txt (Border + TextBlock). Then the full tree. The A-F bisect texts above use the old
  short ref form; prefer regenerating variants from gen_paste.py if the full tree still crashes.
