"""Import OUR hazard-stripe tile as /Game/MiningUnitUI/T_HazardStripes, plus T_Icon* row icons (from scripts\\icons) (shipped in the pak).

Generated 32x32 seamless 45-degree bands: black #151515 / yellow #E8B800, 8 px per band.
T_UI_* and T_Item_* in /Game/MiningUnitUI/Art are our shipped copies of game textures (see docs/ASSETS.md); the hazard stripes and T_Icon* are generated. Compression UserInterface2D, no mips, nearest filter, never stream.

Run headless (editor must not hold this project open):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash

Skips the asset if it exists. Log: %TEMP%\\mu_probe\\make_assets.log
"""
import os
import shutil
import struct
import traceback
import zlib
import unreal

DEST = "/Game/MiningUnitUI"
NAME = "T_HazardStripes"
ICON_DIR = os.path.join(unreal.Paths.project_dir(), "scripts", "icons")
ART_DEST = DEST + "/Art"
ART_SRC = os.path.join(unreal.Paths.project_dir(), "art")
UI_ART = ["T_UI_Header_L", "T_UI_Header_Bar", "T_UI_Header_R"]
ITEM_IDS = ["CobaltOre", "Graphite", "IridiumOre", "NickelOre", "Silica", "TitaniumOre", "WaterIce",
            "EnrichedCobaltOre", "EnrichedNickelOre", "EnrichedTitaniumOre", "EnrichedFuel", "HydrogenFuel", "WarpFuel"]
ICONS = ["T_IconUp", "T_IconDown", "T_IconTrash", "T_IconShip", "T_IconPin", "T_IconStIdle", "T_IconStMining",
         "T_IconStDocking", "T_IconStUndocking", "T_IconStRefueling", "T_IconStUnloading"]
SIZE = 32
BAND = 8
BLACK = (21, 21, 21)
YELLOW = (232, 184, 0)
TMP = os.path.join(os.environ.get("TEMP", "."), "mu_probe")
LOG_PATH = os.path.join(TMP, "make_assets.log")
LOG = []


def log(msg):
    LOG.append(str(msg))
    unreal.log("[make_assets] " + str(msg))


def png_stripes():
    """seamless: (x+y)//BAND parity repeats every SIZE px in x and y (SIZE = 4 bands)."""
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    rows = []
    for y in range(SIZE):
        row = bytearray(b"\x00")
        for x in range(SIZE):
            row += bytes(YELLOW if ((x + y) // BAND) % 2 else BLACK)
        rows.append(bytes(row))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def enum_val(enum_cls, want):
    key = want.replace("_", "").lower()
    for a in dir(enum_cls):
        if a.replace("_", "").lower() == key:
            return getattr(enum_cls, a)
    raise RuntimeError("enum member not found: %s" % want)


def set_prop(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
    except Exception as e:
        log("  set %s failed: %r" % (prop, e))


def art_pngs():
    """(asset name, source PNG) for our game-texture copies."""
    pairs = [(n, os.path.join(ART_SRC, "textures", n + ".png")) for n in UI_ART]
    pairs += [("T_Item_" + i, os.path.join(ART_SRC, "items", "ITEM_" + i + ".png")) for i in ITEM_IDS]
    return pairs


def main():
    log("start")
    os.makedirs(TMP, exist_ok=True)
    for name in [NAME] + ICONS:
        one(name)
    for n, p in art_pngs():
        one(n, p, ART_DEST)


def one(name, src=None, dest=DEST):
    path = dest + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        log("exists, settings re-applied (no import): " + path)
        apply_settings(unreal.EditorAssetLibrary.load_asset(path), name)
        return
    png = os.path.join(TMP, name + ".png")
    if src:
        if not os.path.exists(src):
            raise RuntimeError("missing PNG: " + src)
        shutil.copyfile(src, png)
    elif name == NAME:
        with open(png, "wb") as f:
            f.write(png_stripes())
    else:
        shutil.copyfile(os.path.join(ICON_DIR, name + ".png"), png)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", False)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        raise RuntimeError("import produced no asset: " + path)
    apply_settings(tex, name)
    log("created: %s" % path)


def apply_settings(tex, name):
    set_prop(tex, "compression_settings", enum_val(unreal.TextureCompressionSettings, "TC_EditorIcon"))
    set_prop(tex, "mip_gen_settings", enum_val(unreal.TextureMipGenSettings, "TMGS_NoMipmaps"))
    set_prop(tex, "filter", enum_val(unreal.TextureFilter, "TF_Nearest" if name == NAME else "TF_Bilinear"))
    set_prop(tex, "srgb", True)
    set_prop(tex, "never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)


try:
    main()
except Exception as exc:
    log("FAILED: %r" % exc)
    log(traceback.format_exc())
finally:
    os.makedirs(TMP, exist_ok=True)
    with open(LOG_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(LOG) + "\n")
