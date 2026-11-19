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
SCR = os.path.join(ROOT, "mods", "FleetUI", "Scripts")
MINING = os.path.join(ROOT, "mods", "MiningUnitUI", "Scripts")
UE = os.path.join(ROOT, "ue", "MiningUnitUI", "scripts")
STUBS = os.path.join(SCR, "fleet_stubs.lua")
FLEET = os.path.join(SCR, "fleet_ui.lua")
CORE = os.path.join(SCR, "ui_core.lua")
LOADER = os.path.join(MINING, "wbp_loader.lua")
MINING_MAIN = os.path.join(MINING, "main.lua")
FLEET_MAIN = os.path.join(SCR, "main.lua")
FLEET_WIN = os.path.join(SCR, "fleet_win.lua")
UICOMP = os.path.join(UE, "uicomp.py")
PY_FILES = [os.path.join(UE, n) for n in ("add_fleet_window.py", "add_role_pages.py", "uicomp.py")]
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


def check_mining_untouched():  # (e)
    bad = []
    for p in (LOADER, MINING_MAIN):
        txt = read(p)
        for tok in ("FLEET", "fleet_ui", "ui_core", "Btn_TabFleet"):
            if tok in txt:
                bad.append("%s has %s" % (os.path.basename(p), tok))
    report("mining_untouched", bad)


def check_fleet_main():  # (h)
    txt = read(FLEET_MAIN)
    report("fleet_main_keys", [k for k in ("Key.F5", "Key.ESCAPE") if k not in txt])


def check_close_name():  # (i)
    m = re.search(r"M\.NAMES = \{(.*?)\n\}", read(FLEET), re.S)
    ok = m and "Btn_FlClose" in m.group(1)
    report("close_btn_in_names", [] if ok else ["Btn_FlClose not in fleet_ui.lua M.NAMES"])


def check_fleet_win():  # (j)
    problems = []
    if not os.path.exists(FLEET_WIN):
        problems.append("fleet_win.lua missing")
    elif "wbp_loader" in read(FLEET_WIN):
        problems.append("fleet_win.lua mentions wbp_loader")
    report("fleet_win", problems)


def check_img_guard():  # (k)
    txt = read(UICOMP)
    m = re.search(r"def _img\(.*?(?=\ndef )", txt, re.S)
    ok = m and "ValueError" in m.group(0)
    report("img_guard", [] if ok else ["uicomp.py _img has no ValueError guard"])


def check_img_icons():  # (l)
    bad = []
    for p in PY_FILES:
        for i, line in enumerate(read(p).splitlines(), 1):
            if line.lstrip().startswith("def "):
                continue
            # 4th positional arg (icon) of _img(w, name, parent, icon, ...) must not be None
            if re.search(r"_img\(\s*[^,()]+,\s*[^,]+,\s*[^,]+,\s*None\s*[,)]", line):
                bad.append("%s:%d" % (os.path.basename(p), i))
    report("img_icon_not_none", ["_img( with icon None at " + ", ".join(bad)] if bad else [])


def generated_ok(name, py_text):
    for tok in GEN_TOKENS:
        if tok in name:
            prefix = name.split(tok, 1)[0]
            pieces = re.findall(r"[A-Z][a-z]*|[a-z]+", prefix)
            return tok in py_text and all(p in py_text for p in pieces)
    return False


def loop_ok(name, py_text):
    # names the python builds in loops: Opt_Modal_%d / Txt_ModalC1_%d, Txt_FlStat<Next|Speed> via 'Txt_FlStat' + k
    if re.match(r"(Opt_Modal|Txt_ModalC1)_\d+$", name):
        return ("'%s_%%d'" % name.rsplit("_", 1)[0]) in py_text
    return name in ("Txt_FlStatNext", "Txt_FlStatSpeed") and "'Txt_FlStat' + k" in py_text


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
    pattern = [n for n in names if n not in quoted and (generated_ok(n, py_text) or role_key_ok(n, py_text) or loop_ok(n, py_text)
               or (n.endswith("_Cell") and n[:-len("_Cell")] in quoted))]
    missing = [n for n in names if n not in quoted and n not in pattern]
    print("info names_in_py: %d in M.NAMES, %d literal, %d pattern-allowed (%s)"
          % (len(names), len(literal), len(pattern), ", ".join(pattern) or "-"))
    report("names_in_py", ["missing from add_fleet_window/add_role_pages/uicomp: " + ", ".join(missing)]
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
                check_mining_untouched, check_fleet_main, check_close_name, check_fleet_win,
                check_img_guard, check_img_icons, check_names_in_py, check_no_secrets):
        try:
            chk()
        except Exception as e:  # a crashed check is a failure, never a silent pass
            report(chk.__name__, ["exception: %r" % (e,)])
    sys.exit(1 if FAILS else 0)
