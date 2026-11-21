# WHAT: checks the ShipControl submodule against deps.lock and the consumer's needs (stdlib only, no game).
# Usage: python -I tests\test_deps.py   -> one "ok <check>" line per pass, "FAIL <check>: why" + exit 1 on any failure.
import glob
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SC = os.path.join(ROOT, "external", "ShipControl")
fails = []


def rd(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def git(*args, cwd=ROOT):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True).stdout


def load_lock():
    lock = {}
    for line in rd("deps.lock").splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            lock[k.strip()] = v.strip()
    return lock


LOCK = load_lock()


def c_gitmodules():
    txt = rd(".gitmodules")
    assert re.search(r"^\s*path\s*=\s*external/ShipControl\s*$", txt, re.M), "no path = external/ShipControl"
    assert re.search(r"^\s*url\s*=\s*https://github\.com/xxalex3xx97/ShipControl\.git\s*$", txt, re.M), "wrong url"


def c_pin():
    pin = LOCK.get("shipcontrol_commit", "")
    assert re.fullmatch(r"[0-9a-f]{40}", pin), "deps.lock shipcontrol_commit is not 40-hex"
    out = git("ls-tree", "HEAD", "external/ShipControl")
    m = re.match(r"160000 commit ([0-9a-f]{40})\texternal/ShipControl", out)
    assert m, "external/ShipControl is not a gitlink (mode 160000) in HEAD: %r" % out.strip()
    assert m.group(1) == pin, "gitlink %s != deps.lock %s" % (m.group(1), pin)
    if os.path.exists(os.path.join(SC, ".git")):
        head = git("rev-parse", "HEAD", cwd=SC).strip()
        assert head == pin, "checked-out submodule %s != pin %s (run git submodule update)" % (head, pin)


def c_version():
    p = os.path.join(SC, "sc_version.hpp")
    assert os.path.isfile(p), "external/ShipControl/sc_version.hpp missing (submodule not checked out?)"
    m = re.search(r'kVersion\s*=\s*"([^"]+)"', open(p, encoding="utf-8").read())
    assert m, "kVersion not found"
    assert m.group(1) == LOCK.get("shipcontrol_version"), "kVersion %s != deps.lock %s" % (m.group(1), LOCK.get("shipcontrol_version"))


def c_api_level():
    p = os.path.join(SC, "sc_api.h")
    assert os.path.isfile(p), "external/ShipControl/sc_api.h missing"
    m = re.search(r"#define\s+SC_API_LEVEL\s+(\d+)", open(p, encoding="utf-8").read())
    assert m, "SC_API_LEVEL not found in sc_api.h"
    level = int(m.group(1))
    assert level == int(LOCK.get("sc_api_level", "-1")), "SC_API_LEVEL %d != deps.lock sc_api_level" % level
    # the consumer's needed level = the highest "api_level >= N" gate in its ShipControl wrapper
    need = []
    for f in glob.glob(os.path.join(ROOT, "mods", "*", "mb_shipctl.hpp")):
        need += [int(x) for x in re.findall(r"api_level\s*>=\s*(\d+)", open(f, encoding="utf-8").read())]
    assert need, "no api_level >= N gate found in mods/*/mb_shipctl.hpp"
    assert level >= max(need), "SC_API_LEVEL %d < consumer needs %d" % (level, max(need))


def c_build_files():
    txt = rd("build.ps1")
    files = re.findall(r"external\\ShipControl\\([A-Za-z0-9_.]+)", txt)
    assert files, "build.ps1 copies nothing from external\\ShipControl"
    for f in files:
        assert os.path.isfile(os.path.join(SC, f)), "build.ps1 needs external/ShipControl/%s (missing)" % f
    assert not re.search(r"mods[\\/]ShipControl", txt), "build.ps1 still refers to mods\\ShipControl"


def c_no_inline_copy():
    assert not os.path.exists(os.path.join(ROOT, "mods", "ShipControl")), "mods/ShipControl still exists"
    bad = [p for p in git("ls-files").splitlines() if re.match(r"^mods/[^/]+/sc_[^/]*\.(hpp|h)$", p)]
    assert not bad, "committed ShipControl headers next to mod sources: %s" % bad


def c_requires():
    m = re.fullmatch(r"xxalex3xx97/ShipControl@(\d+)\.(\d+)\.(\d+)", LOCK.get("requires_min", ""))
    assert m, "requires_min is not xxalex3xx97/ShipControl@X.Y.Z"
    ver = re.search(r'kVersion\s*=\s*"(\d+)\.(\d+)\.(\d+)"', open(os.path.join(SC, "sc_version.hpp"), encoding="utf-8").read())
    assert ver, "ShipControl kVersion not readable"
    assert tuple(map(int, m.groups())) <= tuple(map(int, ver.groups())), "requires_min is newer than the pinned ShipControl"
    # a build that writes mscore.mod requires= must read that version from the submodule
    bp = rd("build.ps1")
    if "requires=" in bp:
        assert "external\\ShipControl\\sc_version.hpp" in bp, "build.ps1 writes requires= but does not read external\\ShipControl\\sc_version.hpp"


CHECKS = [
    ("gitmodules", c_gitmodules),
    ("pin", c_pin),
    ("version", c_version),
    ("api_level", c_api_level),
    ("build_files", c_build_files),
    ("no_inline_copy", c_no_inline_copy),
    ("requires_min", c_requires),
]

for name, fn in CHECKS:
    try:
        fn()
        print("ok %s" % name)
    except Exception as e:  # a failed check is reported, the rest still run
        fails.append(name)
        print("FAIL %s: %s" % (name, e))

raise SystemExit(1 if fails else 0)
