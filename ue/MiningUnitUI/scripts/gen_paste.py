"""Emit UE 5.7 UMG clipboard text for the full WBP_MiningUnit tree (SPEC.md).

Usage (plain CPython, no UE needed):
  python gen_paste.py          art version  -> paste\\WBP_MiningUnit_tree.txt
  python gen_paste.py --flat   flat colours -> paste\\WBP_MiningUnit_tree_flat.txt
Art = our own copies of the game UI textures (/Game/MiningUnitUI/Art, imported by scripts\\make_assets.py from art\\textures and art\\items).
Validates the output, then exit 1 on any error.

Format follows UE 5.7 source (Editor/UMGEditor/Private/WidgetBlueprintEditorUtils.cpp):
- ExportWidgetsToText: every widget is a top-level Begin Object; slot objects are nested
  inside their panel. UPanelSlot::Parent/Content are written as references.
- Widget::Slot is TextExportTransient, so it is NOT in the text. ImportWidgetsFromText
  rebuilds child->Slot from the panel's Slots[] and roots = widgets with no parent,
  so each slot must carry Parent= and Content=.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "paste", "WBP_MiningUnit_tree.txt")
OUT_FLAT = os.path.join(ROOT, "paste", "WBP_MiningUnit_tree_flat.txt")
SPEC = os.path.join(ROOT, "SPEC.md")

# widget class -> slot class it uses for its children (UContentWidget default = PanelSlot)
SLOT = {"CanvasPanel": "CanvasPanelSlot", "Border": "BorderSlot", "VerticalBox": "VerticalBoxSlot",
        "HorizontalBox": "HorizontalBoxSlot", "WidgetSwitcher": "WidgetSwitcherSlot",
        "Button": "ButtonSlot", "CheckBox": "PanelSlot", "ScrollBox": "ScrollBoxSlot",
        "SizeBox": "SizeBoxSlot", "ScaleBox": "ScaleBoxSlot", "Overlay": "OverlaySlot"}
MAX_KIDS = {"Border": 1, "Button": 1, "CheckBox": 1, "SizeBox": 1, "ScaleBox": 1}
FUELS = ["Deuterium", "Hydrogen", "Warp"]
ORES = ["Cobalt", "Graphite", "Iridium", "Nickel", "Silica", "Titanium", "WaterIce"]


def srgb_to_lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lin(hex_rgb, alpha=1.0):
    r, g, b = (srgb_to_lin(int(hex_rgb[i:i + 2], 16) / 255.0) for i in (0, 2, 4))
    return "(R=%.6f,G=%.6f,B=%.6f,A=%.6f)" % (r, g, b, alpha)


PANEL = lin("2B2B2B", 0.85)
TAB_OFF = lin("1E1E1E", 0.95)
TAB_ON = lin("D49A00")
BTN = lin("4A4A4A")
DANGER = lin("9C2222")
EMERG = lin("C81414")
EMERG_HOT = lin("E01818")    # estop hover (art)
EMERG_DARK = lin("7A0E0E")   # estop pressed (art)


GOLD = lin("F2B01E")
DARK_TXT = lin("1A1A1A")
ACCENT = lin("F2B01E")
BLACK_RAW = "(R=0.000000,G=0.000000,B=0.000000,A=1.000000)"
TXT_SIZE = 11
CMB_BG = lin("D9D9D9")
SEL_N = 40
CN = 40   # ship card pool (must equal wbp_loader.lua CN)
FN = 20   # fleet card pool (must equal wbp_loader.lua FN)
STAT_KEYS = ["idle", "mining", "docking", "undocking", "refueling", "unloading"]
DIM = lin("000000", 0.6)
ROW_N = 20
UDOCK_N = 20  # dock rows per ore card / refuel list (must equal wbp_loader.lua UDN)
UP_TXT, DN_TXT, DEL_TXT = "^", "v", "X"
def _lin3(h): return [srgb_to_lin(int(h[i:i + 2], 16) / 255.0) for i in (0, 2, 4)]
HOVER_T = "(R=%.6f,G=%.6f,B=%.6f,A=1.000000)" % tuple(min(1.0, a / b) for a, b in zip(_lin3("C38800"), _lin3("D9D9D9")))


def pad(l=0, t=0, r=0, b=0):
    return "(Left=%.1f,Top=%.1f,Right=%.1f,Bottom=%.1f)" % (l, t, r, b)


# ---- game art (ART=True, default). --flat sets ART=False in main(). ----
# Textures are referenced BY PATH in our own package (copies of the game's UI textures, see docs\ASSETS.md).
ART = True
ART_DIR = "/Game/MiningUnitUI/Art/"
ART_ASSETS = ["Header_L", "Header_Bar", "Header_R"]
WHITE_RAW = "(R=1.000000,G=1.000000,B=1.000000,A=1.000000)"
GOLD_RAW = "(R=1.000000,G=0.443000,B=0.000000,A=1.000000)"   # game gold, linear (dump)
GREY_RAW = "(R=0.255000,G=0.255000,B=0.255000,A=1.000000)"   # game X icon tint (dump)


def tex_path(name):
    return "%sT_UI_%s.T_UI_%s" % (ART_DIR, name, name)


ART_PATHS = {tex_path(n) for n in ART_ASSETS}

# 13 bundled item icons: T_Item_<Name> in ART_DIR (PNG 64x64 from art\items). Lua reads Brush.ResourceObject.
ITEM_ICON_IDS = ["ITEM_CobaltOre", "ITEM_Graphite", "ITEM_IridiumOre", "ITEM_NickelOre", "ITEM_Silica",
                 "ITEM_TitaniumOre", "ITEM_WaterIce", "ITEM_EnrichedCobaltOre", "ITEM_EnrichedNickelOre",
                 "ITEM_EnrichedTitaniumOre", "ITEM_EnrichedFuel", "ITEM_HydrogenFuel", "ITEM_WarpFuel"]


def item_path(i):
    n = "T_Item_" + i[5:]
    return "%s%s.%s" % (ART_DIR, n, n)


ITEM_PATHS = {item_path(i) for i in ITEM_ICON_IDS}


def item_brush(i):
    if not ART:
        return "(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW
    return ("(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,Margin=(Left=0.000000,Top=0.000000,Right=0.000000,Bottom=0.000000),"
            "TintColor=(SpecifiedColor=%s),ResourceObject=Texture2D'\"%s\"')" % (WHITE_RAW, item_path(i)))


# Real texture sizes (art	extures\sizes.txt): all header textures are 40x40 px.
HDR_H = 40.0
# Box margins = fraction of the 40 px texture, measured from the alpha of the real art (px / 40):
#   Header_L  chamfers: TL 16 px wide / 20 tall, TR 8 / 8, BL 2 / 10, BR 16 / 10
#   Header_R  frame: left 16 px, top 8, right 8, bottom 10
#   Header_R_In/Out  button body starts 7 px in (rounded corners inside 7 px)
M_TAB = (0.40, 0.50, 0.40, 0.25)     # Left, Top, Right, Bottom
M_CAP = (0.40, 0.20, 0.20, 0.25)
M_BTN = 0.18
M_BAR = 0.49
M_LINE = (0.49, 0.175, 0.49, 0.35)  # Header_Bar: opaque band rows 7-25 of 40 -> sits mid-height of the 40px row
TAB_MAX_W = 420.0                   # title tab grows with its text up to this; ellipsis beyond (last resort)


def tex(name, draw="Box", w=40.0, h=40.0, m=M_BAR, tint=WHITE_RAW, tiling=None):
    """FSlateBrush text (UE 5.7 struct export; ResourceObject = UTexture2D by path). m = float or (L,T,R,B)."""
    til = ",Tiling=%s" % tiling if tiling else ""
    l, t, r, b = m if isinstance(m, tuple) else (m, m, m, m)
    return ("(ImageSize=(X=%.6f,Y=%.6f),DrawAs=%s%s,Margin=(Left=%.6f,Top=%.6f,Right=%.6f,Bottom=%.6f),"
            "TintColor=(SpecifiedColor=%s),ResourceObject=Texture2D'\"%s\"')"
            % (w, h, draw, til, l, t, r, b, tint, tex_path(name)))


# Our own hazard-stripe tile (shipped in Content\MiningUnitUI, made by scripts\make_assets.py).
STRIPE_PATH = "/Game/MiningUnitUI/T_HazardStripes.T_HazardStripes"
ICON_PATHS = {"/Game/MiningUnitUI/T_Icon%s.T_Icon%s" % (n, n) for n in ("Up", "Down", "Trash", "Ship", "Pin", "StIdle", "StMining", "StDocking", "StUndocking", "StRefueling", "StUnloading")}
ARR = lin("B9B9D2")
ARR_HOT = lin("E4E4F6")
ARR_DN = lin("8A8AA6")


def icon_brush(n, tint, size=20):
    if not ART:   # flat: no textures at all (status/pin/ship icons blank)
        return "(ImageSize=(X=%d.000000,Y=%d.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % (size, size, tint)
    return ("(ImageSize=(X=%d.000000,Y=%d.000000),DrawAs=Image,Margin=(Left=0.000000,Top=0.000000,Right=0.000000,Bottom=0.000000),"
            "TintColor=(SpecifiedColor=%s),ResourceObject=Texture2D'\"/Game/MiningUnitUI/T_Icon%s.T_Icon%s\"')" % (size, size, tint, n, n))


def box_brush(fill, line, r=6.0):
    return ("(DrawAs=RoundedBox,OutlineSettings=(Color=(SpecifiedColor=%s),CornerRadii=(X=%.1f,Y=%.1f,Z=%.1f,W=%.1f),"
            "RoundingType=FixedRadius,Width=1.0),TintColor=(SpecifiedColor=%s))" % (line, r, r, r, r, fill))


def stripe_brush():
    return ("(ImageSize=(X=32.000000,Y=32.000000),DrawAs=Image,Tiling=Both,"
            "Margin=(Left=0.000000,Top=0.000000,Right=0.000000,Bottom=0.000000),"
            "TintColor=(SpecifiedColor=%s),ResourceObject=Texture2D'\"%s\"')" % (WHITE_RAW, STRIPE_PATH))


def round_brush(color):
    """red rounded-box button style brush (no texture, tinted)."""
    return ("(DrawAs=RoundedBox,OutlineSettings=(CornerRadii=(X=6.0,Y=6.0,Z=6.0,W=6.0),RoundingType=FixedRadius),"
            "TintColor=(SpecifiedColor=%s))" % color)


class N:
    def __init__(self, cls, name, kids=(), var=True, props=(), sp=()):
        self.cls, self.name, self.kids, self.var = cls, name, list(kids), var
        self.props = list(props)
        self.sp = list(sp)  # props of the SLOT this widget occupies in its parent


def S(n, **kw):
    """set slot props on n: pad=(l,t,r,b) fill=value h=Fill|Left|Center|Right v=Fill|Top|Center|Bottom"""
    sp = dict(n.sp)
    if "pad" in kw:
        sp["Padding"] = pad(*kw["pad"])
    if "fill" in kw:
        sp["Size"] = "(Value=%.1f,SizeRule=Fill)" % kw["fill"]
    if "h" in kw:
        sp["HorizontalAlignment"] = "HAlign_" + kw["h"]
    if "v" in kw:
        sp["VerticalAlignment"] = "VAlign_" + kw["v"]
    n.sp = list(sp.items())
    return n


def tb(name, text, size=15, var=True, color=None, minw=None):
    font = "(Size=%d)" % size   # engine default font, Regular weight (no bold anywhere)
    props = [("Font", font), ("Text", 'INVTEXT("%s")' % text)]
    if color:
        props.append(("ColorAndOpacity", "(SpecifiedColor=%s)" % color))
    if minw:
        props.append(("MinDesiredWidth", "%.1f" % minw))
    return N("TextBlock", name, var=var, props=props)


def btn(name, label, color, var=True, size=15, tname=None):
    t = tb(tname or "Lbl_" + name, label, size, var=bool(tname))
    S(t, pad=(14, 8, 14, 8), h="Center", v="Center")
    return N("Button", name, [t], var, [("BackgroundColor", color)])


def chk(name, label):
    return N("CheckBox", name, [tb("Lbl_" + name, label, 15, var=False)])


def edt(name):
    k = "(SpecifiedColor=%s,ColorUseRule=UseColor_Specified)" % BLACK_RAW
    style = ("(TextStyle=(Font=(Size=%d),ColorAndOpacity=%s),ForegroundColor=%s,"
             "FocusedForegroundColor=%s,ReadOnlyForegroundColor=%s)" % (TXT_SIZE, k, k, k, k))
    return N("EditableTextBox", name, props=[("WidgetStyle", style)])


def select_field(key, minw=None):
    """select field: Sel_<key> button (current choice in Txt_Sel<key>). Click opens the shared modal Pnl_Modal. Lua: selSet / selInit."""
    cur = S(tb("Txt_Sel" + key, "", TXT_SIZE, color=BLACK_RAW), pad=(8, 4, 8, 4), h="Left", v="Center")
    top = N("Button", "Sel_" + key, [cur], props=[("WidgetStyle", "(Normal=%s,Hovered=%s,Pressed=%s)" % (
        round_brush(CMB_BG), round_brush(GOLD2), round_brush(GOLD2)))])
    return wrap(top, minw) if minw else top


def var_on(n):
    n.var = True   # Lua must find it by name (GetWidgetFromName skips layout-only widgets)
    return n


def hrow(name, kids):
    return N("HorizontalBox", name, kids, var=False)


def wrap(node, minw=None, minh=None, maxw=None, maxh=None):
    props = []
    if minw:
        props.append(("MinDesiredWidth", "%.1f" % minw))
    if maxw:
        props.append(("MaxDesiredWidth", "%.1f" % maxw))
    if minh:
        props.append(("MinDesiredHeight", "%.1f" % minh))
    if maxh:
        props.append(("MaxDesiredHeight", "%.1f" % maxh))
    return N("SizeBox", "Row_Sz_" + node.name, [node], var=False, props=props)


def row(label_name, label, value, minw=160, v="Center"):
    return hrow("Row_" + value.name, [
        S(tb(label_name, label, 15, minw=220), pad=(0, 0, 12, 0), v=v),
        S(wrap(value, minw), fill=1, v=v),
    ])


def vbox(name, kids=()):
    return N("VerticalBox", name, kids)


def stack(kids, b=10, r=0):
    """default stack slot: bottom spacing b, right pad r (scrollbar gutter), fill width"""
    for k in kids:
        if not k.sp:
            S(k, pad=(0, 0, r, b), h="Fill")
    return kids


def hdr(name, text, size=16):
    return S(tb(name, text, size, var=(name == "Txt_LoopHeader"), color=GOLD), pad=(0, 8, 0, 8))


def page(name, kids):
    sc = N("ScrollBox", "Row_Scroll_" + name, stack(kids, 10, 12), var=False, props=[("ConsumeMouseWheel", "WhenScrollingPossible")])
    S(sc, fill=1, h="Fill", v="Fill")
    return S(vbox(name, [sc]), h="Fill", v="Fill")


GOLD2 = lin("C38800")   # sampled from the native Ship Inventory header (C18600-C38800)
TAB_ON = GOLD2
BODY = lin("3A3A3A", 0.97)
INSET = lin("2B2B2B")
OUTLINE = lin("1C1C1C")
GREEN = lin("42DA41")
CLEAR = lin("000000", 0.0)
RR_TAB = ("(DrawAs=RoundedBox,OutlineSettings=(CornerRadii=(X=10.0,Y=22.0,Z=0.0,W=0.0),"
          "RoundingType=FixedRadius))")


def game_head(key, title, right=None):
    """game header row: gold tab (title node) + gold line + optional right widget. Returns the Row_Header<key> HBox."""
    if title.cls == "TextBlock":   # title ellipsis
        title.props.append(("TextOverflowPolicy", "ETextOverflowPolicy::Ellipsis"))
    if ART:
        tab_props = [("BrushColor", GOLD_RAW), ("Background", tex("Header_L", m=M_TAB))]
        line = S(N("Border", "Row_Line" + key, var=False,
                   props=[("BrushColor", GOLD_RAW), ("Background", tex("Header_Bar", m=M_LINE))]),
                 fill=1, h="Fill", v="Fill")
    else:
        tab_props = [("BrushColor", GOLD2)]
        line = S(wrap(N("Border", "Row_Line" + key, var=False, props=[("BrushColor", GOLD2)]), None, 20),
                 fill=1, h="Fill", v="Center")   # flat: 20px solid band, centred in the header row
    tab = N("Border", "Row_Tab" + key, [S(title, pad=(33, 0, 33, 0), v="Center")], var=False,
            props=tab_props)
    head = [S(wrap(tab, None, HDR_H, TAB_MAX_W), v="Fill"), line] + ([right] if right else [])
    return hrow("Row_Header" + key, head)


def window(key, title, content, right=None, vfill=False):
    """native-style window: gold header tab + gold line (+ optional right widget), outline > body > darker inset.
    Header row = HDR_H tall. Tab width = Auto (grows with its text, max TAB_MAX_W). Line fills the rest."""
    head_row = game_head(key, title, right)
    inset = N("Border", "Row_Inset" + key, [S(content, h="Fill", v="Fill")], var=False,
              props=[("BrushColor", INSET), ("Padding", pad(16, 12, 16, 12))])
    body = N("Border", "Row_Body" + key, [S(inset, h="Fill", v="Fill")], var=False,
             props=[("BrushColor", BODY), ("Padding", pad(10, 10, 10, 10))])
    outline = S(N("Border", "Row_Outline" + key, [S(body, h="Fill", v="Fill")], var=False,
                  props=[("BrushColor", OUTLINE), ("Padding", pad(2, 2, 2, 2))]), h="Fill", v="Fill")
    if vfill:
        S(outline, fill=1)
    return N("VerticalBox", "Row_Win" + key, [S(head_row, h="Fill"), outline], var=False)


def close_cap(btn="Btn_Close", txt="Txt_Close", key=""):
    """Centre header right end. Flat: gold 'X' button. Art: gold Header_R cap (Border) holding Btn_Close
    styled with Header_R_Out/Header_R_In; dark "X" text (Txt_Close) in an Overlay."""
    if not ART:
        t = S(tb(txt, "X", 16, color=DARK_TXT), pad=(4, 2, 4, 2), h="Center", v="Center")
        return N("Button", btn, [t], props=[("BackgroundColor", GOLD2)])
    # an "X" text on the gold cap (no texture: the old T_UI_X icon drew as a solid square in game)
    txt = S(tb(txt, "X", 18, color=DARK_TXT), h="Center", v="Center")
    overlay = S(N("Overlay", "Row_CloseOverlay" + key, [txt], var=False), h="Fill", v="Fill")
    # 2 layers only: gold cap (Row_CloseCap Border, Header_R) + X icon. Button body is transparent (no draw).
    style = ("(Normal=(DrawAs=NoDrawType),Hovered=(DrawAs=Box,TintColor=(SpecifiedColor=%s)),"
             "Pressed=(DrawAs=Box,TintColor=(SpecifiedColor=%s)))" % (lin("FFFFFF", 0.2), lin("000000", 0.25)))
    btn = S(N("Button", btn, [overlay], props=[("WidgetStyle", style)]), h="Fill", v="Fill")
    return N("Border", "Row_CloseCap" + key, [btn], var=False,
             props=[("Background", tex("Header_R", m=M_CAP)), ("BrushColor", GOLD_RAW)])


ANCH = "(Offsets=(Left=0.0,Top=0.0,Right=0.0,Bottom=0.0),Anchors=(Minimum=(X=0.15,Y=0.15),Maximum=(X=0.85,Y=0.85)))"


def ship_card(P, i, button, compact=False):
    """one ship card. P = name prefix ("Card" modal / "Fleet" side list). button: Button Opt_Card_<i> (+Bdr_CardSel); else Border Pnl_Fleet_<i>."""
    hw, hh = (40, 64) if compact else (64, 104)
    holo = N("Image", "Img_%sHolo_%d" % (P, i), props=[("Brush", "(ImageSize=(X=%d.000000,Y=%d.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % (hw, hh, WHITE_RAW))])
    holoscale = N("ScaleBox", "Row_%sHoloScale_%d" % (P, i), [S(holo, h="Fill", v="Fill")], var=False, props=[("Stretch", "EStretch::ScaleToFit")])  # aspect-fit: Lua sets brush to texture size
    holosz = S(N("SizeBox", "Row_%sHoloSz_%d" % (P, i), [S(holoscale, h="Fill", v="Fill")], var=False,
                 props=[("WidthOverride", "%d.0" % hw), ("HeightOverride", "%d.0" % hh), ("bOverride_WidthOverride", "True"), ("bOverride_HeightOverride", "True")]),
               pad=(0, 0, 8 if compact else 14, 0), v="Center")
    name = tb("Txt_%sName_%d" % (P, i), "", 16 if compact else 20)
    name.props.append(("TextOverflowPolicy", "ETextOverflowPolicy::Ellipsis"))
    pin = N("Image", "Img_%sPin_%d" % (P, i), props=[("Brush", icon_brush("Pin", WHITE_RAW if compact else GOLD_RAW, 20))])
    dist = tb("Txt_%sDist_%d" % (P, i), "", 13)
    if compact:  # pin + distance on their own row under the name (built below)
        top = hrow("Row_%sTop_%d" % (P, i), [S(name, fill=1, v="Center")])
    else:
        top = hrow("Row_%sTop_%d" % (P, i), [S(name, fill=1, v="Center"), S(dist, pad=(0, 0, 6, 0), v="Center"), S(pin, v="Center")])
    sz = 20 if compact else 24
    stat = hrow("Row_%sStat_%d" % (P, i), [
        S(N("Image", "Img_%sStat_%d" % (P, i), props=[("Brush", "(ImageSize=(X=%d.000000,Y=%d.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % (sz, sz, WHITE_RAW))]), pad=(0, 0, 8, 0), v="Center"),
        S(tb("Txt_%sStat_%d" % (P, i), "", 13 if compact else 15), v="Center")])
    lines = []
    tsz = 12 if compact else 13
    ellip = ("TextOverflowPolicy", "ETextOverflowPolicy::Ellipsis")
    mlbl = tb("Txt_%sMineLbl_%d" % (P, i), "Mining:", tsz)
    mlbl.props += [ellip, ("Visibility", "Collapsed")]
    mimg = N("Image", "Img_%sOre_%d" % (P, i), props=[("Brush", "(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Hidden")])
    mtxt = tb("Txt_%sMine_%d" % (P, i), "", tsz)
    mtxt.props += [ellip, ("Visibility", "Collapsed")]
    lines.append(S(hrow("Row_%sMineRow_%d" % (P, i), [
        S(mlbl, pad=(0, 0, 6, 4), v="Center"), S(wrap(mimg, 24, 24), pad=(0, 0, 6, 4), v="Center"), S(mtxt, pad=(0, 0, 0, 4), fill=1, v="Center")]), h="Fill"))
    t = tb("Txt_%sNext_%d" % (P, i), "", tsz)
    t.props += [ellip, ("Visibility", "Collapsed")]
    lines.append(S(t, pad=(0, 0, 0, 4), h="Fill"))
    pbn = N("ProgressBar", "PB_%s_%d" % (P, i), props=[("Percent", "0.0"), ("FillColorAndOpacity", GREEN)])
    cargo = hrow("Row_%sCargo_%d" % (P, i), [
        S(tb("Txt_%sCargo_%d" % (P, i), "", 12 if compact else 13, minw=44), pad=(0, 0, 8, 0), v="Center"),
        S(N("SizeBox", "Row_%sBarSz_%d" % (P, i), [S(pbn, h="Fill", v="Fill")], var=False,
            props=[("HeightOverride", "%d.0" % (6 if compact else 8)), ("bOverride_HeightOverride", "True")]), fill=1, v="Center")])
    kids = [S(top, pad=(0, 0, 0, 4 if compact else 6), h="Fill")]
    if compact:
        kids.append(S(hrow("Row_%sDistRow_%d" % (P, i), [S(pin, pad=(0, 0, 6, 0), v="Center"), S(dist, v="Center")]), pad=(0, 0, 0, 4), h="Fill"))
    col = S(vbox("Row_%sCol_%d" % (P, i), kids + [S(stat, pad=(0, 0, 0, 6), h="Fill")] + lines + [S(cargo, h="Fill")]), fill=1, v="Center")
    col.var = False
    kids_row = [holosz, col]
    rowc = hrow("Row_%sRow_%d" % (P, i), kids_row)
    if not button:
        return N("Border", "Pnl_Fleet_%d" % i, [S(rowc, h="Fill", v="Fill")], props=[("BrushColor", lin("1A1A1A", 0.55)), ("Padding", pad(6, 6, 6, 6)), ("Visibility", "Collapsed")])
    cstyle = "(Normal=%s,Hovered=%s,Pressed=%s)" % (box_brush(lin("1A1A1A", 0.55), WHITE_RAW, 0.0),
                                                    box_brush(lin("2B2B2B", 0.8), GOLD_RAW, 0.0), box_brush(lin("2B2B2B", 0.8), GOLD_RAW, 0.0))
    sel = N("Border", "Bdr_CardSel_%d" % i, [S(rowc, h="Fill", v="Fill")], props=[("BrushColor", CLEAR), ("Padding", pad(10, 8, 12, 8))])
    return N("Button", "Opt_Card_%d" % i, [S(sel, h="Fill", v="Fill")], props=[("WidgetStyle", cstyle), ("Visibility", "Collapsed")])


def modal():
    """ONE shared modal select: dim click-outside button + centered panel (title, X, search, ScrollBox of SEL_N rows, up to 4 text columns + icon per row)."""
    dim = "(Normal=(DrawAs=Box,TintColor=(SpecifiedColor=%s)),Hovered=(DrawAs=Box,TintColor=(SpecifiedColor=%s)),Pressed=(DrawAs=Box,TintColor=(SpecifiedColor=%s)))" % (DIM, DIM, DIM)
    outside = S(N("Button", "Btn_ModalOutside", props=[("WidgetStyle", dim)]), h="Fill", v="Fill")
    xb = S(N("Button", "Btn_ModalClose", [S(tb("Txt_ModalClose", "X", 16, color=DARK_TXT), pad=(10, 2, 10, 2), h="Center", v="Center")],
             props=[("BackgroundColor", GOLD2)]), v="Center")
    headp = hrow("Pnl_ModalHeadP", [S(tb("Txt_ModalTitle", "", 16, color=GOLD), fill=1, v="Center"), xb])
    headp.var = True
    headp = S(headp, pad=(0, 0, 0, 8), h="Fill")
    # game-style header (ship cards): gold tab [ship icon + title] + gold bar + X cap
    tabc = hrow("Row_ModalTabG", [
        S(N("Image", "Img_ModalHeadIcon", props=[("Brush", icon_brush("Ship", WHITE_RAW, 22))]), pad=(0, 0, 8, 0), v="Center"),
        S(tb("Txt_ModalTitleG", "", 20), v="Center")])
    headg = game_head("G", tabc, S(wrap(close_cap("Btn_ModalCloseG", "Txt_ModalCloseG", "G"), 40, None), v="Fill"))
    headg.name, headg.var = "Pnl_ModalHeadG", True
    headg.props.append(("Visibility", "Collapsed"))
    headg = S(headg, pad=(0, 0, 0, 8), h="Fill")
    search = S(edt("Edt_ModalSearch"), pad=(0, 0, 0, 8), h="Fill")
    hk = []
    for c, wt in enumerate((2.0, 1.2, 1.0, 0.8), 1):
        hk.append(S(tb("Txt_ModalH%d" % c, "", 12, color=GOLD), fill=wt, pad=(8, 0, 8, 0), h="Left", v="Center"))
    chr_ = hrow("Pnl_ModalColHead", hk)
    chr_.var = True
    colhead = S(chr_, pad=(22, 0, 50, 4), h="Fill")
    ost = "(Normal=%s,Hovered=%s,Pressed=%s)" % (round_brush(WHITE_RAW), round_brush(HOVER_T), round_brush(HOVER_T))
    rows = []
    for i in range(1, SEL_N + 1):
        kids = [S(N("Image", "Img_ModalIcon_%d" % i, props=[("Brush", "(ImageSize=(X=20.000000,Y=20.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Collapsed")]), pad=(8, 0, 0, 0), v="Center")]
        for c, wt in enumerate((2.0, 1.2, 1.0, 0.8), 1):
            t = tb("Txt_ModalC%d_%d" % (c, i), "", TXT_SIZE, color=BLACK_RAW)
            t.props.append(("TextOverflowPolicy", "ETextOverflowPolicy::Ellipsis"))
            kids.append(S(t, fill=wt, pad=(8, 3, 8, 3), h="Left", v="Center"))
        ores = hrow("Row_ModalOres_%d" % i, [
            S(N("Image", "Img_ModalOre_%d_%d" % (i, k), props=[("Brush", "(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Collapsed")]),
              pad=(8 if k == 1 else 6, 0, 0, 0), v="Center") for k in range(1, 7)])
        ores.var = True
        ores.props.append(("Visibility", "Collapsed"))
        mcol = vbox("Row_ModalCol_%d" % i, [S(hrow("Row_ModalRow_%d" % i, kids), h="Fill", v="Center"), S(ores, pad=(0, 2, 0, 4), h="Left")])
        mcol.var = False
        b = N("Button", "Opt_Modal_%d" % i, [S(mcol, h="Fill", v="Center")],
              props=[("WidgetStyle", ost), ("BackgroundColor", CMB_BG), ("Visibility", "Collapsed")])
        rows.append(S(b, pad=(0, 0, 12, 2), h="Fill"))
    sb = N("ScrollBox", "Row_ModalScroll", rows, props=[("ConsumeMouseWheel", "WhenScrollingPossible"), ("AlwaysShowScrollbar", "True")])
    crows = []
    for i in range(1, CN + 1):
        hb = N("Button", "Btn_CardHdr_%d" % i, [S(tb("Txt_CardHdr_%d" % i, "", 18, color=DARK_TXT), pad=(12, 6, 12, 6), h="Left", v="Center")],
               props=[("BackgroundColor", CMB_BG)])
        fb = N("Button", "Btn_CardFold_%d" % i, [S(tb("Txt_CardFold_%d" % i, "v", 18, color=DARK_TXT), pad=(14, 6, 14, 6), h="Center", v="Center")],
               props=[("BackgroundColor", CMB_BG), ("Visibility", "Collapsed")])
        hr = var_on(hrow("Row_CardHdr_%d" % i, [S(hb, fill=1, h="Fill", v="Fill"), S(fb, pad=(6, 0, 0, 0), v="Fill")]))
        hr.props.append(("Visibility", "Collapsed"))
        crows.append(S(hr, pad=(0, 0, 12, 8), h="Fill"))
        crows.append(S(ship_card("Card", i, True), pad=(0, 0, 12, 8), h="Fill"))
    cards_sb = N("ScrollBox", "Row_ModalCardScroll", crows, props=[("ConsumeMouseWheel", "WhenScrollingPossible"), ("AlwaysShowScrollbar", "True"), ("Visibility", "Collapsed")])
    done = S(N("Button", "Btn_ModalDone", [S(tb("Txt_ModalDone", "Done", 16, color=DARK_TXT), pad=(24, 6, 24, 6), h="Center", v="Center")],
               props=[("BackgroundColor", GOLD2), ("Visibility", "Collapsed")]), pad=(0, 8, 12, 0), h="Right")
    bodyv = vbox("Row_ModalBody", [headg, headp, search, colhead, S(sb, fill=1, h="Fill", v="Fill"), S(cards_sb, fill=1, h="Fill", v="Fill"), done])
    bodyv.var = False
    body = N("Border", "Row_ModalBorder", [S(bodyv, h="Fill", v="Fill")], var=False, props=[("BrushColor", BODY), ("Padding", pad(14, 12, 14, 12))])
    outline = N("Border", "Row_ModalOutline", [S(body, h="Fill", v="Fill")], var=False, props=[("BrushColor", OUTLINE), ("Padding", pad(2, 2, 2, 2))])
    size = N("SizeBox", "Row_ModalSize", [S(outline, h="Fill", v="Fill")], var=False, props=[("WidthOverride", "720.0"), ("HeightOverride", "640.0"), ("bOverride_WidthOverride", "True"), ("bOverride_HeightOverride", "True"),
                   ("MinDesiredWidth", "720.0"), ("MinDesiredHeight", "640.0"), ("bOverride_MinDesiredWidth", "True"), ("bOverride_MinDesiredHeight", "True")])
    src = hrow("Row_StatSrc", [N("Image", "Img_StatSrc_" + k, props=[("Brush", icon_brush("St" + k.capitalize(), WHITE_RAW, 24))]) for k in STAT_KEYS] + [N("Image", "Img_ItemSrc_" + i, props=[("Brush", item_brush(i))]) for i in ITEM_ICON_IDS])
    src.props.append(("Visibility", "Collapsed"))   # hidden texture source: Lua reads Brush.ResourceObject
    return N("Overlay", "Pnl_Modal", [outside, S(size, h="Center", v="Center"), S(src, h="Left", v="Top")], props=[("Visibility", "Collapsed")])


# Hover explanations (Operation tab). name -> text; labels listed in TIP_LABELS get the same text (TextBlocks need Visible to show a tooltip).
# No double quotes or newlines inside the texts (they go into INVTEXT("...")).
FUEL_TIP = {"Deuterium": "Enriched fuel (deuterium)", "Hydrogen": "Hydrogen fuel", "Warp": "Warp fuel"}
TIPS = {
    "Edt_Radius": "Radius: how far from where the ship is that it looks for a rock to mine (50km, 2500m; a plain number means meters). Default 50km. 0 = no limit. With a sector selected it can never be more than Warp range. Saved per ship.",
    "Edt_MiningDistance": "Mining distance: how far from the rock the ship stops while mining (50m, 0.2km; a plain number means meters). Default 50m. Saved per ship.",
    "Edt_MaxRadius": "Max radius: upper limit for Radius on every ship (50km, 8000m; a plain number means meters). Default 50km. 0 = no limit. Applies to all ships.",
    "Edt_IdleSeconds": "Idle seconds: how long the ship may find nothing to mine (or gain no ore) before it goes home. 0 = never. Also how long a refuel may stall before it stops. Saved per ship.",
    "Edt_WaitSeconds": "Wait seconds: how long the ship waits at the dock after its unload or refuel orders before it checks them. Applies to all ships.",
    "Edt_DockHoldLimit": "Dock time limit: most seconds the ship stays docked unloading or refuelling with items still left. After that it undocks, waits and docks again; after 10 rounds it undocks and goes idle. 0 = no limit. Saved per ship.",
    "Chk_Enabled": "Enabled: when off, the mining loop ignores this ship. Saved per ship.",
    "Chk_Repeat": "Repeat: when the item queue is finished, go home, unload and start the queue again. Off = stop after the last row. Saved per ship.",
    "Sel_Belt": "Sector Selector: the sector the ship warps to when there is no ore in range. AUTO picks the nearest sector that still has the ore you mine. Saved per ship.",
    "Btn_BeltPinned": "Use pinned sector: take the sector you pinned on the map as this ship's sector.",
    "Btn_BeltWarp": "Warp to sector: send the ship to its sector now. Not possible while the mining loop is running.",
    "Edt_WarpRange": "Warp range: furthest a ship flies to a rock before warping to another sector instead (50km, 20000m or plain meters). Empty = 50km. Also caps Radius. Only used when a sector is selected. Saved per ship.",
    "Edt_WarpTripKm": "Warp trip distance: an unload, refuel or dock trip longer than this many km warps if the ship has warp fuel (otherwise it flies). Saved per ship.",
    "Btn_ApplyGlobal": "Set as default: copy this ship's Operation settings to the defaults that new ships and ships without their own value use.",
}
for _k, _n in FUEL_TIP.items():
    TIPS["Chk_Fuel%sOn" % _k] = "%s: when on, the ship refuels it at the refuel dock. Applies to all ships." % _n
    TIPS["Edt_Fuel%sThreshold" % _k] = "%s min: refuel when the ship has less than this many in its cargo. Applies to all ships." % _n
    TIPS["Edt_Fuel%sAmount" % _k] = "%s max: how many the ship takes on when refuelling (it waits at the dock until it has this many). Applies to all ships." % _n
for _k in ORES:
    _n = "Water Ice" if _k == "WaterIce" else _k
    TIPS["Chk_Unload%s" % _k] = "%s: when on, this ore is unloaded at this ship's unload docks for it (Ship tab). Off = it stays in the cargo. Applies to all ships." % _n
TIP_LABELS = {"Edt_Radius": "Txt_OpRadiusLabel", "Edt_MiningDistance": "Txt_OpMiningDistLabel", "Edt_MaxRadius": "Txt_OpMaxRadiusLabel",
              "Edt_IdleSeconds": "Txt_OpIdleLabel", "Edt_WaitSeconds": "Txt_OpWaitLabel", "Edt_DockHoldLimit": "Txt_OpDockLimitLabel",
              "Sel_Belt": "Txt_OpBeltLabel", "Edt_WarpRange": "Txt_OpWarpRangeLabel", "Edt_WarpTripKm": "Txt_OpWarpTripLabel"}
for _k in FUELS:
    TIP_LABELS["Edt_Fuel%sThreshold" % _k] = "Lbl_Fuel%sThreshold" % _k
    TIP_LABELS["Edt_Fuel%sAmount" % _k] = "Lbl_Fuel%sAmount" % _k


def apply_tips(root):
    """add ToolTipText to every widget named in TIPS (+ its label); labels also get Visibility Visible so the tooltip shows"""
    by_name = {}
    def walk(n):
        by_name[n.name] = n
        for k in n.kids:
            walk(k)
    walk(root)
    for name, text in TIPS.items():
        w = by_name.get(name)
        assert w, "tooltip target missing: " + name
        w.props.append(("ToolTipText", 'INVTEXT("%s")' % text))
        lab = by_name.get(TIP_LABELS.get(name, ""))
        if lab:
            lab.props.append(("ToolTipText", 'INVTEXT("%s")' % text))
            lab.props.append(("Visibility", "Visible"))


LOG_N = 60   # Txt_Log_1..N pool, must equal wbp_loader.lua LN
LOG_ROWS = 10   # Txt_LogCard_<f>_<r> rows per log card, must equal wbp_loader.lua LOG_ROWS (cards use FN)


def build_tree():
    tabs =[("Btn_TabShip", "Txt_TabShip", "Ship"), ("Btn_TabItems", "Txt_TabItems", "Items"),
            ("Btn_TabOperation", "Txt_TabOperation", "Operation"), ("Btn_TabLog", "Txt_TabLog", "Log"),
            ("Btn_TabDocks", "Txt_TabDocks", "Docks")]
    tab_nodes = []
    for i, (bn, tn, label) in enumerate(tabs):
        t = S(tb(tn, label, 16, color=DARK_TXT if i == 0 else None), pad=(16, 8, 16, 8), h="Center", v="Center")
        tab_nodes.append(S(N("Button", bn, [t], props=[("BackgroundColor", TAB_ON if i == 0 else TAB_OFF)]),
                           fill=1, pad=(0 if i == 0 else 3, 0, 0 if i == len(tabs) - 1 else 3, 0), h="Fill", v="Fill"))

    def smallbtn(bn, tn, label):
        t = S(tb(tn, label, 12), pad=(8, 2, 8, 2), h="Center", v="Center")
        return S(N("Button", bn, [t], props=[("BackgroundColor", BTN)]), h="Fill", pad=(0, 0, 0, 2))
    def hidden_txt(tn):
        t = tb(tn, "", 12)
        t.props.append(("Visibility", "Collapsed"))
        return t
    def arrowbtn(bn, tn, ic):
        img = S(N("Image", "Row_%sIcon" % bn, var=False, props=[("Brush", icon_brush(ic, ARR, 20))]), h="Center", v="Center")
        ov = S(N("Overlay", "Row_%sOverlay" % bn, [img, hidden_txt(tn)], var=False), h="Fill", v="Fill")
        st = "(Normal=(DrawAs=NoDrawType),Hovered=(DrawAs=NoDrawType),Pressed=(DrawAs=NoDrawType))"
        return S(wrap(N("Button", bn, [ov], props=[("WidgetStyle", st)]), 20, 20), h="Fill", pad=(0, 0, 0, 0))
    def trashbtn(r):
        ic = S(N("Image", "Row_ItemDelIcon_%d" % r, var=False, props=[("Brush", icon_brush("Trash", ARR, 24))]), h="Center", v="Center")
        ov = S(N("Overlay", "Row_ItemDelOverlay_%d" % r, [ic, hidden_txt("Txt_ItemDel_%d" % r)], var=False), h="Fill", v="Fill")
        st = "(Normal=%s,Hovered=%s,Pressed=%s)" % (box_brush(lin("2E2E2E"), lin("1A1A1A")), box_brush(lin("3C3C3C"), lin("1A1A1A")), box_brush(lin("222222"), lin("1A1A1A")))
        return S(wrap(N("Button", "Btn_ItemDel_%d" % r, [ov], props=[("WidgetStyle", st)]), 40, 40), v="Center")
    rows = []
    for r in range(1, ROW_N + 1):
        icon = S(wrap(N("Image", "Img_ItemIcon_%d" % r, props=[("Brush", "(ImageSize=(X=32.000000,Y=32.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Hidden")]), 32, 32), pad=(0, 0, 8, 0), v="Top")
        sel = S(select_field("Item%d" % r), fill=1, h="Fill", v="Top")
        asz = wrap(edt("Edt_ItemAmount_%d" % r), 110)
        asz.props.append(("WidthOverride", "110.0"))
        amt = S(asz, pad=(8, 0, 8, 0), v="Top")
        if ART:
            ud = S(N("VerticalBox", "Row_ItemUD_%d" % r, [arrowbtn("Btn_ItemUp_%d" % r, "Txt_ItemUp_%d" % r, "Up"), arrowbtn("Btn_ItemDown_%d" % r, "Txt_ItemDown_%d" % r, "Down")], var=False), v="Center", pad=(0, 0, 8, 0))
            dl = trashbtn(r)
        else:
            ud = S(N("VerticalBox", "Row_ItemUD_%d" % r, [smallbtn("Btn_ItemUp_%d" % r, "Txt_ItemUp_%d" % r, UP_TXT), smallbtn("Btn_ItemDown_%d" % r, "Txt_ItemDown_%d" % r, DN_TXT)], var=False), v="Top", pad=(0, 0, 8, 0))
            dl = S(btn("Btn_ItemDel_%d" % r, DEL_TXT, DANGER, size=12, tname="Txt_ItemDel_%d" % r), v="Top")
        rw = N("HorizontalBox", "Pnl_ItemRow_%d" % r, [icon, sel, amt, ud, dl], props=[("Visibility", "Collapsed")])
        rows.append(S(rw, pad=(0, 0, 0, 6), h="Fill"))
    add = S(wrap(btn("Btn_ItemAdd", "Add resource", BTN, size=15, tname="Txt_ItemAdd"), None, 40), pad=(0, 4, 0, 0), h="Fill")
    hint = S(tb("Txt_AmountHint", "0 = unlimited (mine until cargo is full)", 13, color=GOLD), pad=(0, 0, 0, 6))
    page_items = page("Page_Items", [hint] + rows + [add])
    # Operation page: bordered cards (outline > body, gold title + 2px gold line). Layout-only widgets are Row_*/Lbl_*.
    def card(key, title_name, title, kids, icons=()):
        t = S(tb(title_name, title, 16, var=False, color=GOLD), pad=(0, 0, 0, 4))
        if icons:   # header icons (Lua sets brushes)
            ims = [S(wrap(N("Image", i, props=[("Brush", "(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Hidden")]), 24, 24), pad=(8, 0, 0, 0), v="Center") for i in icons]
            t = S(hrow("Row_CardHead" + key, [S(tb(title_name, title, 16, var=False, color=GOLD), v="Center")] + ims), pad=(0, 0, 0, 4), h="Fill")
        ln = S(wrap(N("Border", "Row_CardLine" + key, var=False, props=[("BrushColor", GOLD2)]), None, 2), pad=(0, 0, 0, 8), h="Fill")
        for k in kids:
            if not k.sp:
                S(k, pad=(0, 0, 0, 8), h="Fill")
        inner = vbox("Row_CardInner" + key, [t, ln] + kids)
        inner.var = False
        body = S(N("Border", "Row_CardBody" + key, [S(inner, h="Fill", v="Fill")], var=False,
                   props=[("BrushColor", BODY), ("Padding", pad(8, 8, 8, 2))]), h="Fill", v="Fill")
        return N("Border", "Row_CardOutline" + key, [body], var=False,
                 props=[("BrushColor", OUTLINE), ("Padding", pad(2, 2, 2, 2))])

    def num(e, w=80):
        z = wrap(e, w)
        z.props.append(("WidthOverride", "%.1f" % w))
        return z

    def crow(label_name, label, e):
        return hrow("Row_" + e.name, [S(tb(label_name, label, 15), fill=1, pad=(0, 0, 6, 0), v="Center"),
                                      S(num(e), v="Center")])

    # Ship page: warning, refuel dock + refuel list card, one unload-dock card per ore (Lua shows used rows/cards)
    def dock_trash(bn):
        ic = S(N("Image", "Row_%sIcon" % bn, var=False, props=[("Brush", icon_brush("Trash", ARR, 24))]), h="Center", v="Center")
        st = "(Normal=%s,Hovered=%s,Pressed=%s)" % (box_brush(lin("2E2E2E"), lin("1A1A1A")), box_brush(lin("3C3C3C"), lin("1A1A1A")), box_brush(lin("222222"), lin("1A1A1A")))
        return S(wrap(N("Button", bn, [ic], props=[("WidgetStyle", st)]), 40, 40), pad=(8, 0, 0, 0), v="Center")

    def dock_rows(tag, key):
        """UDOCK_N rows: Pnl_<tag>Row_<key>_<m> = select + trash; returns row list"""
        out = []
        for m in range(1, UDOCK_N + 1):
            suf = "%s_%d" % (key, m) if key else str(m)
            sel = S(select_field("%s_%s" % (tag, suf)), fill=1, h="Fill", v="Center")
            rw = N("HorizontalBox", "Pnl_%sRow_%s" % (tag, suf), [sel, dock_trash("Btn_%sDel_%s" % (tag, suf))], props=[("Visibility", "Collapsed")])
            out.append(S(rw, pad=(0, 0, 0, 6), h="Fill"))
        return out

    def dock_card(tag, key, ckey, title, icons=()):
        add = S(wrap(btn("Btn_%sAdd%s" % (tag, "_" + key if key else ""), "Add dock", BTN, size=15), None, 40), pad=(0, 4, 0, 0), h="Fill")
        c = card(ckey, "Lbl_Hdr" + ckey, title, dock_rows(tag, key) + [add], icons)
        c.name, c.var = "Pnl_%sCard%s" % (tag, "_" + key if key else ""), True
        c.props.append(("Visibility", "Collapsed"))
        return c

    warn = tb("Txt_ShipWarn", "", 15, color=lin("F2D21E"))
    warn.props += [("AutoWrapText", "True"), ("Visibility", "Collapsed")]
    ship_kids = [warn,
                 dock_card("RDock", "", "RDock", "Refuel docks (tried in order)", ["Img_RDockFuel" + f for f in FUELS])]
    for k in reversed(ORES):
        ship_kids.append(dock_card("UDock", k, "UD" + k, "Unload docks: " + ("Water Ice" if k == "WaterIce" else k), ["Img_UDockOre_" + k]))
    page_ship = page("Page_Ship", ship_kids)

    c_mining = card("Mining","Lbl_HdrMining", "Mining", [
        crow("Txt_OpRadiusLabel", "Radius", edt("Edt_Radius")),
        crow("Txt_OpMiningDistLabel", "Mining distance", edt("Edt_MiningDistance")),
        crow("Txt_OpMaxRadiusLabel", "Max radius (global)", edt("Edt_MaxRadius"))])
    c_timing = card("Timing", "Lbl_HdrTiming", "Timing", [
        crow("Txt_OpIdleLabel", "Idle seconds", edt("Edt_IdleSeconds")),
        crow("Txt_OpWaitLabel", "Wait seconds", edt("Edt_WaitSeconds")),
        crow("Txt_OpDockLimitLabel", "Dock time limit (s)", edt("Edt_DockHoldLimit"))])
    c_run = card("Run", "Lbl_HdrRun", "Run", [
        hrow("Row_Flags", [S(chk("Chk_Enabled", "Enabled"), fill=1), S(chk("Chk_Repeat", "Repeat"), fill=1)])])
    top_l = vbox("Row_OpColL", [S(c_mining, fill=1, h="Fill", v="Fill")])
    top_l.var = False
    top_r = vbox("Row_OpColR", [S(c_timing, pad=(0, 0, 0, 10), h="Fill"), S(c_run, h="Fill")])
    top_r.var = False
    top = hrow("Row_OpTop", [S(top_l, fill=1.2, pad=(0, 0, 5, 0), h="Fill", v="Fill"),
                             S(top_r, fill=0.8, pad=(5, 0, 0, 0), h="Fill", v="Top")])
    fuel_rows = []
    for k in FUELS:
        fuel_rows.append(hrow("Row_Fuel" + k, [
            S(wrap(N("Image", "Img_Fuel" + k, props=[("Brush", "(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Hidden")]), 24, 24), pad=(0, 0, 8, 0), v="Center"),
            S(chk("Chk_Fuel%sOn" % k, k), fill=1, v="Center"),
            S(tb("Lbl_Fuel%sThreshold" % k, "min", 14, var=False), pad=(0, 0, 6, 0), v="Center"),
            S(num(edt("Edt_Fuel%sThreshold" % k), 80), pad=(0, 0, 16, 0), v="Center"),
            S(tb("Lbl_Fuel%sAmount" % k, "max", 14, var=False), pad=(0, 0, 6, 0), v="Center"),
            S(num(edt("Edt_Fuel%sAmount" % k), 80), v="Center"),
        ]))
    c_fuel = card("Fuel", "Lbl_HdrFuel", "Fuel", fuel_rows)
    ore_chks = [S(hrow("Row_OreCell" + k, [
        S(wrap(N("Image", "Img_Unload" + k, props=[("Brush", "(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Hidden")]), 24, 24), pad=(0, 0, 6, 0), v="Center"),
        S(chk("Chk_Unload" + k, "Water Ice" if k == "WaterIce" else k), fill=1, v="Center")]),
        fill=1, pad=(0, 0, 8, 0), v="Center") for k in ORES]
    ore_chks.append(S(N("SizeBox", "Row_OreSpacer", var=False), fill=1))
    c_ores = card("Ores", "Lbl_HdrOres", "Unload ores", [hrow("Row_Ores1", ore_chks[:4]), hrow("Row_Ores2", ore_chks[4:])])
    c_ores.props.append(("Visibility", "Collapsed"))
    c_belt = card("Warp", "Lbl_HdrWarp", "Warp management", [
        row("Txt_OpBeltLabel", "Sector Selector", select_field("Belt")),
        crow("Txt_OpWarpRangeLabel", "Warp range", edt("Edt_WarpRange")),
        crow("Txt_OpWarpTripLabel", "Warp trip distance (km)", edt("Edt_WarpTripKm")),
        hrow("Row_BeltButtons", [S(wrap(btn("Btn_BeltPinned", "Use pinned sector", BTN), None, 36), fill=1, pad=(0, 0, 6, 0)),
                                 S(wrap(btn("Btn_BeltWarp", "Warp to sector", BTN), None, 36), fill=1)])])
    op = [top, c_belt, c_fuel, c_ores,
          S(hrow("Row_ApplyButtons", [wrap(btn("Btn_ApplyGlobal", "Set as default", BTN), 180, 36)]), pad=(0, 4, 12, 0), h="Right")]
    page_op = page("Page_Operation", op)

    # Log page: hint + ScrollBox of LOG_N one-line-ish TextBlocks (Lua: time + text, newest first)
    log_lines = [S(N("TextBlock", "Txt_Log_%d" % i, props=[("Font", "(Size=14)"), ("Text", 'INVTEXT("")'),
                                                          ("AutoWrapText", "True"), ("Visibility", "Collapsed")]),
                   pad=(6, 2, 12, 2), h="Fill") for i in range(1, LOG_N + 1)]
    log_sb = S(N("ScrollBox", "Row_LogScroll", log_lines, props=[("ConsumeMouseWheel", "WhenScrollingPossible")]), fill=1, h="Fill", v="Fill")
    log_hint = S(tb("Txt_LogHint", "No log yet", 14, color=GOLD), pad=(0, 0, 0, 6))
    def log_card(f):
        head = hrow("Row_LogCardHead_%d" % f, [
            S(tb("Txt_LogCardName_%d" % f, "", 16), pad=(0, 0, 10, 0), v="Center"),
            S(tb("Txt_LogCardStatus_%d" % f, "", 14, color=lin("FFFFFF", 0.6)), v="Center")])
        rows = [S(N("TextBlock", "Txt_LogCard_%d_%d" % (f, r), props=[("Font", "(Size=13)"), ("Text", 'INVTEXT("")'), ("AutoWrapText", "True")]),
                  pad=(0, 1, 0, 1), h="Fill") for r in range(1, LOG_ROWS + 1)]
        v = vbox("Row_LogCardCol_%d" % f, [S(head, pad=(0, 0, 0, 4), h="Fill")] + rows)
        v.var = False
        return S(N("Border", "Pnl_LogCard_%d" % f, [S(v, h="Fill", v="Fill")],
                   props=[("BrushColor", lin("1A1A1A", 0.55)), ("Padding", pad(10, 8, 10, 8)), ("Visibility", "Collapsed")]),
                 pad=(0, 0, 12, 8), h="Fill")
    log_all_sb = S(N("ScrollBox", "Row_LogAllScroll", [log_card(f) for f in range(1, FN + 1)], var=False, props=[("ConsumeMouseWheel", "WhenScrollingPossible")]), fill=1, h="Fill", v="Fill")
    log_all = S(vbox("Pnl_LogAll", [log_all_sb]), fill=1, h="Fill", v="Fill")
    log_all.props.append(("Visibility", "Collapsed"))
    page_log = S(vbox("Pnl_PageLog", [log_hint, log_sb, log_all]), h="Fill", v="Fill")

    # Docks page: header + ScrollBox Row_DocksScroll; rows (Dyn_Docks, WBP_NetDockRow) are added by add_docks_tab.py, not here
    docks_hdr = S(tb("Txt_DocksHdr", "Dock | Status | Used by | Purpose | Since | Waiting", 14, color=GOLD), pad=(6, 0, 6, 6))
    docks_sb = S(N("ScrollBox", "Row_DocksScroll", [], props=[("ConsumeMouseWheel", "WhenScrollingPossible")]), fill=1, h="Fill", v="Fill")
    page_docks = S(vbox("Pnl_PageDocks", [docks_hdr, docks_sb]), h="Fill", v="Fill")

    pages = [S(p, h="Fill", v="Fill") for p in (page_ship, page_items, page_op, page_log, page_docks)]
    warn_t = S(tb("Txt_AllWarn", "All ships mode: any change overrides the settings of every ship.", 14, color=lin("FFFFFF")), h="Left", v="Center")
    warn_t.props.append(("AutoWrapText", "True"))
    warn = N("Border", "Row_AllWarnBody", [warn_t], var=False, props=[("BrushColor", DANGER), ("Padding", pad(12, 8, 12, 8))])
    warn_o = N("Border", "Pnl_AllWarn", [S(warn, h="Fill")], props=[("BrushColor", GOLD2), ("Padding", pad(2, 2, 2, 2)), ("Visibility", "Collapsed")])
    center = vbox("Row_CenterBody", [
        S(N("HorizontalBox", "TabBar", tab_nodes), pad=(0, 0, 0, 14), h="Fill"),
        S(warn_o, pad=(0, 0, 0, 14), h="Fill"),
        S(N("WidgetSwitcher", "PageSwitcher", pages), fill=1, h="Fill", v="Fill"),
    ])
    center.var = False

    # LEFT: Control
    def big(n, l):
        return S(wrap(btn(n, l, BTN, size=18), None, 56), pad=(0, 0, 0, 12), h="Fill")
    if ART:
        # hazard frame (our stripes) > gold rim > red rounded Btn_EmergencyStop; Lua does not repaint it
        lbl = S(tb("Txt_EmergencyStop", "EMERGENCY STOP", 17, color=lin("FFFFFF")), pad=(10, 6, 10, 6),
                h="Center", v="Center")
        lbl.props.append(("AutoWrapText", "True"))
        lbl.props.append(("Justification", "ETextJustify::Center"))
        ebtn = N("Button", "Btn_EmergencyStop", [lbl], props=[("WidgetStyle", "(Normal=%s,Hovered=%s,Pressed=%s)" % (
            round_brush(EMERG), round_brush(EMERG_HOT), round_brush(EMERG_DARK)))])
        rim = N("Border", "Row_EstopRim", [S(ebtn, h="Fill", v="Fill")], var=False,
                props=[("BrushColor", lin("E8B800")), ("Padding", pad(3, 3, 3, 3))])
        frame = N("Border", "Row_EstopFrame", [S(rim, h="Fill", v="Fill")], var=False,
                  props=[("Background", stripe_brush()), ("Padding", pad(9, 9, 9, 9))])
        estop = S(wrap(frame, None, 70), pad=(0, 0, 0, 12), h="Fill")
    else:
        ebtn = btn("Btn_EmergencyStop", "Emergency stop (all ships)", EMERG, size=16, tname="Txt_EmergencyStop")
        ebtn.kids[0].props.append(("AutoWrapText", "True"))   # narrow Control column: let label wrap
        ebtn.kids[0].props.append(("Justification", "ETextJustify::Center"))
        estop = S(wrap(ebtn, None, 56), pad=(0, 0, 0, 12), h="Fill")
    control = vbox("Row_ControlBody", [estop, big("Btn_LoopStart", "Start loop"), big("Btn_LoopStop", "Stop loop")])
    control.var = False

    # LEFT, second box: one-shot actions (full width, 6px apart, same flat style as Start/Stop)
    def act(n, l, b=6):
        return S(wrap(btn(n, l, BTN, size=18, tname="Txt_" + n[4:]), None, 56), pad=(0, 0, 0, b), h="Fill")
    actions = vbox("Row_ActionsBody", [act("Btn_Dock", "Dock"), act("Btn_Undock", "Undock"),
                                       act("Btn_Refuel", "Refuel"), act("Btn_Unload", "Unload"), act("Btn_Collect", "Collect ore", 0)])
    actions.var = False
    xb = S(wrap(btn("Btn_RemoteAll", "X", BTN, size=14, tname="Txt_RemoteAll"), 36), pad=(6, 0, 0, 0), h="Fill", v="Fill")
    fleet = vbox("Row_FleetBody", [S(tb("Lbl_RemoteControl", "Remote Control", 15, var=False), pad=(0, 0, 0, 6)),
                                   hrow("Row_RemoteRow", [S(select_field("Remote"), fill=1, h="Fill", v="Fill"), xb])])
    fleet.var = False
    fleet_box = N("VerticalBox", "Pnl_Fleet", [S(window("Fleet", tb("Lbl_TitleFleet", "Fleet Control", 20, var=False), fleet), h="Fill")])
    left = vbox("Row_LeftCol", [
        S(fleet_box, pad=(0, 0, 0, 16), h="Fill"),
        S(window("Control", tb("Lbl_TitleControl", "Control", 20, var=False), control), pad=(0, 0, 0, 16), h="Fill"),
        S(window("Actions", tb("Lbl_TitleActions", "Actions", 20, var=False), actions), h="Fill"),
    ])
    left.var = False

    # RIGHT: Status
    pct = hrow("Row_CargoHead", [S(tb("Lbl_Cargo", "Cargo", 15, var=False), fill=1),
                                 tb("Txt_CargoPct", "0%", 15)])
    pb = N("ProgressBar", "PB_Cargo", props=[("Percent", "0.0"), ("FillColorAndOpacity", GREEN)])
    head = hrow("Row_StatusHead", [S(tb("Txt_StatusAction", "Idle", 20), fill=1, v="Center"),
                                   S(tb("Txt_Eta", "ETA: --", 15), h="Right", v="Center")])
    status = vbox("Row_StatusBody", [
        S(head, pad=(0, 0, 0, 6), h="Fill"),
        S(tb("Txt_NextState", "Next: -", 15), pad=(0, 0, 0, 6)),
        S(var_on(hrow("Row_StatusMine", [
            S(tb("Txt_StatusMineLbl", "Mining", 15), pad=(0, 0, 8, 0), v="Center"),
            S(wrap(N("Image", "Img_StatusOre", props=[("Brush", "(ImageSize=(X=24.000000,Y=24.000000),DrawAs=Image,TintColor=(SpecifiedColor=%s))" % WHITE_RAW), ("Visibility", "Hidden")]), 24, 24), pad=(0, 0, 6, 0), v="Center"),
            S(tb("Txt_StatusMine", "-", 15), v="Center")])), pad=(0, 0, 0, 14), h="Fill"),
        S(pct, pad=(0, 0, 0, 6), h="Fill"),
        S(wrap(pb, None, 18), pad=(0, 0, 0, 14), h="Fill"),
        S(tb("Txt_Speed", "Speed: 0 m/s", 15), pad=(0, 0, 0, 14)),
        S(N("TextBlock", "Txt_Status", props=[("Font", "(Size=14)"), ("Text", 'INVTEXT("")'),
                                              ("AutoWrapText", "True")]), h="Fill"),
    ])
    status.var = False
    fleet_rows = [S(ship_card("Fleet", i, False, True), pad=(0, 0, 8, 6), h="Fill") for i in range(1, FN + 1)]
    fleet_sb = N("ScrollBox", "Row_FleetScroll", fleet_rows, var=False, props=[("ConsumeMouseWheel", "WhenScrollingPossible"), ("AlwaysShowScrollbar", "True")])
    fleet_body = vbox("Row_FleetListBody", [S(fleet_sb, fill=1, h="Fill", v="Fill")])
    fleet_body.var = False
    fleet_list = N("VerticalBox", "Pnl_FleetList", [S(window("FleetList", tb("Lbl_TitleFleetList", "Ships", 20, var=False), fleet_body, vfill=True), fill=1, h="Fill", v="Fill")],
                   props=[("Visibility", "Collapsed")])
    status_col = vbox("Row_StatusCol", [S(window("Status", tb("Lbl_TitleStatus", "Status", 20, var=False), status), pad=(0, 0, 0, 16), h="Fill"),
                                        S(fleet_list, fill=1, h="Fill", v="Fill")])
    status_col.var = False

    close = S(wrap(close_cap(), 40, None), v="Fill")   # header-row height, flush right

    cols = N("HorizontalBox", "Row_Cols", [
        S(left, fill=0.2, pad=(0, 0, 16, 0), h="Fill", v="Top"),
        S(window("Center", tb("Txt_Title", "Mining Unit", 20), center, right=close,
                 vfill=True), fill=0.55, pad=(0, 0, 16, 0), h="Fill", v="Fill"),
        S(status_col, fill=0.25, h="Fill", v="Fill"),
    ], var=False)
    panel = S(N("Border", "Panel", [S(cols, h="Fill", v="Fill")],
                props=[("BrushColor", CLEAR), ("Padding", pad(20, 20, 20, 20))]))
    panel.sp.append(("LayoutData", ANCH))
    mslot = S(modal())
    mslot.sp.append(("LayoutData", ANCH))
    root = N("CanvasPanel", "Root", [panel, mslot])
    apply_tips(root)
    return root


PKG = "/Game/MiningUnitUI/WBP_MiningUnit.WBP_MiningUnit:WidgetTree"


def ref(cls, name):
    return ('"/Script/UMG.%s' + chr(39) + '%s' + chr(39) + '"') % (cls, name)


def exp(cls, name, owner=None):
    return ('ExportPath="/Script/UMG.%s' + chr(39) + '%s.%s%s' + chr(39) + '"') % (cls, PKG, (owner + ".") if owner else "", name)


import itertools
PAIR_ID = itertools.count(1)


def emit(n, out, root=True):
    """Format = real UE 5.7.4 copy of Border>ListView: slot declared (empty) then defined inside the
    panel, THEN Slots(i)=, then other props; WidgetSlotPair after the root-most widget; children top-level after."""
    slot = SLOT.get(n.cls)
    lines = ['Begin Object Class=/Script/UMG.%s Name="%s" %s' % (n.cls, n.name, exp(n.cls, n.name))]
    if slot:
        for c in n.kids:
            sn = "S_" + c.name
            lines += ['   Begin Object Class=/Script/UMG.%s Name="%s" %s' % (slot, sn, exp(slot, sn, n.name)),
                      "   End Object",
                      '   Begin Object Name="%s" %s' % (sn, exp(slot, sn, n.name)),
                      "      Parent=%s" % ref(n.cls, n.name),
                      "      Content=%s" % ref(c.cls, c.name)]
            lines += ["      %s=%s" % kv for kv in c.sp]
            lines.append("   End Object")
        for i, c in enumerate(n.kids):
            lines.append("   Slots(%d)=%s" % (i, ref(slot, "S_" + c.name)))
    if n.var:
        lines.append("   bIsVariable=True")
    props = list(n.props)
    if n.cls == "Border" and n.kids and not any(k == "Padding" for k, _ in props):
        # editor ignores a BorderSlot's padding on paste; Border.Padding is the real property
        sp = dict(n.kids[0].sp)
        if "Padding" in sp:
            props.append(("Padding", sp["Padding"]))
    for k, v in props:
        lines.append("   %s=%s" % (k, v))
    lines.append("End Object")
    if not root and n.sp:
        # editor restores slot props from WidgetSlotPair (a slot defined inside the parent loses e.g. Padding)
        i = next(PAIR_ID)
        lines += ['Begin Object Class=/Script/UMGEditor.WidgetSlotPair Name="WidgetSlotPair_%d" '
                  'ExportPath="/Script/UMGEditor.WidgetSlotPair' % i + chr(39) + '/Engine/Transient.WidgetSlotPair_%d' % i + chr(39) + '"',
                  '   WidgetName="%s"' % n.name]
        for j, (k, v) in enumerate(n.sp):
            lines += ['   SlotPropertyNames(%d)="%s"' % (j, k), '   SlotPropertyValues(%d)="%s"' % (j, v)]
        lines.append("End Object")
    if root:
        lines += ['Begin Object Class=/Script/UMGEditor.WidgetSlotPair Name="WidgetSlotPair_0" '
                  'ExportPath="/Script/UMGEditor.WidgetSlotPair' + chr(39) + '/Engine/Transient.WidgetSlotPair_0' + chr(39) + '"',
                  '   WidgetName="%s"' % n.name, "End Object"]
    out.append("\n".join(lines))
    if slot:
        for c in n.kids:
            emit(c, out, root=False)


# ---------- validation (text-level, no UE) ----------

BEGIN = re.compile(r'^Begin Object (?:Class=/Script/(\w+)\.(\w+) )?Name="(\w+)"')
REF = re.compile(r"/Script/UMG\.(\w+)'(\w+)'")
FIXED_NAMES = {"Root", "Panel", "TabBar", "PageSwitcher", "Row_ModalCardScroll", "Row_ModalScroll", "Row_LogScroll", "Row_StatusMine", "Row_DocksScroll"} | {"Row_ModalOres_%d" % i for i in range(1, SEL_N + 1)} | {"Row_CardHdr_%d" % i for i in range(1, CN + 1)}


def spec_names():
    txt = open(SPEC, encoding="utf-8").read()
    names = set(FIXED_NAMES)
    for m in re.finditer(r"(?<!\w)((?:Btn|Txt|Edt|Chk|Cmb|Lst|Page|PB|Sel|Pnl|Opt|Img|Bdr)_[A-Za-z0-9_<>]+)(\*?)", txt):
        tok, star = m.group(1), m.group(2)
        if "<m>" in tok:   # dock list rows: <ok> = ore key, <m> = 1..UDOCK_N
            for m_ in range(1, UDOCK_N + 1):
                s = tok.replace("<m>", str(m_))
                names.update([s.replace("<ok>", k_) for k_ in ORES] if "<ok>" in s else [s])
            continue
        if "<ok>" in tok:
            names.update(tok.replace("<ok>", k_) for k_ in ORES)
            continue
        if "<f>" in tok and "<r>" in tok:
            names.update(tok.replace("<f>", str(f_)).replace("<r>", str(r_)) for f_ in range(1, FN + 1) for r_ in range(1, LOG_ROWS + 1))
            continue
        if "<f>" in tok:
            names.update(tok.replace("<f>", str(f_)) for f_ in range(1, FN + 1))
            continue
        if "<l>" in tok:
            names.update(tok.replace("<l>", str(l_)) for l_ in range(1, LOG_N + 1))
            continue
        if "<I>" in tok:
            names.update(tok.replace("<I>", i_) for i_ in ITEM_ICON_IDS)
            continue
        if "<c>" in tok:
            names.update(tok.replace("<c>", str(c_)) for c_ in range(1, CN + 1))
            continue
        if "<n>" in tok or "<r>" in tok:
            rs = range(1, ROW_N + 1) if "<r>" in tok else [None]
            ns = range(1, SEL_N + 1) if "<n>" in tok else [None]
            for r_ in rs:
                for n_ in ns:
                    s = tok
                    if r_ is not None:
                        s = s.replace("<r>", str(r_))
                    if n_ is not None:
                        s = s.replace("<n>", str(n_))
                    if "<k>" in s:
                        names.update(s.replace("<k>", str(k_)) for k_ in range(1, 7))
                    else:
                        names.add(s)
            continue
        if star or "<n>" in tok or ("<" in tok and "<K>" not in tok):
            continue
        if "<K>" in tok:
            ks = ORES if "Unload" in tok else FUELS
            names.update(tok.replace("<K>", k) for k in ks)
        else:
            names.add(tok)
    return names


def validate(text):
    errs = []
    used = set(re.findall(r"ResourceObject=Texture2D'\"([^\"]+)\"'", text))
    for p in sorted(used - ART_PATHS - ITEM_PATHS - {STRIPE_PATH} - ICON_PATHS):
        errs.append("texture not in allow-list: %s" % p)
    if not ART and used:
        errs.append("flat mode must not reference textures (%d refs)" % len(used))
    if ART and not used:
        errs.append("art mode has no texture refs")
    objs = {}
    stack = []
    refs = []
    defined = set()
    pairs = []
    for ln, raw in enumerate(text.split("\n"), 1):
        line = raw.strip()
        if not line:
            continue
        m = BEGIN.match(line)
        if m:
            pkg, cls, name = m.groups()
            if cls is None:  # define block of an already declared object
                if name not in objs:
                    errs.append("define of undeclared %s (line %d)" % (name, ln))
                    objs[name] = {"cls": "?", "owner": None, "props": {}}
                defined.add(name)
            elif pkg == "UMGEditor":
                pairs.append(name)
                objs[name] = {"cls": cls, "owner": None, "props": {}, "pair": True}
            else:
                if name in objs:
                    errs.append("duplicate object name %s (line %d)" % (name, ln))
                owner = stack[-1][0] if stack else None
                objs[name] = {"cls": cls, "owner": owner, "props": {}}
                if owner is None:
                    defined.add(name)
            stack.append((name, objs[name]["cls"]))
            continue
        if line == "End Object":
            if not stack:
                errs.append("End Object without Begin (line %d)" % ln)
            else:
                stack.pop()
            continue
        if not stack:
            errs.append("text outside object (line %d)" % ln)
            continue
        cur = stack[-1][0]
        key, _, val = line.partition("=")
        m2 = REF.search(val)
        if key.startswith("Slots(") or key in ("Parent", "Content"):
            if not m2:
                errs.append("bad ref %s in %s (line %d)" % (key, cur, ln))
            else:
                refs.append((cur, key, m2.group(1), m2.group(2)))
                if key.startswith("Slots(") and m2.group(2) not in defined:
                    errs.append("Slots ref to %s before its define block (line %d)" % (m2.group(2), ln))
        else:
            objs[cur]["props"][key] = val
    if stack:
        errs.append("unclosed object %s" % stack[-1][0])
    pw = [objs[p]["props"].get("WidgetName") for p in pairs]
    if pw.count('"Root"') != 1 or len(pw) != len(set(pw)):
        errs.append("expected exactly one WidgetSlotPair for Root and one per widget at most, got %s" % pw[:5])
    objs = {k: v for k, v in objs.items() if not v.get("pair")}

    widgets = {k: v for k, v in objs.items() if v["owner"] is None}
    slots = {k: v for k, v in objs.items() if v["owner"] is not None}
    incoming = {k: 0 for k in widgets}
    slot_refs = {k: 0 for k in slots}
    children = {k: [] for k in widgets}
    if "Root" not in widgets:
        errs.append("no top-level Root")
    for owner, key, rcls, target in refs:
        if target not in objs:
            errs.append("unresolved %s -> %s in %s" % (key, target, owner))
            continue
        if objs[target]["cls"] != rcls:
            errs.append("class mismatch %s -> %s (%s vs %s)" % (key, target, rcls, objs[target]["cls"]))
        if key.startswith("Slots("):
            if objs[owner]["cls"] not in SLOT or target not in slots or objs[target]["owner"] != owner:
                errs.append("Slots ref %s in %s not a nested slot of it" % (target, owner))
            elif objs[target]["cls"] != SLOT[objs[owner]["cls"]]:
                errs.append("slot %s wrong class for %s" % (target, objs[owner]["cls"]))
            else:
                slot_refs[target] += 1
        elif key == "Parent":
            if target != objs[owner]["owner"]:
                errs.append("Parent of %s points to %s, expected %s" % (owner, target, objs[owner]["owner"]))
        elif key == "Content":
            if target not in widgets:
                errs.append("Content of %s is not a top-level widget (%s)" % (owner, target))
            else:
                incoming[target] += 1
                children[objs[owner]["owner"]].append(target)
    for s, n in slot_refs.items():
        if n != 1:
            errs.append("slot %s referenced %d times" % (s, n))
    for s, v in slots.items():
        keys = {k for (o, k, _, _) in refs if o == s}
        if not {"Parent", "Content"} <= keys:
            errs.append("slot %s missing Parent or Content" % s)
    for w, n in incoming.items():
        if w == "Root":
            if n:
                errs.append("Root has a parent")
        elif n != 1:
            errs.append("widget %s has %d parents" % (w, n))
    for w, v in widgets.items():
        kids = children[w] if w in children else []
        if v["cls"] in MAX_KIDS and len(kids) > MAX_KIDS[v["cls"]]:
            errs.append("%s (%s) has %d children" % (w, v["cls"], len(kids)))
    # reachability from Root
    seen, todo = set(), ["Root"]
    while todo:
        w = todo.pop()
        if w in seen:
            continue
        seen.add(w)
        todo += children.get(w, [])
    if seen != set(widgets):
        errs.append("unreachable widgets: %s" % sorted(set(widgets) - seen))
    # vars / names
    var_names = {k for k, v in widgets.items() if v["props"].get("bIsVariable") == "True"}
    for k, v in widgets.items():
        if k not in var_names and not (k.startswith("Lbl_") or k.startswith("Row_")):
            errs.append("%s is not a variable but is not Lbl_/Row_" % k)
    expected = spec_names()
    for miss in sorted(expected - set(widgets)):
        errs.append("SPEC name missing: %s" % miss)
    for extra in sorted(var_names - expected):
        errs.append("variable not in SPEC: %s" % extra)
    for miss in sorted(expected - var_names):
        errs.append("SPEC name not Is Variable: %s" % miss)
    # colors in range
    for k, v in objs.items():
        for pk, pv in v["props"].items():
            if pv.startswith("(R="):
                vals = [float(x.split("=")[1]) for x in pv.strip("()").split(",")]
                if any(not 0.0 <= x <= 1.0 for x in vals):
                    errs.append("color out of range %s.%s" % (k, pk))
    # every EditableTextBox: black text (style lives in WidgetStyle)
    for blk in re.finditer(r'^Begin Object Class=/Script/UMG\.EditableTextBox Name="(\w+)".*?^End Object', text, re.S | re.M):
        if "ColorAndOpacity=(SpecifiedColor=(R=0.000000,G=0.000000,B=0.000000" not in blk.group(0):
            errs.append("EditableTextBox %s not black text" % blk.group(1))
        if "ColorUseRule=UseColor_Specified" not in blk.group(0):
            errs.append("EditableTextBox %s missing ColorUseRule" % blk.group(1))
    stats = {"widgets": len(widgets), "slots": len(slots), "variables": len(var_names),
             "spec_names": len(expected)}
    return errs, stats


def main():
    global ART
    ART = "--flat" not in sys.argv[1:]
    out_path = OUT if ART else OUT_FLAT
    tree = build_tree()
    out = []
    emit(tree, out)
    text = "\n".join(out) + "\n"
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    small = []
    emit(N("Border", "TestBorder", [tb("TestText", "hello")], var=False), small)
    with open(os.path.join(os.path.dirname(OUT), "test_small.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(small) + "\n")
    errs, stats = validate(text)
    print("wrote %s (mode: %s)" % (out_path, "art" if ART else "flat"))
    print("stats: %s" % stats)
    if errs:
        print("VALIDATION FAILED:")
        for e in errs:
            print("  - " + e)
        return 1
    print("VALIDATION OK (structure, refs, names, Is Variable vs SPEC.md, texture allow-list)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
