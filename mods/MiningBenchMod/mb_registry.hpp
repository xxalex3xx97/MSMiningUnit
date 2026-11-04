// FILE: mods/MiningBenchMod/mb_registry.hpp
// WHAT: game side of the rock registry (stage 1). Seeds the RockReg table (mb_registry_logic.hpp) from the save objects
//       (SmallResourceAsteroidObject), reads radius lazily, resolves loaded actors for rocks near ships, validates save objects near
//       ships, runs the self-check (registry position vs actor K2_GetActorLocation), stats and a MiningRegistry.txt dump.
//       Stage 1 runs ALONGSIDE the old rock code: no ship reads the registry yet (no behaviour change).
// WHY: rocks never move or shrink, so position and radius are read once per rock; the save data holds every rock, loaded or not.
// SAVE FIELDS USED (same as save_rock_nearest in mb_mining.hpp): sys_id(save) = system, get_chunk(save) + get_loc(save) = chunk-relative
//       position (abs m = chunk * 2048 + loc / g_raw_per_m), obj_id(save) = rock id (= actor ObjectId), bAlive (bool),
//       ResourceItems (TArray of ore items), GetRadius() on the save (raw, / g_raw_per_m = m). Not verified: ResourceItems length.
// DELETES: the engine delete listener (rock_drain_dead -> reg_on_dead) only clears the loaded actor handle and marks the rock suspect.
//       A rock is killed only when its SAVE object is gone (checked: suspects, targeted rocks, rocks within REG_NEAR_M of a ship).
//       WHY: the game streams actors in and out; a streamed-out actor is a delete but the rock still exists.
// TICK: registry_tick(dt, ships, targets) from cmd_tick (0.25 s block). Stages: targets/suspects alive check, seed slice (REG_SEED_TICK saves),
//       2 km validation, radius reads (REG_RAD_TICK), actor resolution (rocks within 5 km of a ship), self-check (once after seeding and
//       every 10 min, REG_SELF_N rocks), stats and dump every 60 s. Every stage is skipped when the tick budget (REG_BUDGET_MS) is spent.
// SEED TRIGGERS: first tick, every 10 min (safety refresh), a mining ship in a system no pass has covered yet (checked every 2 s).
//       A pass is one FindAllOf of SmallResourceAsteroidObject (all systems) read in slices.
// UNITS: metres (see mb_registry_logic.hpp). Positions of ships come from MineSt::anchor (uu / MV_UU_PER_M), not a new game call.
// USED BY: mb_rocks.hpp (rock_drain_dead calls reg_on_dead), mb_testcmd.hpp (registry_frame, cmd_tick), mb_devcmd.hpp (reg_dump, reg_query).
// INCLUDE ORDER: after mb_rocks.hpp, before mb_steer.hpp (uses alive_seh, g_rock_by_id, act_frame, item_kind from mb_rocks.hpp).
// OUTPUT: log lines 'REG seed', 'REG selfcheck', 'REG stats', 'REG slow'; file MiningRegistry.txt (overwritten every 60 s and by reg_dump).

#pragma once

#include "mb_registry_logic.hpp"

namespace
{
    // forward declarations of helpers defined in mb_warp.hpp (same unnamed namespace)
    bool get_chunk(UObject* o, int64_t c[3]);
    int sys_id(UObject* o);

    // WHAT: periods and caps of the registry (ms / count).
    constexpr ULONGLONG REG_SEED_REFRESH_MS = 600000;   // safety full pass every 10 min
    constexpr ULONGLONG REG_SELF_MS = 600000;           // self-check period
    constexpr ULONGLONG REG_STATS_MS = 60000;           // stats line and MiningRegistry.txt period
    constexpr ULONGLONG REG_SLOW_LOG_MS = 30000;        // 'REG slow' at most this often
    constexpr ULONGLONG REG_SYS_MS = 2000;              // mining ship system check period (sys_id)
    constexpr size_t REG_SELF_N = 50;                   // rocks compared in one self-check
    constexpr size_t REG_ACT_TICK = 200;                // rocks looked at for actor resolution per tick
    constexpr size_t REG_SUSPECT_TICK = 64;             // suspect rocks checked per tick
    constexpr size_t REG_DUMP_N = 200;                  // rows of MiningRegistry.txt
    constexpr double REG_DUMP_M = 20000.0;              // dump: rocks within this distance of the first ship
    constexpr size_t REG_LIST_N = 50;                   // rows logged by reg_query

