"""Build /Game/MiningUnitUI/WBP_MiningUnit per SPEC.md.

Run headless (editor must not hold this project open):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash

Refuses to run if the asset already exists (no overwrite).
Log: %TEMP%\mu_probe\make_wbp.log
"""
import os
import traceback
import unreal

ASSET_NAME = "WBP_MiningUnit"
PKG_PATH = "/Game/MiningUnitUI"
LOG_PATH = os.path.join(os.environ.get("TEMP", "."), "mu_probe", "make_wbp.log")
LOG = []


def log(msg):
    LOG.append(str(msg))
    unreal.log("[make_wbp] " + str(msg))


def srgb_to_lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lin_color(hex_rgb, alpha=1.0):
    h = hex_rgb.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    return unreal.LinearColor(srgb_to_lin(r), srgb_to_lin(g), srgb_to_lin(b), alpha)


PANEL = lin_color("2B2B2B", 0.85)
TAB_OFF = lin_color("1E1E1E", 0.95)
TAB_ON = lin_color("D49A00", 1.0)
BTN = lin_color("4A4A4A", 1.0)
DANGER = lin_color("9C2222", 1.0)
WHITE = unreal.LinearColor(1, 1, 1, 1)


def build(wbp):
    tree = wbp.get_editor_property("widget_tree")
    if tree is None:
        raise RuntimeError("widget_tree not readable from python")
    wt = tree

    def new_widget(cls_name, name):
        cls = unreal.load_class(None, "/Script/UMG." + cls_name)
        try:
            return unreal.new_object(cls, outer=wt, name=name)
        except TypeError:
            return unreal.new_object(cls, wt, name)

    def mk(cls_name, name, parent=None, var=True, color=None):
        w = new_widget(cls_name, name)
        w.set_editor_property("is_variable", var)
        if color is not None:
            if cls_name == "Border":
                w.set_editor_property("brush_color", color)
            elif cls_name == "Button":
                w.set_editor_property("background_color", color)
        if parent is not None:
            parent.add_child(w)
        return w

    def text(name, s, parent, size=13, var=True, color=None):
        tb = mk("TextBlock", name, parent, var)
        try:
            tb.set_editor_property("text", unreal.Text(s))
        except Exception:
            tb.set_editor_property("text", s)
        f = tb.get_editor_property("font")
        f.set_editor_property("size", size)
        tb.set_editor_property("font", f)
        return tb

    def button(name, label, parent, color=BTN, var=True):
        b = mk("Button", name, parent, var, color)
        text("Lbl_" + name, label, b, 13, False)
        return b

    def row(parent, label_text, label_name, value_cls, value_name):
        hb = mk("HorizontalBox", "Row_" + value_name, parent, False)
        text(label_name, label_text, hb)
        return mk(value_cls, value_name, hb)

    def check(name, label, parent):
        c = mk("CheckBox", name, parent, True)
        text("Lbl_" + name, label, c, 13, False)
        return c

    # --- root / panel / body
    root = mk("CanvasPanel", "Root", None, True)
    tree.set_editor_property("root_widget", root)
    panel = mk("Border", "Panel", root, True, PANEL)
    body = mk("VerticalBox", "Body", panel, True)

    text("TitleText", "Mining Unit - <ship name>", body, 18)

    # --- tab bar
    tabbar = mk("HorizontalBox", "TabBar", body, True)
    tab_names = [("Btn_TabLoop", "Txt_TabLoop", "Loop"),
                 ("Btn_TabShip", "Txt_TabShip", "Ship"),
                 ("Btn_TabItems", "Txt_TabItems", "Items"),
                 ("Btn_TabOperation", "Txt_TabOperation", "Operation")]
    for i, (bn, tn, label) in enumerate(tab_names):
        b = mk("Button", bn, tabbar, True, TAB_ON if i == 0 else TAB_OFF)
        text(tn, label, b, 13, True)

    # --- page switcher
    switcher = mk("WidgetSwitcher", "PageSwitcher", body, True)
    page_loop = mk("VerticalBox", "Page_Loop", switcher, True)
    page_ship = mk("VerticalBox", "Page_Ship", switcher, True)
    page_items = mk("VerticalBox", "Page_Items", switcher, True)
    page_op = mk("VerticalBox", "Page_Operation", switcher, True)
    switcher.set_editor_property("active_widget_index", 0)

    # Page_Loop
    text("Txt_LoopHeader", "Loop", page_loop, 14)
    button("Btn_LoopStart", "Start loop", page_loop)
    button("Btn_LoopStop", "Stop loop", page_loop)
    button("Btn_LoopStartAll", "Start all", page_loop)
    button("Btn_LoopStopAll", "Stop all", page_loop)
    text("Txt_LoopStatus", "", page_loop)
    text("Txt_LoopRows", "", page_loop)

    # Page_Ship
    row(page_ship, "Dock filter", "Txt_ShipDockFilterLabel", "EditableTextBox", "Edt_DockFilter")
    mk("VerticalBox", "Lst_Docks", page_ship, True)
    row(page_ship, "Refuel dock", "Txt_ShipRefuelDockLabel", "TextBlock", "Txt_ShipRefuelDock")

    # Page_Items (no action button: unconfirmed in SPEC)
    row(page_items, "Resource", "Txt_ItemsFilterLabel", "EditableTextBox", "Edt_ItemsFilter")
    check("Chk_ItemsShowAll", "Show all", page_items)
    mk("VerticalBox", "Lst_Items", page_items, True)
    row(page_items, "Amount", "Txt_ItemsAmountLabel", "EditableTextBox", "Edt_ItemsAmount")

    # Page_Operation
    row(page_op, "Radius", "Txt_OpRadiusLabel", "EditableTextBox", "Edt_Radius")
    row(page_op, "Mining distance", "Txt_OpMiningDistLabel", "EditableTextBox", "Edt_MiningDistance")
    row(page_op, "Max radius (global)", "Txt_OpMaxRadiusLabel", "EditableTextBox", "Edt_MaxRadius")
    row(page_op, "Idle seconds", "Txt_OpIdleLabel", "EditableTextBox", "Edt_IdleSeconds")
    row(page_op, "Wait seconds", "Txt_OpWaitLabel", "EditableTextBox", "Edt_WaitSeconds")
    check("Chk_Enabled", "Enabled", page_op)
    check("Chk_Repeat", "Repeat", page_op)
    for k in ["Deuterium", "Hydrogen", "Warp"]:
        fr = mk("HorizontalBox", "Row_Fuel" + k, page_op, False)
        check("Chk_Fuel%sOn" % k, k, fr)
        mk("Image", "Img_Fuel" + k, fr, True)
        text("Lbl_Fuel%sThreshold" % k, "threshold", fr, 13, False)
        mk("EditableTextBox", "Edt_Fuel%sThreshold" % k, fr, True)
        text("Lbl_Fuel%sAmount" % k, "amount", fr, 13, False)
        mk("EditableTextBox", "Edt_Fuel%sAmount" % k, fr, True)
    for k, label in [("Cobalt", "Cobalt"), ("Graphite", "Graphite"), ("Iridium", "Iridium"),
                     ("Nickel", "Nickel"), ("Silica", "Silica"), ("Titanium", "Titanium"),
                     ("WaterIce", "Water Ice")]:
        check("Chk_Unload" + k, label, page_op)
        mk("Image", "Img_Unload" + k, page_op, True)
    button("Btn_Apply", "Apply", page_op)
    button("Btn_ApplyGlobal", "Apply as global default", page_op)

    # footer
    text("Txt_Status", "", body, 13)
    close = mk("Button", "Btn_Close", body, True, DANGER)
    text("Txt_Close", "Close", close, 13, True)

    log("widgets built")
    return wt


