// FILE: mods/MiningBenchMod/mb_ships.hpp
// WHAT: bench-ship helpers: a log dedupe helper for skipped orders, the ships.txt writer, and the ship scan that logs bench ships.
// WHY separate: ship-level maintenance and scanning form one unit, kept apart from the order, steering and command code.
// DEPENDS ON: mb_common (g_skip, g_sig_ships_file, trim, get_dll_dir, write_atomic, log_line, u8), mb_config (g_cfg), mb_ue (count_bench, read_struct_int, obj_id, obj_display).
// USED BY: mb_dockjob (skip), dllmain.cpp (scan_and_log).
// GAME FACTS:
//   VERIFIED (logs): the bench block of ship 41709 sits in Subobjects, with BuildingBlocks=0 (M1 scan).
//   ASSUMPTION: a ship has the bench when count_bench finds it in BuildingBlocks or in Subobjects.
#pragma once

namespace
{
    // ---------------- skipped-order log ----------------

    // WHAT: logs an "order skipped" line for a ship, but only when the reason differs from the last one logged for that ship (g_skip map).
    // WHY: the order path runs on every tick, so without this dedupe the same skip reason would repeat in the log every tick.
    void skip(int sid, const std::wstring& why)
    {
        auto& last = g_skip[sid];
        if (last == why) return;
        last = why;
        if (g_debug) Output::send<LogLevel::Normal>(STR("[MiningBenchMod] order skipped ship={} reason: {}\n"), sid, why);
    }

    // ---------------- ship list for the Lua window ----------------

    // WHAT: writes ships.txt ("id|name|1 or 0 for has-bench" per ship), but only when the text changed (g_sig_ships_file).
    // WHY: the Lua Mining window cannot enumerate ships itself, so it reads this file and shows only the rows with has-bench = 1. Write errors are logged, not thrown.
    void write_ships_file(const std::string& txt)
    {
        if (txt == g_sig_ships_file) return;
        try { write_atomic(get_dll_dir() / "ships.txt", txt); g_sig_ships_file = txt; log_line(STR("[M2.5] ships.txt written: rows=") + std::to_wstring(std::count(txt.begin(), txt.end(), '\n'))); }
        catch (const std::exception& e) { log_line(STR("ships.txt write failed: ") + widen(e.what())); }
        catch (...) { log_line(STR("ships.txt write failed (unknown)")); }
    }

    // ---------------- ship scan ----------------

    // WHAT: scans all player ships. Each ship with bench blocks gets a log line (id, class, bench counts, DockID, MiningBeaconID), and an entry in bench_ships. Then writes ships.txt.
    // WHY: a ship has the bench when BuildingBlocks or Subobjects holds it (ship 41709 has it in Subobjects, see the file banner). It calls FindAllOf on every call, so the caller runs it on a timer, not every frame.
    void scan_and_log(std::vector<std::pair<UObject*, int>>& bench_ships)
    {
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        std::string txt;
        for (UObject* ship : ships)
        {
            if (!ship) continue;
            int bb = count_bench(ship, STR("BuildingBlocks"));
            int so = count_bench(ship, STR("Subobjects"));
            bool has = bb > 0 || so > 0;
            int32_t id = obj_id(ship);
            std::wstring name = obj_display(ship);
            if (name.empty()) name = ship->GetName();
            if (id > 0) txt += std::to_string(id) + "|" + u8(name) + "|" + (has ? "1" : "0") + "\n";
            if (!has) continue;
            int32_t dock = -1, beacon = -1;
            read_struct_int(ship, STR("AutoMiningData"), STR("DockID"), dock);
            read_struct_int(ship, STR("AutoMiningData"), STR("MiningBeaconID"), beacon);
            if (g_debug) Output::send<LogLevel::Normal>(
                    STR("[MiningBenchMod] ship id={} name={} class={} bench_blocks(BuildingBlocks)={} bench_blocks(Subobjects)={} DockID={} MiningBeaconID={}\n"),
                    id, ship->GetName(), ship->GetClassPrivate() ? ship->GetClassPrivate()->GetName() : std::wstring(STR("?")), bb, so, dock, beacon);
            bench_ships.emplace_back(ship, id);
        }
        write_ships_file(txt);
        if (g_debug) Output::send<LogLevel::Normal>(STR("[MiningBenchMod] scan: ships={} with_bench={}\n"), ships.size(), bench_ships.size());
    }
}