    // WHAT: one mining ship as the registry sees it this tick: save id, system id, centre (m).
    struct RegShip { int sid = 0; int sys = 0; double p[3] = {0, 0, 0}; };

    // WHAT: all registry state (heap, never freed: the engine may call the delete listener at shutdown).
    struct RegState
    {
        RockReg tab;
        std::vector<UObject*> save;        // per rock index: save object (alive_seh before each use)
        std::vector<uint64_t> seen;        // per rock index: seed pass that last saw it
        std::vector<size_t> act_list;      // rock indices with a loaded actor handle
        std::vector<size_t> suspect;       // rock indices whose actor was deleted (kill only if the save is gone)
        std::vector<UObject*> seed_list;   // FindAllOf result of the running pass
        size_t seed_cur = 0;
        bool seeding = false;
        uint64_t pass = 0;
        std::set<int> sys_done;            // systems a pass has covered (started with a mining ship there)
        ULONGLONG seed_ms = 0, self_ms = 0, stats_ms = 0, slow_ms = 0, sys_ms = 0;
        bool self_due = false;
        size_t valid_cur = 0;              // rotation of the 2 km validation
        uint64_t ticks = 0;
        double ms_sum = 0.0, ms_worst = 0.0;
        uint64_t stat_ticks = 0;
        std::vector<RegShip> ships;
        std::vector<int> targets;
    };
    RegState& reg_st() { static RegState* s = new RegState; return *s; }

    // WHAT: grows the per-rock side arrays to cover index i.
    void reg_side(RegState& s, size_t i)
    {
        if (s.save.size() <= i) { s.save.resize(i + 1, nullptr); s.seen.resize(i + 1, 0); }
    }

