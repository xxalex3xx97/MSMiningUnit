"""Check every WBP_BlueprintUnit contract name exists and is a variable. Prints MISSING/NOTVAR lines, then CHECK-DONE."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uew import *
wbp = U.load_asset(ASSETS + '/WBP_BlueprintUnit')
U.BlueprintEditorLibrary.compile_blueprint(wbp)
cdo = U.get_default_object(wbp.generated_class())  # a widget is a variable iff the generated class has a property of its name
names = ['Panel', 'Txt_Title', 'Btn_Close', 'Txt_Close', 'Txt_Status', 'Btn_Import', 'Txt_Import', 'Btn_Build', 'Txt_Build',
         'Txt_CostHdr', 'Scr_Cost', 'Txt_CostEmpty', 'Txt_UnitInvTitle', 'Scr_UnitInv', 'Txt_PlayerInvTitle', 'Host_PlayerInv']
for i in range(1, 41):
    names += ['Row_Cost_%d' % i, 'Img_Cost_%d' % i, 'Txt_CostHave_%d' % i, 'Txt_CostName_%d' % i]
miss = nonvar = 0
for n in names:
    w = ELib.find_source_widget_by_name(wbp, n)
    if not w:
        print('MISSING', n); miss += 1
    else:
        try:
            cdo.get_editor_property(n)
        except Exception:
            print('NOTVAR', n); nonvar += 1
print('CHECK-DONE n_missing=%d n_notvar=%d total=%d' % (miss, nonvar, len(names)))
