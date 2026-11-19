# FILE: tests/test_abi.py
# WHAT: checks that every SC_ export declared in mods/ShipControl/sc_api.h is defined in dllmain.cpp with SHIP_CONTROL_API, and the reverse.
# WHY: the C ABI is the contract other mods load by name; a renamed or missing export breaks them at runtime, and CI builds no DLLs (static checks only).
# RUN: python -I tests\test_abi.py (run.ps1 runs it). Prints "ok ..." / "FAIL ..." lines; exit 0 = all ok.
import os
import re
import sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mods", "ShipControl")
with open(os.path.join(root, "sc_api.h"), encoding="utf-8") as f:
    header = set(re.findall(r"^\s*(?:int|void)\s+(SC_\w+)\s*\(", f.read(), re.M))
with open(os.path.join(root, "dllmain.cpp"), encoding="utf-8") as f:
    exported = set(re.findall(r"SHIP_CONTROL_API\s+[\w:*<> ]+?\b(SC_\w+)\s*\(", f.read()))

fails = 0
if not header:
    print("FAIL no SC_ declarations found in sc_api.h")
    fails += 1
for name in sorted(header - exported):
    print(f"FAIL {name} declared in sc_api.h but not exported from dllmain.cpp")
    fails += 1
for name in sorted(exported - header):
    print(f"FAIL {name} exported from dllmain.cpp but not declared in sc_api.h")
    fails += 1
if not fails:
    print(f"ok {len(header)} SC_ exports match sc_api.h and dllmain.cpp")
sys.exit(1 if fails else 0)
