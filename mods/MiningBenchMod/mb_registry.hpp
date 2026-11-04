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
// SEED TRIGGERS (no ship needed): first tick, every 10 min (safety refresh), the player's system changed (checked every 1 s),
//       no alive rock (retry every 5 s while the world is not ready), a mining ship in a system no pass has covered yet.
//       A pass is one FindAllOf of SmallResourceAsteroidObject (all systems) read in slices.
// COMPARE: reg_cmp_run (SEH) walks g_rock_by_id: rows = registry vs actor (selfcheck, reg_check); missing = coverage (actors within 3 km, no entry).
// UNITS: metres (see mb_registry_logic.hpp). Positions of ships come from MineSt::anchor (uu / MV_UU_PER_M), not a new game call.
// USED BY: mb_rocks.hpp (rock_drain_dead calls reg_on_dead), mb_testcmd.hpp (registry_frame, cmd_tick), mb_devcmd.hpp (reg_dump, reg_query).
// INCLUDE ORDER: after mb_rocks.hpp, before mb_steer.hpp (uses alive_seh, g_rock_by_id, act_frame, item_kind from mb_rocks.hpp).
// OUTPUT: log lines 'REG seed start/done', 'REG selfcheck SUMMARY', 'REG stats', 'REG slow', 'REG check', 'REG query row' (log filter keyword "reg ");
//       files MiningRegistry.txt (every 60 s and by reg_dump; header holds the last selfcheck SUMMARY), MiningRegCheck.txt (reg_check), MiningRegQuery.txt (reg_query).
// DEV CMDS: reg_dump, reg_query:<ship>:<radius_m>, reg_check:<ship>[:<n>] (mb_devcmd.hpp registry_dev).

#pragma once

#include "mb_registry_logic.hpp"

