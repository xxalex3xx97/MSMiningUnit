// FILE: mods/MiningBenchMod/mb_docktags.hpp
// WHAT: dock name tags ([TITANIUM], [MISC], [AUTO-FILTER], [PARKING], [REFUEL], [HYDROGEN], [DEUTERIUM], [WARP]) read from the dock walk. dock_tags_refresh fills g_dock_tags and writes DockTags.txt and DockFilter.txt next to the dll when their content changed.
//   DockTags.txt  line: "<id>|<ores>|<misc>|<autofilter>|<park>|<refuel>|<fuelmask>"  (7 columns, refuel 0/1, fuelmask H=1 D=2 W=4, 0 = all; ores = comma keys, misc/autofilter/park 0/1; read by the Lua window)
//   DockFilter.txt line: "<id>|<ITEM_xxx>"  (read by MiningUnitUI main.lua, which calls MSCore set_dock_filter)
//   ShipTags.txt  line: "<ship id>|<ITEM_x:amt,ITEM_y:amt>"  (ships whose name carries >=1 ore tag; amt 0 = until full; tag order; written by ship_tags_write; read by the Lua Items tab)
// WHY separate: the tag walk is its own rule set; it stays out of write_lists (docks.txt).
// DEPENDS ON: mb_common (alive, log_line, get_dll_dir, write_atomic, u8, widen, STR), mb_ue (obj_id), mb_docks (dock_name), mb_logic (DockTag, dock_tag_parse, autofilter_pairs, g_cfg, g_ship_kv, apply_kv, ORE_KEY, ORE_ITEM).
// USED BY: dllmain.cpp (tick_work, after write_lists).
// PONYTAIL: one extra FindAllOf per 20 s; fold into write_lists later.
#pragma once

namespace
{
    // WHAT: every tick_work (walk throttled to 20 s) walks the docks, fills g_dock_tags (alive docks only, no ship/NPC/abandoned docks); then writes DockTags.txt and DockFilter.txt next to the dll when their content changed.
    void dock_tags_refresh()
    {
        try
        {
            static ULONGLONG last_walk = 0;
            if (last_walk == 0 || GetTickCount64() - last_walk >= 20000)
            {
                last_walk = GetTickCount64();
                std::vector<UObject*> v;
                UObjectGlobals::FindAllOf(STR("Dock"), v);
                std::vector<DockTag> tags;
                for (UObject* o : v)
                {
                    if (!alive(o)) continue;
                    auto* cls = o->GetClassPrivate();
                    const std::wstring cn = cls ? std::wstring(cls->GetName()) : std::wstring();
                    if (cn.find(STR("Abandoned")) != std::wstring::npos || cn.find(STR("NPC")) != std::wstring::npos || cn.find(STR("ShipBlockDock")) != std::wstring::npos) continue;
                    DockTag t = dock_tag_parse(obj_id(o), u8(dock_name(o)));
                    if (dock_tag_invalidate(t)) { static std::set<int> warned; if (warned.insert(t.id).second) log_line(STR("DOCK TAGS: dock ") + std::to_wstring(t.id) + STR(" has several ore tags: ignored (one ore tag per dock)")); }
                    if (t.id > 0 && (t.misc || t.autof || t.park || t.refuel || !t.ores.empty())) tags.push_back(t);
                }
                std::sort(tags.begin(), tags.end(), [](const DockTag& a, const DockTag& b) { return a.id < b.id; });
                g_dock_tags = tags;
            }
            std::string tx, fx;
            for (const DockTag& t : g_dock_tags)
            {
                std::string keys;
                if (!t.misc) for (int i : t.ores) keys += (keys.empty() ? "" : ",") + std::string(ORE_KEY[i]);
                tx += std::to_string(t.id) + "|" + keys + "|" + (t.misc ? "1" : "0") + "|" + (t.autof ? "1" : "0") + "|" + (t.park ? "1" : "0") + "|" + (t.refuel ? "1" : "0") + "|" + std::to_string(t.refuel ? t.fuel : 0) + "\n";
            }
            std::vector<Config> cfgs{g_cfg};
            for (const auto& kv : g_ship_kv) { Config c = g_cfg; apply_kv(c, kv.second, true); cfgs.push_back(c); }
            for (const auto& pr : autofilter_pairs(g_dock_tags, cfgs)) fx += std::to_string(pr.first) + "|" + ORE_ITEM[pr.second] + "\n";
            static bool first = true;
            static std::string last_tx, last_fx;
            const auto dir = get_dll_dir();
            if (first || tx != last_tx) { write_atomic(dir / "DockTags.txt", tx); log_line(STR("DOCK TAGS: ") + std::to_wstring(g_dock_tags.size()) + STR(" tagged docks")); last_tx = tx; }
            if (first || fx != last_fx) { write_atomic(dir / "DockFilter.txt", fx); log_line(STR("AUTO-FILTER pairs: ") + std::to_wstring(std::count(fx.begin(), fx.end(), '\n'))); last_fx = fx; }
            first = false;
        }
        catch (const std::exception& e) { log_line(STR("dock tags failed: ") + widen(e.what())); }
        catch (...) { log_line(STR("dock tags failed (unknown)")); }
    }

    // WHAT: ShipTags.txt next to the dll, one line per ship whose NAME carries >=1 ore tag: "<ship id>|<ITEM_x:amt,ITEM_y:amt>" (amount 0 = until full; tag order). Written only when the text changed (and once at start so a stale file is replaced). Read by the Lua Items tab (locked rows).
    void ship_tags_write()
    {
        try
        {
            std::string tx;
            for (const auto& kv : g_ship_names) { const auto t = ship_tags_parse(kv.second); if (!t.empty()) tx += std::to_string(kv.first) + "|" + ship_tags_items(t) + "\n"; }
            static bool first = true; static std::string last;
            if (first || tx != last) { write_atomic(get_dll_dir() / "ShipTags.txt", tx); log_line(STR("SHIP TAGS: ") + std::to_wstring(std::count(tx.begin(), tx.end(), '\n')) + STR(" tagged ships")); last = tx; }
            first = false;
        }
        catch (const std::exception& e) { log_line(STR("ship tags failed: ") + widen(e.what())); }
        catch (...) { log_line(STR("ship tags failed (unknown)")); }
    }
}
