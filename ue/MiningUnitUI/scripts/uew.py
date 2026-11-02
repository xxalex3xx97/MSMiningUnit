"""Headless UMG authoring helpers (UnrealEditor-Cmd -ExecutePythonScript). Colors/sizes mirror gen_paste.py."""
import unreal

ASSETS = '/Game/MiningUnitUI'
U = unreal
ELib = unreal.EditorUtilityLibrary


def _srgb(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lin(hex_rgb, a=1.0):
    return U.LinearColor(*(_srgb(int(hex_rgb[i:i + 2], 16)) for i in (0, 2, 4)), a)


def slate_color(c):
    return U.SlateColor(specified_color=c)


def round_brush(c, r=6.0):
    b = U.SlateBrush()
    b.set_editor_property('draw_as', U.SlateBrushDrawType.ROUNDED_BOX)
    o = U.SlateBrushOutlineSettings()
    o.set_editor_property('corner_radii', U.Vector4(r, r, r, r))
    o.set_editor_property('rounding_type', U.SlateBrushRoundingType.FIXED_RADIUS)
    b.set_editor_property('outline_settings', o)
    b.set_editor_property('tint_color', slate_color(c))
    return b


def image_brush(path, tint, size):
    b = U.SlateBrush()
    b.set_editor_property('draw_as', U.SlateBrushDrawType.IMAGE)
    v = U.DeprecateSlateVector2D(); v.set_editor_property('x', size); v.set_editor_property('y', size)
    b.set_editor_property('image_size', v)
    b.set_editor_property('tint_color', slate_color(tint))
    b.set_editor_property('resource_object', U.load_asset(path))
    return b


def button_style(normal, hover, pressed):
    s = U.ButtonStyle()
    s.set_editor_property('normal', normal)
    s.set_editor_property('hovered', hover)
    s.set_editor_property('pressed', pressed)
    return s


def new_wbp(name):
    path = '%s/%s' % (ASSETS, name)
    if U.EditorAssetLibrary.does_asset_exist(path):
        U.EditorAssetLibrary.delete_asset(path)
    f = U.WidgetBlueprintFactory()
    f.set_editor_property('parent_class', U.UserWidget)
    return U.AssetToolsHelpers.get_asset_tools().create_asset(name, ASSETS, U.WidgetBlueprint, f)


def add(wbp, cls, name, parent='', var=True):
    w = ELib.add_source_widget(wbp, cls, name, parent)
    try:
        w.set_editor_property('is_variable', var)
    except Exception:
        pass
    return w


def slot(w, pad=None, fill=None, h=None, v=None):
    s = w.slot
    if pad is not None:
        l, t, r, b = pad
        s.set_editor_property('padding', U.Margin(l, t, r, b))
    if fill is not None:
        s.set_editor_property('size', U.SlateChildSize(value=fill, size_rule=U.SlateSizeRule.FILL))
    if h:
        s.set_editor_property('horizontal_alignment', getattr(U.HorizontalAlignment, 'H_ALIGN_' + h.upper()))
    if v:
        s.set_editor_property('vertical_alignment', getattr(U.VerticalAlignment, 'V_ALIGN_' + v.upper()))


def save(wbp):
    U.BlueprintEditorLibrary.compile_blueprint(wbp)
    U.EditorAssetLibrary.save_loaded_asset(wbp)
