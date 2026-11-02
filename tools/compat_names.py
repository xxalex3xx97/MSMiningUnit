# Lists every UE name the mod looks up by string (properties, functions, classes, objects) and where it is used.
# Usage: python tools\compat_names.py [--all]   (default: skips mb_testcmd.hpp and mb_devcmd.hpp = dev/test paths)
# Output: one line per name: <name> | <accessor> | <file>:<line>. Compare with the contract table in mb_compat_run.hpp.
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mods", "MiningBenchMod")
ACCESSORS = re.compile(
    r"\b(get_int_prop|get_bool_prop|get_obj_prop|set_int_prop|set_bool_prop|set_obj_prop|sec_axes|pvec_set_axis|pvec_text|"
    r"pvec_axis_ptr|vec3|GetFunctionByNameInChain|FindAllOf|first_of|find_by_id|StaticFindObject|call_\w+|make_cmd|"
    r"GetPropertyByNameInChain|GetValuePtrByPropertyNameInChain|add_input|struct_prop_text|read_text_prop|call_struct_text|"
    r"obj_display|type_text|FindProperty)\b"
)
LIT = re.compile(r'STR\("([^"]+)"\)')
DEV = {"mb_testcmd.hpp", "mb_devcmd.hpp"}


def main():
    include_dev = "--all" in sys.argv
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, "*.hpp")) + [os.path.join(ROOT, "dllmain.cpp")]):
        base = os.path.basename(path)
        if not include_dev and base in DEV:
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                if line.lstrip().startswith("//"):
                    continue
                acc = ACCESSORS.search(line)
                if not acc:
                    continue
                for lit in LIT.findall(line):
                    rows.append((lit, acc.group(1), f"{base}:{n}"))
    seen = set()
    for name, acc, where in rows:
        key = (name, acc)
        if key in seen:
            continue
        seen.add(key)
        print(f"{name} | {acc} | {where}")


if __name__ == "__main__":
    main()