    // WHAT: seeds one save object into the table (skipped when dead, not alive, no system, no chunk, no id).
    void reg_seed_one(RegState& s, UObject* o)
    {
        if (!alive_seh(o)) return;
        bool al = true;
        get_bool_prop(o, STR("bAlive"), al);
        if (!al) return;
        const int sys = sys_id(o);
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        if (sys <= 0 || !get_chunk(o, ch) || !get_loc(o, loc)) return;
        const int id = obj_id(o);
        if (id <= 0) return;
        std::vector<void*> items;
        auto* arr = o->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("ResourceItems"));
        for (int32_t k = 0; arr && k < clamp_tarray_num(arr->Num()); ++k) if ((*arr)[k]) items.push_back((*arr)[k]);
        double c[3] = {0, 0, 0};
        reg_abs_m(ch, loc, g_raw_per_m, c);
        const size_t i = s.tab.add(id, sys, c, items);
        reg_side(s, i);
        s.save[i] = o;
        s.seen[i] = s.pass;
    }

    // WHAT: starts a seed pass: one FindAllOf of SmallResourceAsteroidObject (all systems); the saves are then read in slices.
    // WHY: the save objects exist without a loaded actor; the pass is the only world-wide call of the registry.
    void reg_seed_begin(RegState& s, const std::vector<RegShip>& ships)
    {
        s.seed_ms = GetTickCount64();   // set first: a faulted find is retried at the next refresh, not every tick
        for (const RegShip& sh : ships) if (sh.sys > 0) s.sys_done.insert(sh.sys);
        std::vector<UObject*> v;
        if (!find_all_seh(STR("SmallResourceAsteroidObject"), v))
        {
            log_line(STR("REG seed: FindAllOf faulted: ") + seh_describe(g_seh));
            return;
        }
        s.seed_list.swap(v);
        s.seed_cur = 0;
        s.seeding = true;
        ++s.pass;
        log_line(STR("REG seed start saves=") + std::to_wstring(s.seed_list.size()) + STR(" pass=") + std::to_wstring(s.pass));
    }

    // WHAT: ends a pass: rocks it did not see (save gone, or not alive any more) are killed; the self-check is due.
    void reg_seed_finish(RegState& s)
    {
        s.seeding = false;
        size_t gone = 0;
        for (size_t i = 0; i < s.tab.rocks.size(); ++i)
        {
            if (!s.tab.rocks[i].alive || s.seen[i] == s.pass) continue;
            if (s.tab.kill(s.tab.rocks[i].id, s.ticks)) ++gone;
        }
        s.seed_list.clear();
        s.self_due = true;
        size_t alive = 0;
        for (const RegRock& r : s.tab.rocks) if (r.alive) ++alive;
        log_line(STR("REG seed done pass=") + std::to_wstring(s.pass) + STR(" rocks=") + std::to_wstring(alive) + STR(" gone=") + std::to_wstring(gone));
    }

    // WHAT: one seed slice: up to REG_SEED_TICK saves, stopped by the budget. Finishes the pass at the end.
    void reg_seed_slice(RegState& s, const LARGE_INTEGER& t0)
    {
        size_t n = 0;
        while (s.seed_cur < s.seed_list.size() && n < REG_SEED_TICK)
        {
            if (reg_over_budget(perf_ms_since(t0))) return;
            reg_seed_one(s, s.seed_list[s.seed_cur++]);
            ++n;
        }
        if (s.seed_cur >= s.seed_list.size()) reg_seed_finish(s);
    }

    // WHAT: kills a rock when its save object is gone. Suspects (actor deleted) are checked first, then targeted rocks, every tick.
    // WHY: an actor delete alone is a stream-out, not a destroy: only the save object decides.
    void reg_target_stage(RegState& s)
    {
        // registered targets: the ships' rocks now (only a mark, no behaviour change)
        std::set<int> want(s.targets.begin(), s.targets.end());
        std::vector<int> drop;
        for (int id : s.tab.targeted()) if (!want.count(id)) drop.push_back(id);
        for (int id : drop) s.tab.target_set(id, false);
        for (int id : want) s.tab.target_set(id, true);
        // suspects and targeted rocks: save alive?
        size_t n = 0;
        std::vector<size_t> keep;
        for (size_t i : s.suspect)
        {
            if (n++ >= REG_SUSPECT_TICK) { keep.push_back(i); continue; }
            if (i < s.save.size() && s.tab.rocks[i].alive && !alive_seh(s.save[i])) s.tab.kill(s.tab.rocks[i].id, s.ticks);
        }
        s.suspect.swap(keep);
        for (int id : want)
        {
            const size_t i = s.tab.index_of(id);
            if (i == RockReg::npos || !s.tab.rocks[i].alive) continue;
            if (!alive_seh(s.save[i])) s.tab.kill(id, s.ticks);
        }
    }

    // WHAT: GetRadius of the rocks near a ship (or targeted), REG_RAD_TICK per tick. Each rock once; zero is cached too.
    void reg_radius_stage(RegState& s, const std::vector<RegShip>& ships)
    {
        std::vector<size_t> want;
        for (const RegShip& sh : ships) s.tab.radius_wanted(sh.sys, {{sh.p[0], sh.p[1], sh.p[2]}}, REG_RAD_NEAR_M, REG_RAD_TICK, want);
        for (int id : s.tab.targeted())
        {
            const size_t i = s.tab.index_of(id);
            if (i != RockReg::npos && !s.tab.rocks[i].rad_known && want.size() < REG_RAD_TICK) want.push_back(i);
        }
        for (size_t i : want)
        {
            if (i >= s.save.size() || !s.tab.rocks[i].alive) continue;
            UObject* sv = s.save[i];
            if (!alive_seh(sv)) { s.tab.kill(s.tab.rocks[i].id, s.ticks); continue; }
            int64_t rad = 0;
            const double raw = call_pnum(sv, STR("GetRadius"), rad) ? static_cast<double>(rad) : 0.0;
            s.tab.set_radius(i, raw / g_raw_per_m);
        }
    }

    // WHAT: loaded actors for the rocks near a ship: the handle comes from the old HUD map (g_rock_by_id). Handles that the old list no longer
    //       holds are dropped. No game call except the SEH alive check.
    void reg_actor_stage(RegState& s, const std::vector<RegShip>& ships)
    {
        std::vector<size_t> cand;
        for (const RegShip& sh : ships) s.tab.within(sh.sys, sh.p, REG_RAD_NEAR_M, cand);
        size_t n = 0;
        for (size_t i : cand)
        {
            if (n >= REG_ACT_TICK) break;
            RegRock& r = s.tab.rocks[i];
            if (!r.alive || r.act) continue;
            ++n;
            auto it = g_rock_by_id.find(r.id);
            if (it != g_rock_by_id.end() && alive_seh(it->second)) { s.tab.set_act(i, it->second); s.act_list.push_back(i); }
        }
        std::vector<size_t> keep;
        for (size_t i : s.act_list)
        {
            const RegRock& r = s.tab.rocks[i];
            if (!r.act) continue;
            auto it = g_rock_by_id.find(r.id);
            if (it == g_rock_by_id.end() || it->second != r.act) { s.tab.set_act(i, nullptr); continue; }   // pointer compare only
            keep.push_back(i);
        }
        s.act_list.swap(keep);
    }

    // WHAT: validation of the save objects of the rocks within REG_NEAR_M of a ship, REG_VALID_TICK per tick (rotating).
    void reg_validate_stage(RegState& s, const std::vector<RegShip>& ships)
    {
        std::vector<size_t> cand;
        for (const RegShip& sh : ships) s.tab.within(sh.sys, sh.p, REG_NEAR_M, cand);
        std::sort(cand.begin(), cand.end());
        size_t n = 0, last = 0;
        bool limited = false;
        for (size_t i : cand)
        {
            if (i < s.valid_cur) continue;
            if (n >= REG_VALID_TICK) { limited = true; break; }
            if (!s.tab.rocks[i].alive) continue;
            ++n;
            last = i + 1;
            if (!alive_seh(s.save[i])) s.tab.kill(s.tab.rocks[i].id, s.ticks);
        }
        s.valid_cur = limited ? last : 0;
    }

    // WHAT: self-check: for up to REG_SELF_N rocks with a registry entry and a loaded actor, compares the registry centre (m) with
    //       the actor K2_GetActorLocation / MV_UU_PER_M. Logs n, max and mean error. Runs once after each seed pass and every REG_SELF_MS.
    // WHY: the save-derived position is only trusted once it matches the game's own actor position.
    void reg_self_stage(RegState& s)
    {
        if (!s.self_due || s.seeding) return;
        s.self_due = false;
        s.self_ms = GetTickCount64();
        double max_e = 0.0, sum = 0.0;
        int worst_id = 0;
        size_t n = 0;
        for (size_t i : s.act_list)
        {
            if (n >= REG_SELF_N) break;
            const RegRock& r = s.tab.rocks[i];
            if (!r.alive || !r.act || !alive_seh(static_cast<UObject*>(r.act))) continue;
            double p[3] = {0, 0, 0};
            std::wstring why;
            if (!actor_vec_call(static_cast<UObject*>(r.act), STR("K2_GetActorLocation"), p, why)) continue;
            const double d[3] = {p[0] / MV_UU_PER_M - r.c[0], p[1] / MV_UU_PER_M - r.c[1], p[2] / MV_UU_PER_M - r.c[2]};
            const double e = norm3(d);
            sum += e;
            ++n;
            if (e > max_e) { max_e = e; worst_id = r.id; }
        }
        if (n == 0) { log_line(STR("REG selfcheck n=0 (no loaded rock with a registry entry)")); return; }
        log_line(STR("REG selfcheck n=") + std::to_wstring(n) + STR(" max_err_m=") + std::to_wstring(max_e) + STR(" mean_err_m=") +
                 std::to_wstring(sum / static_cast<double>(n)) + STR(" worst_id=") + std::to_wstring(worst_id));
    }

    // WHAT: one line per 60 s: counts, version, tick timing of the window.
    void reg_stats_stage(RegState& s)
    {
        size_t wr = 0, ld = 0, dead = 0;
        for (const RegRock& r : s.tab.rocks)
        {
            if (r.rad_known) ++wr;
            if (!r.alive) ++dead;
            else if (r.act) ++ld;
        }
        const double avg = s.stat_ticks ? s.ms_sum / static_cast<double>(s.stat_ticks) : 0.0;
        log_line(STR("REG stats rocks=") + std::to_wstring(s.tab.rocks.size()) + STR(" with_radius=") + std::to_wstring(wr) + STR(" loaded=") +
                 std::to_wstring(ld) + STR(" dead=") + std::to_wstring(dead) + STR(" version=") + std::to_wstring(s.tab.version) + STR(" ticks=") +
                 std::to_wstring(s.stat_ticks) + STR(" tick_avg_ms=") + std::to_wstring(avg) + STR(" tick_worst_ms=") + std::to_wstring(s.ms_worst));
        s.ms_sum = 0.0;
        s.ms_worst = 0.0;
        s.stat_ticks = 0;
    }

    // WHAT: writes the REG_DUMP_N rocks nearest to (sys, p m) to file: id, ore, abs m (x y z), distance, radius, loaded. Returns the row count.
    size_t reg_dump_file(const RegState& s, int sys, const double p[3], const wchar_t* name)
    {
        std::vector<std::pair<double, size_t>> out;
        s.tab.nearest_n(sys, nullptr, p, nullptr, nullptr, REG_DUMP_M, REG_DUMP_N, out);
        std::string txt = "# MiningRegistry: id ore abs_x_m abs_y_m abs_z_m dist_m radius_m loaded\n";
        for (const auto& pr : out)
        {
            const RegRock& r = s.tab.rocks[pr.second];
            const std::wstring ore = r.items.empty() ? STR("?") : item_kind(static_cast<UObject*>(r.items[0]));
            char b[256];
            std::snprintf(b, sizeof(b), "%d %s %.2f %.2f %.2f %.2f %.2f %d\n", r.id, u8(ore).c_str(), r.c[0], r.c[1], r.c[2], pr.first,
                          r.rad_m, r.act ? 1 : 0);
            txt += b;
        }
        try { write_atomic(get_dll_dir() / name, txt); } catch (...) {}
        return out.size();
    }

    // WHAT: the registry tick (cmd_tick 0.25 s block). Stages in priority order; a stage is skipped when the 1 ms budget is spent.
    // WHY: the registry is the one updater, so no ship pays for rock reads of its own.
    void registry_tick(double dt, const std::vector<RegShip>& ships, const std::vector<int>& targets)
    {
        (void)dt;
        RegState& s = reg_st();
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        const ULONGLONG now = GetTickCount64();
        ++s.ticks;
        s.ships = ships;
        s.targets = targets;
        reg_target_stage(s);   // deletes (suspects) and targeted rocks: always, cheap
        // seed triggers: first tick, safety refresh, a mining ship in a system no pass covered
        if (!s.seeding)
        {
            bool start = s.seed_ms == 0 || now - s.seed_ms >= REG_SEED_REFRESH_MS;
            if (!start && now - s.sys_ms >= REG_SYS_MS)
            {
                s.sys_ms = now;
                for (const RegShip& sh : ships) if (sh.sys > 0 && !s.sys_done.count(sh.sys)) { start = true; break; }
            }
            if (start) reg_seed_begin(s, ships);
        }
        if (s.seeding && !reg_over_budget(perf_ms_since(t0))) reg_seed_slice(s, t0);
        if (!reg_over_budget(perf_ms_since(t0))) reg_validate_stage(s, ships);
        if (!reg_over_budget(perf_ms_since(t0))) reg_radius_stage(s, ships);
        if (!reg_over_budget(perf_ms_since(t0))) reg_actor_stage(s, ships);
        if (!reg_over_budget(perf_ms_since(t0))) reg_self_stage(s);
        if (!reg_over_budget(perf_ms_since(t0)) && now - s.stats_ms >= REG_STATS_MS)
        {
            s.stats_ms = now;
            reg_stats_stage(s);
            if (!ships.empty()) reg_dump_file(s, ships[0].sys, ships[0].p, L"MiningRegistry.txt");
        }
        if (s.self_ms == 0 && !s.seeding) s.self_ms = now;   // the periodic self-check counts from the first full pass
        if (!s.seeding && !s.self_due && now - s.self_ms >= REG_SELF_MS) { s.self_due = true; }
        const double ms = perf_ms_since(t0);
        s.ms_sum += ms;
        if (ms > s.ms_worst) s.ms_worst = ms;
        ++s.stat_ticks;
        if (reg_over_budget(ms) && (s.slow_ms == 0 || now - s.slow_ms >= REG_SLOW_LOG_MS))
        {
            s.slow_ms = now;
            log_line(STR("REG slow ms=") + std::to_wstring(ms) + STR(" rocks=") + std::to_wstring(s.tab.rocks.size()));
        }
    }

    // WHAT: rock died in the engine (delete listener, drained by rock_drain_dead): clears the actor handle and marks the rock suspect.
    // WHY: the kill decision is taken from the save object (see reg_target_stage), not from the delete.
    void reg_on_dead(const std::unordered_set<const void*>& dead)
    {
        RegState& s = reg_st();
        for (const void* a : dead)
        {
            const size_t i = s.tab.index_of_act(a);
            if (i == RockReg::npos) continue;
            s.tab.set_act(i, nullptr);
            s.suspect.push_back(i);
        }
    }

    // WHAT: dev commands. 'reg_dump' writes MiningRegistry.txt around the ship sid. 'reg_query:<ship>:<radius_m>' logs the rocks within radius.
    std::wstring registry_dev(int sid, const std::string& cmd)
    {
        RegState& s = reg_st();
        if (cmd == "reg_dump")
        {
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            ActFrame f;
            if (!alive(ship) || !act_frame(ship, f)) return STR("error: ship unreadable");
            const double p[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
            const size_t n = reg_dump_file(s, sys_id(ship), p, L"MiningRegistry.txt");
            return STR("ok registry dump rows=") + std::to_wstring(n);
        }
        // reg_query:<ship>:<radius_m>
        const size_t c1 = cmd.find(':');
        const size_t c2 = c1 == std::string::npos ? std::string::npos : cmd.find(':', c1 + 1);
        if (c2 == std::string::npos) return STR("error: usage reg_query:<ship>:<radius_m>");
        const int ship_id = std::atoi(cmd.substr(c1 + 1, c2 - c1 - 1).c_str());
        const double rad = std::atof(cmd.substr(c2 + 1).c_str());
        if (rad <= 0.0) return STR("error: radius must be > 0");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), ship_id);
        ActFrame f;
        if (!alive(ship) || !act_frame(ship, f)) return STR("error: ship unreadable");
        const double p[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
        const int sys = sys_id(ship);
        std::vector<size_t> cand;
        s.tab.within(sys, p, rad, cand);
        log_line(STR("REG query ship=") + std::to_wstring(ship_id) + STR(" sys=") + std::to_wstring(sys) + STR(" r_m=") + std::to_wstring(rad) +
                 STR(" n=") + std::to_wstring(cand.size()));
        for (size_t k = 0; k < cand.size() && k < REG_LIST_N; ++k)
        {
            const RegRock& r = s.tab.rocks[cand[k]];
            log_line(STR("REG query row id=") + std::to_wstring(r.id) + STR(" abs_m=") + std::to_wstring(r.c[0]) + STR(",") + std::to_wstring(r.c[1]) +
                     STR(",") + std::to_wstring(r.c[2]) + STR(" rad_m=") + std::to_wstring(r.rad_m) + STR(" loaded=") + (r.act ? STR("1") : STR("0")));
        }
        return STR("ok rocks=") + std::to_wstring(cand.size()) + STR(" (see REG query log)");
    }
}