namespace
{
    // forward declarations of helpers defined in mb_warp.hpp (same unnamed namespace)
    bool get_chunk(UObject* o, int64_t c[3]);
    int sys_id(UObject* o);
    bool ch_player(int& psys, int64_t pch[3]);   // mb_chunkhold.hpp: the player's system (0/false when unreadable)

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
    constexpr ULONGLONG REG_TRIG_MS = 1000;             // world system and rock count check (seed triggers)
    constexpr ULONGLONG REG_EMPTY_RETRY_MS = 5000;      // seed retry while the table holds no alive rock (world not ready)
    constexpr double REG_MISSING_M = 3000.0;            // compare: a loaded actor this close with no registry entry counts as missing

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
        size_t seed_total = 0;             // saves of the running pass (log)
        double seed_work_ms = 0.0;         // time spent in the running pass (FindAllOf + slices), log
        bool seeding = false;
        uint64_t pass = 0;
        std::set<int> sys_done;            // systems a pass has covered (started with a mining ship there)
        int world_sys = 0;                 // player's system at the last trigger check (0 = unknown)
        ULONGLONG seed_ms = 0, self_ms = 0, stats_ms = 0, slow_ms = 0, sys_ms = 0, trig_ms = 0;
        std::string self_summary = "SUMMARY n/a (no selfcheck yet)";   // last selfcheck SUMMARY line (MiningRegistry.txt header)
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
        s.seed_ms = GetTickCount64();   // set first: a faulted find is retried at the next trigger, not every tick
        for (const RegShip& sh : ships) if (sh.sys > 0) s.sys_done.insert(sh.sys);
        LARGE_INTEGER a;
        QueryPerformanceCounter(&a);
        std::vector<UObject*> v;
        const bool found = find_all_seh(STR("SmallResourceAsteroidObject"), v);
        s.seed_work_ms = perf_ms_since(a);
        if (!found)
        {
            log_line(STR("REG seed: FindAllOf faulted: ") + seh_describe(g_seh));
            return;
        }
        s.seed_list.swap(v);
        s.seed_cur = 0;
        s.seed_total = s.seed_list.size();
        s.seeding = true;
        ++s.pass;
        log_line(STR("REG seed start n=") + std::to_wstring(s.seed_total) + STR(" pass=") + std::to_wstring(s.pass));
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
        log_line(STR("REG seed done n=") + std::to_wstring(alive) + STR(" saves=") + std::to_wstring(s.seed_total) + STR(" gone=") +
                 std::to_wstring(gone) + STR(" pass=") + std::to_wstring(s.pass) + STR(" ms=") + std::to_wstring(s.seed_work_ms));
    }

    // WHAT: one seed slice: up to REG_SEED_TICK saves, stopped by the budget. Finishes the pass at the end.
    void reg_seed_slice(RegState& s, const LARGE_INTEGER& t0)
    {
        LARGE_INTEGER a;
        QueryPerformanceCounter(&a);
        size_t n = 0;
        while (s.seed_cur < s.seed_list.size() && n < REG_SEED_TICK && !reg_over_budget(perf_ms_since(t0)))
        {
            reg_seed_one(s, s.seed_list[s.seed_cur++]);
            ++n;
        }
        s.seed_work_ms += perf_ms_since(a);
        if (s.seed_cur >= s.seed_list.size()) reg_seed_finish(s);
    }

    // WHAT: rocks alive in the table (count for the seed retry).
    size_t reg_alive_count(const RegState& s)
    {
        size_t n = 0;
        for (const RegRock& r : s.tab.rocks) if (r.alive) ++n;
        return n;
    }

    // WHAT: the player's system id (run under seh_run). sys = 0 when unreadable.
    struct RegWorldCtx { int sys = 0; };
    void reg_world_fn(void* p)
    {
        auto* x = static_cast<RegWorldCtx*>(p);
        int64_t pch[3] = {0, 0, 0};
        int ps = 0;
        if (ch_player(ps, pch)) x->sys = ps;
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
    // WHAT: one row of a compare: a registry rock and its loaded actor. Positions in m; rad_live = GetRadius of the save object (-1 = not read).
    struct RegCmpRow
    {
        int id = 0;
        size_t idx = 0;
        double reg[3] = {0, 0, 0}, act[3] = {0, 0, 0};
        double err = 0.0, dist = 0.0, rad_reg = 0.0, rad_live = -1.0;
    };

    // WHAT: the summary numbers of a compare rows list.
    struct RegSum { size_t n = 0; double max_e = 0.0, mean_e = 0.0; int worst_id = 0; };
    RegSum reg_sum(const std::vector<RegCmpRow>& rows)
    {
        RegSum sm;
        double sum = 0.0;
        for (const RegCmpRow& r : rows)
        {
            sum += r.err;
            ++sm.n;
            if (r.err > sm.max_e) { sm.max_e = r.err; sm.worst_id = r.id; }
        }
        sm.mean_e = sm.n ? sum / static_cast<double>(sm.n) : 0.0;
        return sm;
    }

    // WHAT: compare context (reg_cmp_fn).
    struct RegCmpCtx
    {
        RegState* s = nullptr;
        bool has_c = false;
        double c[3] = {0, 0, 0};
        size_t n_max = 0;
        std::vector<RegCmpRow>* rows = nullptr;
        long long missing = -1;
    };

    // WHAT: every loaded rock actor (g_rock_by_id) against the registry, under SEH. Rows: the n_max nearest to c (any order when no c), with the
    //       actor position (K2_GetActorLocation / MV_UU_PER_M), the error, the registry radius and GetRadius of the save object.
    //       missing (when c is known) = loaded actors within REG_MISSING_M of c with no registry entry (coverage check).
    // WHY one loop: the same candidates serve the rows and the coverage count.
    void reg_cmp_fn(void* p)
    {
        auto* x = static_cast<RegCmpCtx*>(p);
        RegState& s = *x->s;
        std::vector<RegCmpRow> cand;
        long long missing = 0;
        for (const auto& kv : g_rock_by_id)
        {
            UObject* a = kv.second;
            if (!alive_seh(a)) continue;
            double ap[3] = {0, 0, 0};
            std::wstring why;
            if (!actor_vec_call(a, STR("K2_GetActorLocation"), ap, why)) continue;
            const double am[3] = {ap[0] / MV_UU_PER_M, ap[1] / MV_UU_PER_M, ap[2] / MV_UU_PER_M};
            const double dv[3] = {am[0] - x->c[0], am[1] - x->c[1], am[2] - x->c[2]};
            const double dist = x->has_c ? norm3(dv) : 0.0;
            const size_t idx = s.tab.index_of(kv.first);
            if (idx == RockReg::npos || !s.tab.rocks[idx].alive)
            {
                if (x->has_c && dist <= REG_MISSING_M) ++missing;
                continue;
            }
            RegCmpRow r;
            r.id = kv.first;
            r.idx = idx;
            std::copy(s.tab.rocks[idx].c, s.tab.rocks[idx].c + 3, r.reg);
            std::copy(am, am + 3, r.act);
            r.dist = dist;
            cand.push_back(r);
        }
        if (x->has_c) std::sort(cand.begin(), cand.end(), [](const RegCmpRow& a, const RegCmpRow& b) { return a.dist < b.dist; });
        x->missing = x->has_c ? missing : -1;
        for (size_t k = 0; k < cand.size() && k < x->n_max; ++k)
        {
            RegCmpRow r = cand[k];
            const double d[3] = {r.act[0] - r.reg[0], r.act[1] - r.reg[1], r.act[2] - r.reg[2]};
            r.err = norm3(d);
            r.rad_reg = s.tab.rocks[r.idx].rad_known ? s.tab.rocks[r.idx].rad_m : -1.0;
            UObject* sv = r.idx < s.save.size() ? s.save[r.idx] : nullptr;
            int64_t rad = 0;
            if (alive_seh(sv) && call_pnum(sv, STR("GetRadius"), rad)) r.rad_live = static_cast<double>(rad) / g_raw_per_m;
            x->rows->push_back(r);
        }
    }

    // WHAT: runs reg_cmp_fn under SEH. false = a fault (g_seh holds it).
    bool reg_cmp_run(RegState& s, bool has_c, const double c[3], size_t n_max, std::vector<RegCmpRow>& rows, long long& missing)
    {
        RegCmpCtx x;
        x.s = &s;
        x.has_c = has_c;
        if (has_c) std::copy(c, c + 3, x.c);
        x.n_max = n_max;
        x.rows = &rows;
        const bool ok = seh_run([](void* p) { reg_cmp_fn(p); }, &x);
        missing = x.missing;
        return ok;
    }

    // WHAT: self-check (after each seed pass and every REG_SELF_MS): the registry against the loaded actors, centred on the first mining ship
    //       when there is one. Logs one 'REG selfcheck SUMMARY' line and keeps it for the MiningRegistry.txt header.
    // WHY: the save-derived position is only trusted once it matches the game's own actor position.
    void reg_self_stage(RegState& s)
    {
        if (!s.self_due || s.seeding) return;
        s.self_due = false;
        s.self_ms = GetTickCount64();
        const bool has = !s.ships.empty();
        const double cc[3] = {has ? s.ships[0].p[0] : 0.0, has ? s.ships[0].p[1] : 0.0, has ? s.ships[0].p[2] : 0.0};
        std::vector<RegCmpRow> rows;
        long long missing = -1;
        if (!reg_cmp_run(s, has, cc, REG_SELF_N, rows, missing))
        {
            log_line(STR("REG selfcheck fault: ") + seh_describe(g_seh));
            return;
        }
        const RegSum sm = reg_sum(rows);
        s.self_summary = reg_summary_line(sm.n, sm.max_e, sm.mean_e, missing);
        log_line(STR("REG selfcheck ") + widen(s.self_summary) + STR(" worst_id=") + std::to_wstring(sm.worst_id) +
                 STR(" centre=") + (has ? STR("ship") : STR("none")));
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
        std::string txt = "# MiningRegistry: id ore abs_x_m abs_y_m abs_z_m dist_m radius_m loaded\n# selfcheck " + s.self_summary + "\n";
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
        // seed triggers, no ship needed: first tick, 10 min refresh, the player's system changed, no alive rock (retry every 5 s),
        // a mining ship in a system no pass covered
        bool start = false;
        if (now - s.trig_ms >= REG_TRIG_MS)
        {
            s.trig_ms = now;
            RegWorldCtx wc;
            if (seh_run(reg_world_fn, &wc) && wc.sys > 0 && wc.sys != s.world_sys)
            {
                s.world_sys = wc.sys;
                start = true;
            }
            if (!s.seeding && reg_alive_count(s) == 0 && now - s.seed_ms >= REG_EMPTY_RETRY_MS) start = true;
        }
        if (!s.seeding)
        {
            if (s.seed_ms == 0 || now - s.seed_ms >= REG_SEED_REFRESH_MS) start = true;
            if (!start && now - s.sys_ms >= REG_SYS_MS)
            {
                s.sys_ms = now;
                for (const RegShip& sh : ships) if (sh.sys > 0 && !s.sys_done.count(sh.sys)) { start = true; break; }
            }
            if (start) reg_seed_begin(s, ships);
        }
        if (s.seeding && !reg_over_budget(perf_ms_since(t0))) reg_seed_slice(s, t0);
        // stats and dump: not budget-gated (cheap, once a minute); they used to run last and were skipped whenever an earlier stage used the budget
        if (now - s.stats_ms >= REG_STATS_MS)
        {
            s.stats_ms = now;
            reg_stats_stage(s);
            if (!ships.empty()) reg_dump_file(s, ships[0].sys, ships[0].p, L"MiningRegistry.txt");
        }
        if (!reg_over_budget(perf_ms_since(t0))) reg_validate_stage(s, ships);
        if (!reg_over_budget(perf_ms_since(t0))) reg_radius_stage(s, ships);
        if (!reg_over_budget(perf_ms_since(t0))) reg_actor_stage(s, ships);
        if (!reg_over_budget(perf_ms_since(t0))) reg_self_stage(s);
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

    // WHAT: a ship's centre (m), system and ok flag (run under seh_run). ok = false when the ship or its frame is unreadable.
    struct RegShipPos { int sid = 0; UObject* ship = nullptr; int sys = 0; double p[3] = {0, 0, 0}; bool ok = false; };
    void reg_ship_pos_fn(void* p)
    {
        auto* x = static_cast<RegShipPos*>(p);
        x->ship = find_by_id(STR("DMSPlayerShipObject"), x->sid);
        ActFrame f;
        if (!alive(x->ship) || !act_frame(x->ship, f)) return;
        x->sys = sys_id(x->ship);
        for (int k = 0; k < 3; ++k) x->p[k] = f.loc[k] / MV_UU_PER_M;
        x->ok = x->sys > 0;
    }

    // WHAT: ore name of each query row (run under seh_run): item_kind of the first ore item, "?" when none.
    struct RegOreCtx { const RegState* s = nullptr; const std::vector<std::pair<double, size_t>>* rows = nullptr; std::vector<std::wstring>* ore = nullptr; };
    void reg_ore_fn(void* p)
    {
        auto* x = static_cast<RegOreCtx*>(p);
        for (const auto& pr : *x->rows)
        {
            const RegRock& r = x->s->tab.rocks[pr.second];
            x->ore->push_back(r.items.empty() ? std::wstring(STR("?")) : item_kind(static_cast<UObject*>(r.items[0])));
        }
    }

    // WHAT: reg_check:<ship>[:<n>]. For the n loaded rock actors nearest to the ship: registry position vs actor position, registry radius vs
    //       GetRadius of the save object. Writes MiningRegCheck.txt (rows, then the SUMMARY line) and logs the SUMMARY.
    std::wstring reg_check_cmd(RegState& s, int ship_id, size_t n)
    {
        RegShipPos sp;
        sp.sid = ship_id;
        if (!seh_run(reg_ship_pos_fn, &sp)) return STR("error: probe fault ") + seh_describe(g_seh);
        if (!sp.ok) return STR("error: ship unreadable");
        std::vector<RegCmpRow> rows;
        long long missing = -1;
        if (!reg_cmp_run(s, true, sp.p, n, rows, missing)) return STR("error: probe fault ") + seh_describe(g_seh);
        const RegSum sm = reg_sum(rows);
        const std::string summary = reg_summary_line(sm.n, sm.max_e, sm.mean_e, missing);
        char b[256];
        std::string txt;
        std::snprintf(b, sizeof(b), "# MiningRegCheck ship=%d sys=%d pos_m=(%.2f,%.2f,%.2f) requested=%zu rows=%zu (ship centre = live actor location / 100)\n",
                      sp.sid, sp.sys, sp.p[0], sp.p[1], sp.p[2], n, rows.size());
        txt += b;
        txt += "# columns: id reg_x_m reg_y_m reg_z_m act_x_m act_y_m act_z_m err_m reg_rad_m live_rad_m dist_ship_m (rad -1 = not read)\n";
        for (const RegCmpRow& r : rows)
        {
            std::snprintf(b, sizeof(b), "%d %.2f %.2f %.2f %.2f %.2f %.2f %.3f %.2f %.2f %.2f\n", r.id, r.reg[0], r.reg[1], r.reg[2], r.act[0], r.act[1],
                          r.act[2], r.err, r.rad_reg, r.rad_live, r.dist);
            txt += b;
        }
        txt += summary + "\n";
        try { write_atomic(get_dll_dir() / L"MiningRegCheck.txt", txt); } catch (...) { return STR("error: MiningRegCheck.txt not written"); }
        log_line(widen("REG check ship=" + std::to_string(sp.sid) + " requested=" + std::to_string(n) + " " + summary));
        return STR("ok MiningRegCheck.txt rows=") + std::to_wstring(rows.size()) + STR(" ") + widen(summary);
    }

    // WHAT: reg_query:<ship>:<radius_m>. The nearest REG_LIST_N registry rocks within radius_m of the ship's live position, nearest first.
    //       One log line per row ('REG query row'), and MiningRegQuery.txt with the ship position used in its header.
    std::wstring reg_query_cmd(RegState& s, int ship_id, double rad)
    {
        RegShipPos sp;
        sp.sid = ship_id;
        if (!seh_run(reg_ship_pos_fn, &sp)) return STR("error: probe fault ") + seh_describe(g_seh);
        if (!sp.ok) return STR("error: ship unreadable");
        std::vector<std::pair<double, size_t>> out;
        s.tab.nearest_n(sp.sys, nullptr, sp.p, nullptr, nullptr, rad, REG_LIST_N, out);
        std::vector<std::wstring> ore;
        RegOreCtx oc{&s, &out, &ore};
        if (!seh_run(reg_ore_fn, &oc)) return STR("error: probe fault ") + seh_describe(g_seh);
        char b[320];
        std::string txt;
        std::snprintf(b, sizeof(b), "# MiningRegQuery ship=%d sys=%d pos_m=(%.2f,%.2f,%.2f) radius_m=%.2f rows=%zu (nearest first, from the live ship position)\n",
                      sp.sid, sp.sys, sp.p[0], sp.p[1], sp.p[2], rad, out.size());
        txt += b;
        txt += "# columns: rank id dist_m rad_m loaded ore abs_x_m abs_y_m abs_z_m\n";
        log_line(widen("REG query ship=" + std::to_string(sp.sid) + " sys=" + std::to_string(sp.sys) + " pos_m=(" + std::to_string(sp.p[0]) + "," +
                       std::to_string(sp.p[1]) + "," + std::to_string(sp.p[2]) + ") r_m=" + std::to_string(rad) + " n=" + std::to_string(out.size())));
        for (size_t k = 0; k < out.size(); ++k)
        {
            const RegRock& r = s.tab.rocks[out[k].second];
            const std::string ot = u8(ore[k]);
            std::snprintf(b, sizeof(b), "%zu %d %.2f %.2f %d %s %.2f %.2f %.2f\n", k + 1, r.id, out[k].first, r.rad_m, r.act ? 1 : 0, ot.c_str(),
                          r.c[0], r.c[1], r.c[2]);
            txt += b;
            std::string line(b);
            line.pop_back();
            log_line(widen("REG query row rank=" + line));
        }
        try { write_atomic(get_dll_dir() / L"MiningRegQuery.txt", txt); } catch (...) { return STR("error: MiningRegQuery.txt not written"); }
        return STR("ok rocks=") + std::to_wstring(out.size()) + STR(" (MiningRegQuery.txt, REG query log)");
    }

    // WHAT: dev commands. 'reg_dump' writes MiningRegistry.txt around the ship sid. 'reg_query:<ship>:<radius_m>' and 'reg_check:<ship>[:<n>]' see above.
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
        if (cmd.rfind("reg_check:", 0) == 0)
        {
            int rs = 0;
            size_t rn = 0;
            if (!reg_check_parse(cmd, rs, rn)) return STR("error: usage reg_check:<ship>[:<n>] (n 1..100, default 30)");
            return reg_check_cmd(s, rs, rn);
        }
        // reg_query:<ship>:<radius_m>
        const size_t c1 = cmd.find(':');
        const size_t c2 = c1 == std::string::npos ? std::string::npos : cmd.find(':', c1 + 1);
        if (c2 == std::string::npos) return STR("error: usage reg_query:<ship>:<radius_m>");
        const int ship_id = std::atoi(cmd.substr(c1 + 1, c2 - c1 - 1).c_str());
        const double rad = std::atof(cmd.substr(c2 + 1).c_str());
        if (rad <= 0.0) return STR("error: radius must be > 0");
        return reg_query_cmd(s, ship_id, rad);
    }
}
