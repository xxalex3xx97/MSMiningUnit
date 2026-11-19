"""Fleet UI static checks for MiningUnitUI (plain CPython, no UE, no game needed).

  python -I tests\\test_fleet_ui.py     prints "ok name" / "FAIL name: detail" per check, exit 1 on any FAIL

Name check (f): a name in fleet_ui.lua M.NAMES passes when it is a quoted literal in one of the three python
generators, or when it is built from a role/class token (Defend, Haul, Attack, Escort, All, Miner, ...) and every
alphabetic piece of its prefix (before the token) and the token itself appear in the generator text.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCR = os.path.join(ROOT, "mods", "MiningUnitUI", "Scripts")
UE = os.path.join(ROOT, "ue", "MiningUnitUI", "scripts")
STUBS = os.path.join(SCR, "fleet_stubs.lua")
FLEET = os.path.join(SCR, "fleet_ui.lua")
CORE = os.path.join(SCR, "ui_core.lua")
LOADER = os.path.join(SCR, "wbp_loader.lua")
PY_FILES = [os.path.join(UE, n) for n in ("add_fleet_tab.py", "add_role_pages.py", "uicomp.py")]
NEW_FILES = [STUBS, FLEET, CORE] + PY_FILES
GEN_TOKENS = ("Defend", "Haul", "Attack", "Escort", "All", "Miner", "Hauler", "Fighter", "Frigate", "Capital")
SECRET_RE = re.compile(r"TODO|FIXME|XXX|(?i:password|passwd|api[_-]?key|secret|private key)"
                       r"|gh[pousr]_[A-Za-z0-9]{20,}|sk-[A-Za-z0-9]{20,}")
FAILS = []


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def report(name, problems):
    if problems:
        print("FAIL " + name + ": " + "; ".join(problems))
        FAILS.append(name)
    else:
        print("ok " + name)


def check_demo_flag():  # (a)
    ok = re.search(r"^fleet\.FLEET_DEMO\s*=\s*false\s*$", read(STUBS), re.M)
    report("fleet_demo_false", [] if ok else ["no 'fleet.FLEET_DEMO = false' literal in fleet_stubs.lua"])


def check_no_game_paths():  # (b)
    bad = [os.path.basename(p) for p in (STUBS, FLEET, CORE) if "/Game/" in read(p)]
    report("no_game_paths", ["'/Game/' found in " + ", ".join(bad)] if bad else [])


def check_stub_calls():  # (c)
    stubs = read(STUBS)
    defined = set(re.findall(r"^function fleet\.([A-Za-z_]\w*)\(", stubs, re.M))
    data = set(re.findall(r"^fleet\.([A-Za-z_]\w*)\s*=", stubs, re.M))
    called = set(re.findall(r"\bFS\.([A-Za-z_]\w*)", read(FLEET)))
    missing = sorted(n for n in called if n not in defined and n not in data)
    report("stub_calls_defined", ["fleet_ui.lua uses stub(s) not in fleet_stubs.lua: " + ", ".join(missing)]
           if missing else [])


def check_stub_log():  # (d)
    txt = read(STUBS)
    problems = []
    if '"FLEET STUB"' not in txt:
        problems.append('no "FLEET STUB" literal')
    if not re.search(r"print\(TAG \.\. ", txt):
        problems.append("stub() does not print through TAG")
    report("stub_log_tag", problems)


def check_walk_names():  # (e)
    m = re.search(r"local WALK_NAMES = \{(.*?)\}", read(LOADER), re.S)
    body = m.group(1) if m else ""
    want = ["Btn_TabFleet", "Txt_TabFleet", "Btn_TabRole", "Txt_TabRole",
            "Pnl_PageFleet", "Pnl_PageRole", "Pnl_FleetLeft", "Pnl_FleetStatus"]
    missing = [n for n in want if not re.search(r"\b%s\s*=\s*true" % n, body)]
    report("walk_names", ["not in WALK_NAMES: " + ", ".join(missing)] if missing else [])


def generated_ok(name, py_text):
    for tok in GEN_TOKENS:
        if tok in name:
            prefix = name.split(tok, 1)[0]
            pieces = re.findall(r"[A-Z][a-z]*|[a-z]+", prefix)
            return tok in py_text and all(p in py_text for p in pieces)
    return False


def role_key_ok(name, py_text):
    # Sel_/Edt_/Chk_Rl<letter><key>: allowed only when add_role_pages has <key> under that role letter
    m = re.match(r"(?:Sel|Edt|Chk)_Rl([DHAE])([A-Za-z]\w*)$", name)
    if not m:
        return False
    letter, key = m.groups()
    head = re.search(r"\('\w+', '%s', \[" % letter, py_text)
    if not head:
        return False
    nxt = re.search(r"\('\w+', '[A-Z]', \[", py_text[head.end():])
    section = py_text[head.end():head.end() + nxt.start()] if nxt else py_text[head.end():]
    return ("('%s', '" % key) in section


def check_names_in_py():  # (f)
    m = re.search(r"M\.NAMES = \{(.*?)\n\}", read(FLEET), re.S)
    names = re.findall(r"\"([A-Za-z_]\w*)\"", m.group(1)) if m else []
    py_text = "\n".join(read(p) for p in PY_FILES)
    quoted = set(re.findall(r"['\"]([A-Za-z_]\w*)['\"]", py_text))
    literal = [n for n in names if n in quoted]
    pattern = [n for n in names if n not in quoted and (generated_ok(n, py_text) or role_key_ok(n, py_text)
               or (n.endswith("_Cell") and n[:-len("_Cell")] in quoted))]
    missing = [n for n in names if n not in quoted and n not in pattern]
    print("info names_in_py: %d in M.NAMES, %d literal, %d pattern-allowed (%s)"
          % (len(names), len(literal), len(pattern), ", ".join(pattern) or "-"))
    report("names_in_py", ["missing from add_fleet_tab/add_role_pages/uicomp: " + ", ".join(missing)]
           if (missing or not names) else [])


def check_no_secrets():  # (g)
    bad = []
    for p in NEW_FILES:
        for i, line in enumerate(read(p).splitlines(), 1):
            if SECRET_RE.search(line):
                bad.append("%s:%d" % (os.path.basename(p), i))
    report("no_todo_secrets", ["hit in " + ", ".join(bad)] if bad else [])


if __name__ == "__main__":
    for chk in (check_demo_flag, check_no_game_paths, check_stub_calls, check_stub_log,
                check_walk_names, check_names_in_py, check_no_secrets):
        try:
            chk()
        except Exception as e:  # a crashed check is a failure, never a silent pass
            report(chk.__name__, ["exception: %r" % (e,)])
    sys.exit(1 if FAILS else 0)
