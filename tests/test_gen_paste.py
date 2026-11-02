"""gen_paste checks for the MiningUnitUI paste tree (plain CPython, no UE needed).

  python -I tests\\test_gen_paste.py    prints "ok name" / "FAIL name" per test, exit 1 on any failure

Checks: build_tree + emit give text that validate() accepts (art and flat mode), and validate() rejects a texture
that is not on the allow-list. Loads gen_paste.py by path, so nothing is written to paste\\.
"""
import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GP = os.path.join(ROOT, "ue", "MiningUnitUI", "scripts", "gen_paste.py")


def load_gp():
    spec = importlib.util.spec_from_file_location("gen_paste", GP)
    gp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gp)
    return gp


def paste_text(gp, art):
    gp.ART = art
    out = []
    gp.emit(gp.build_tree(), out)
    return "\n".join(out) + "\n"


def test_art_tree_validates():
    gp = load_gp()
    errs, _ = gp.validate(paste_text(gp, True))
    assert errs == [], errs[:5]


def test_flat_tree_validates():
    gp = load_gp()
    errs, _ = gp.validate(paste_text(gp, False))
    assert errs == [], errs[:5]


def test_validate_rejects_bad_texture():
    gp = load_gp()
    text = paste_text(gp, True) + "ResourceObject=Texture2D'\"/Game/Other/Bad\"'\n"
    errs, _ = gp.validate(text)
    assert any("not in allow-list" in e for e in errs), errs[:5]


if __name__ == "__main__":
    rc = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("ok", name)
            except AssertionError as e:
                rc = 1
                print("FAIL", name, e)
    sys.exit(rc)
