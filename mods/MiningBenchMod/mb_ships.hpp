// FILE: mods/MiningBenchMod/mb_ships.hpp
// WHAT: bench-ship helpers: a log dedupe helper for skipped orders, the ships.txt writer, and the ship scan that logs bench ships.
// WHY separate: ship-level maintenance and scanning form one unit, kept apart from the order, steering and command code.
// DEPENDS ON: mb_common (g_skip, g_sig_ships_file, trim, get_dll_dir, write_atomic, log_line, u8), mb_config (g_cfg), mb_ue (count_bench, read_struct_int, obj_id, obj_display), mb_mscore (ms_mining_caps, ms_presence_step), mb_logic (mining_read_from_caps).
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

    // destroyed-ship debounce state (see dead_scan_next in mb_logic.hpp)
    // g_dead_n: ship id -> consecutive scans with the destroyed signal. g_bench_prev: ids with a bench in the last scan (an id missing from the list stays until confirmed).
    std::map<int, int> g_dead_n;
    std::set<int> g_bench_prev;
    // laser gate state (MSCore_presence_step is stateless, so the mod keeps it): g_laser_listed = ships accepted on the last scan, g_laser_bad = consecutive absent reads.
    std::set<int> g_laser_listed;
    std::map<int, int> g_laser_bad;

    // WHAT: Health read of one ship under SEH (ctx for seh_run). ok = the property was read.
    struct HpCtx { UObject* ship; int32_t hp; bool ok; };
    void read_hp_fn(void* p)
    {
        auto* c = static_cast<HpCtx*>(p);
        c->ok = get_int_prop(c->ship, STR("Health"), c->hp);
    }

    // WHAT: MSCore_ship_mining_caps of one ship under SEH (ctx for seh_run). caps stays -1 on a fault.
    struct CapsCtx { UObject* ship; int caps; };
    void caps_read_fn(void* p)
    {
        auto* c = static_cast<CapsCtx*>(p);
        c->caps = ms_mining_caps(c->ship);
    }

    // WHAT: scans all player ships. Each ship with bench blocks gets a log line (id, class, bench counts, DockID, MiningBeaconID), and an entry in bench_ships. Then writes ships.txt.
    //       Destroyed ships go to dead_ids instead, and are written with has-bench 0.
    //       Signals: A = hull Health reads 0 (SHIP_DEAD_SCANS scans in a row). B = the ship had a bench last scan and is missing from FindAllOf
    //       (SHIP_MISSING_SCANS scans in a row; skipped while the list is empty: save load).
    // WHY: a destroyed ship's UObject can still be listed and readable (log 2026-11-04: ships kept showing in the bench and the selector), so the scan decides, not the object's alive().
    // WHY bench_prev: the scan is the only place that sees the list each time; the mining state is cleared by the caller (mine_set_bench -> mine_forget_ship).
    // It calls FindAllOf on every call, so the caller runs it on a timer, not every frame.
    //       Laser gate: a ship with the bench is in the mining list while MSCore_presence_step accepts its read (bench_read: bench + MiningLaser). An unknown laser read (caps -1) accepts the bench alone (gate_accept HOLD), listed or not. A ship the gate dropped goes to lost_laser_ids with its bench flag (only that list halts).
    //       No laser check (MSCore export missing, load failed or module off): the released behaviour, the bench block alone.
    void scan_and_log(std::vector<std::pair<UObject*, int>>& bench_ships, std::vector<int>& dead_ids, std::vector<std::pair<int, bool>>& lost_laser_ids)
    {
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        std::string txt;
        std::set<int> seen, bench_now;
        const int lavail = ms_laser_avail();   // 0 = check, else bench-only; resolved once
        if (lavail) ms_laser_fallback_log(lavail == 1 ? STR("load failed") : STR("missing export"));
        for (UObject* ship : ships)
        {
            if (!ship) continue;
            int bb = count_bench(ship, STR("BuildingBlocks"));
            int so = count_bench(ship, STR("Subobjects"));
            const bool bench = bb > 0 || so > 0;
            int32_t id = obj_id(ship);
            // a Health read that faults counts as signal A (a destroyed object can fault on read)
            HpCtx hc{ship, 0, false};
            const bool hp_fault = id > 0 && !seh_run(read_hp_fn, &hc);
            if (hp_fault && g_dead_n[id] == 0) log_line(STR("Health read faulted ship=") + std::to_wstring(id) + STR(" ") + seh_describe(g_seh));
            const bool hull0 = id > 0 && (hp_fault || (hc.ok && hc.hp <= 0));
            std::wstring name = obj_display(ship);
            if (name.empty()) name = ship->GetName();
            if (id > 0)
            {
                seen.insert(id);
                g_dead_n[id] = dead_scan_next(g_dead_n[id], hull0);
            }
            const bool dead = id > 0 && dead_confirmed(g_dead_n[id]);
            // laser gate: id > 0 and live only. id <= 0 = never accepted, no state. Dead = state erased. A bench == false read never erases state (it is absent, debounced).
            bool has = false;
            if (id > 0 && !dead)
            {
                int caps = -1;
                if (lavail == 0 && bench) { CapsCtx cc{ship, -1}; caps = seh_run(caps_read_fn, &cc) ? cc.caps : -1; }
                // no bench = read 0 (absent): debounced by presence_step, so CHECK, never HOLD (HOLD accepts a bench ship)
                LaserGateMode mode = LASER_CHECK;
                if (lavail != 0) mode = LASER_FALLBACK_BENCH_ONLY;
                else if (bench) mode = laser_gate_mode(true, caps);
                if (mode == LASER_FALLBACK_BENCH_ONLY)
                {
                    if (caps == -2) ms_laser_fallback_log(STR("module off"));
                    has = bench;   // released behaviour: the bench block alone, no laser state, no halt
                    g_laser_listed.erase(id); g_laser_bad.erase(id);
                }
                else
                {
                    const bool listed = g_laser_listed.count(id) > 0;
                    int bad = g_laser_bad[id];
                    const bool pres = ms_presence_step(bad, listed ? 1 : 0, bench_read(bench, caps), &bad) == 1;   // read -1 (HOLD) = state held
                    has = gate_accept(bench, mode, listed, pres);
                    g_laser_bad[id] = bad;
                    if (has) g_laser_listed.insert(id);
                    else
                    {
                        g_laser_listed.erase(id);
                        if (listed) lost_laser_ids.emplace_back(id, bench);   // the caller logs the one drop line (mine_set_bench)
                    }
                }
            }
            if (id > 0) txt += std::to_string(id) + "|" + u8(name) + "|" + (has && !dead ? "1" : "0") + "\n";
            if (dead)
            {
                if (g_dead_n[id] == SHIP_DEAD_SCANS) log_line(STR("DEAD signal ship=") + std::to_wstring(id) + STR(" hull Health 0 on ") + std::to_wstring(SHIP_DEAD_SCANS) + STR(" scans"));
                g_laser_listed.erase(id); g_laser_bad.erase(id);   // dead-confirmed: state erased
                dead_ids.push_back(id);
                continue;
            }
            if (!has) continue;
            if (id > 0) bench_now.insert(id);
            int32_t dock = -1, beacon = -1;
            read_struct_int(ship, STR("AutoMiningData"), STR("DockID"), dock);
            read_struct_int(ship, STR("AutoMiningData"), STR("MiningBeaconID"), beacon);
            if (g_debug) Output::send<LogLevel::Normal>(
                    STR("[MiningBenchMod] ship id={} name={} class={} bench_blocks(BuildingBlocks)={} bench_blocks(Subobjects)={} DockID={} MiningBeaconID={}\n"),
                    id, ship->GetName(), ship->GetClassPrivate() ? ship->GetClassPrivate()->GetName() : std::wstring(STR("?")), bb, so, dock, beacon);
            bench_ships.emplace_back(ship, id);
        }
        // signal B: had a bench last scan, not in the list now
        for (int id : g_bench_prev)
        {
            if (seen.count(id)) continue;
            if (ships.empty()) { bench_now.insert(id); continue; }   // empty list = not filled yet (save load): no signal
            const int n = dead_scan_next(g_dead_n[id], true);
            g_dead_n[id] = n;
            if (dead_confirmed(n, SHIP_MISSING_SCANS))
            {
                if (n == SHIP_MISSING_SCANS) log_line(STR("DEAD signal ship=") + std::to_wstring(id) + STR(" missing from the ship list on ") + std::to_wstring(SHIP_MISSING_SCANS) + STR(" scans"));
                dead_ids.push_back(id);
                g_dead_n.erase(id);
                g_laser_listed.erase(id); g_laser_bad.erase(id);
            }
            else bench_now.insert(id);
        }
        g_bench_prev = bench_now;
        // laser state: erased only for ships not seen in this scan (dead-confirmed ones are erased above)
        for (auto it = g_laser_listed.begin(); it != g_laser_listed.end();) it = seen.count(*it) ? std::next(it) : g_laser_listed.erase(it);
        for (auto it = g_laser_bad.begin(); it != g_laser_bad.end();) it = seen.count(it->first) ? std::next(it) : g_laser_bad.erase(it);
        write_ships_file(txt);
        if (g_debug) Output::send<LogLevel::Normal>(STR("[MiningBenchMod] scan: ships={} with_bench={}\n"), ships.size(), bench_ships.size());
    }
}
