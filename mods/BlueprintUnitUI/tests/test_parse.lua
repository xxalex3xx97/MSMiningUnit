-- Parse self-check for bpu_window.lua (pure functions, no game). Run from anywhere with stock Lua 5.4:
--   lua mods/BlueprintUnitUI/tests/test_parse.lua   (exits non-zero on failure)
local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./"
local BPU = dofile(here .. "../Scripts/bpu_window.lua")
local P = BPU._parse

local STATUS = "done|place 2, keep 0, remove 2 (refund LightweightFrame x2, Servomotor x10), recipe changes 0\n"
  .. "seq=17948000\nblueprint=Starter Station\nstation=41238\nplace=2\nkeep=0\nremove=2\nrecipe=0\nport=0\nskipped=\n"
  .. "creative=0\nremoves=BP_ContainerObject:1,BP_ContainerObject_2:1"
local COST = "ITEM_LightweightFrame|0|2|-2\nITEM_Servomotor|0|10|-10\n"

local fails = 0
local function check(label, got, want)
  local good = got == want
  if not good then fails = fails + 1 end
  print((good and "PASS " or "FAIL ") .. label .. ": " .. tostring(got) .. (good and "" or ("  (want " .. tostring(want) .. ")")))
end

-- status
local st, text, kv = P.status(STATUS)
check("state", st, "done")
check("text", text, "place 2, keep 0, remove 2 (refund LightweightFrame x2, Servomotor x10), recipe changes 0")
check("kv.seq", kv.seq, "17948000")
check("kv.blueprint", kv.blueprint, "Starter Station")
check("kv.skipped (empty)", kv.skipped, "")
check("kv.remove", kv.remove, "2")

-- removes prettifier
check("removes", P.removes(kv.removes), "1 container, 1 large container")
check("removes plural", P.removes("BP_ContainerObject:2,BP_ContainerObject_2:3"), "2 containers, 3 large containers")
check("removes belt/splitter", P.removes("BP_ConveyorBeltObject:3,BP_Splitter:1"), "3 belts, 1 splitter")

-- cost
local list, bad = P.cost(COST)
check("cost rows", #list, 2)
check("cost bad lines", bad, 0)
check("cost[1]", list[1].name .. " " .. list[1].need .. " " .. list[1].refund .. " " .. list[1].net,
  "ITEM_LightweightFrame 0 2 -2")
check("cost[2]", list[2].name .. " " .. list[2].need .. " " .. list[2].refund .. " " .. list[2].net,
  "ITEM_Servomotor 0 10 -10")
local _, bad2 = P.cost("ITEM_X|1|2\n")
check("cost malformed", bad2, 1)

-- seq match: equal, or equal modulo 1e8 (13-digit ms form)
check("seq equal", P.seqMatch("17948000", 17948000), true)
check("seq 13-digit tail", P.seqMatch("1794017948000", 17948000), true)
check("seq mismatch", P.seqMatch("14132618", 17948000), false)
check("seq missing", P.seqMatch("", 17948000), false)

if fails > 0 then print(fails .. " FAILED"); os.exit(1) end
print("ALL PASS")