def walk(w, depth=0, out=None):
    out = [] if out is None else out
    if w is None:
        return out
    try:
        var = w.get_editor_property("is_variable")
    except Exception:
        var = "?"
    out.append("%s%s [%s] var=%s" % ("  " * depth, w.get_name(), w.get_class().get_name(), var))
    if hasattr(w, "get_children_count"):
        for i in range(w.get_children_count()):
            walk(w.get_child_at(i), depth + 1, out)
    return out


def main():
    log("start")
    if unreal.EditorAssetLibrary.does_asset_exist(PKG_PATH + "/" + ASSET_NAME):
        raise RuntimeError("asset already exists, refusing to overwrite: " + PKG_PATH + "/" + ASSET_NAME)

    factory = unreal.WidgetBlueprintFactory()
    try:
        factory.set_editor_property("parent_class", unreal.UserWidget.static_class())
    except Exception as e:
        log("parent_class not settable on factory (default is UserWidget): %r" % e)
    wbp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME, PKG_PATH, unreal.WidgetBlueprint, factory)
    if wbp is None:
        raise RuntimeError("create_asset returned None")
    log("created " + wbp.get_path_name())

    try:
        build(wbp)
    except Exception as e:
        log("BUILD BLOCKED (widget tree not reachable from python): %r" % e)

    unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
    log("compiled, generated class: " + str(wbp.generated_class()))
    ok = unreal.EditorAssetLibrary.save_loaded_asset(wbp)
    log("saved: %s" % ok)

    # verify by reloading
    re = unreal.EditorAssetLibrary.load_asset(PKG_PATH + "/" + ASSET_NAME)
    log("reloaded: %s" % re)
    try:
        tree = re.get_editor_property("widget_tree")
        for line in walk(tree.get_editor_property("root_widget")):
            log("TREE " + line)
    except Exception as e:
        log("verify tree: %r" % e)
    log("done")


try:
    main()
except Exception as exc:
    log("FAILED: %r" % exc)
    log(traceback.format_exc())
finally:
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w") as f:
        f.write("\n".join(LOG) + "\n")
