// FILE: mods/MiningBenchMod/mb_registry.hpp
// WHAT: game side of the rock registry. Seeds the RockReg table (mb_registry_logic.hpp) from the save objects
//       (SmallResourceAsteroidObject), nearest first; caches the table to disk; reads radius lazily, resolves loaded actors for rocks near
//       ships, validates save objects near ships, runs the self-check (registry position vs actor K2_GetActorLocation), stats and a
//       MiningRegistry.txt dump.
// WHY: rocks never move or shrink, so position and radius are read once per rock; the save data holds every rock, loaded or not.
// SAVE FIELDS USED (same as save_rock_nearest in mb_mining.hpp): sys_id(save) = system, get_chunk(save) + get_loc(save) = chunk-relative
//       position (abs m = chunk * 2048 + loc / g_raw_per_m), obj_id(save) = rock id (= actor ObjectId), bAlive (bool),
//       ResourceItems (TArray of ore items), GetRadius() on the save (raw, / g_raw_per_m = m). Not verified: ResourceItems length.
// DELETES: the engine delete listener (rock_drain_dead -> reg_on_dead) only clears the loaded actor handle and marks the rock suspect.
//       A rock is killed only when its SAVE object is gone (checked: suspects, targeted rocks, rocks within REG_NEAR_M of a ship).
//       WHY: the game streams actors in and out; a streamed-out actor is a delete but the rock still exists.
// PASS (one FindAllOf of SmallResourceAsteroidObject, then slices; the find is the only world-wide call):
//       1 bind: obj_id + bAlive per save (cheap). A save whose id is in the table is bound (save pointer kept); the others are 'need'.
//       2 sample (only after a cache load): REG_CACHE_SAMPLE_N nearest + as many random cache rocks are read and compared (reg_cache_verdict).
//       3 classify: system + chunk of each 'need' save -> ring (chunks from the nearest ship of its system at pass start).
//       4 read: 'need' saves in ring order (full read). Phase A = rings <= REG_PRIORITY_CHUNKS: when done the zones are ready
//         (reg_area_ready) and 'REG seed near done' is logged. Budget REG_SEED_A_BUDGET_MS for the first REG_PHASE_A_MS, then REG_BUDGET_MS.
//       5 finish: rocks the pass did not see are killed; cache written when the table changed by > REG_CACHE_CHANGE_FRAC or no valid cache.
// CACHE (MiningRegistryCache.bin in the dlls dir, format in mb_registry_logic.hpp): loaded once, first pass, one read; entries are unverified
//       until bound (and sampled). Corrupt, other version or other exe size: ignored (normal seed). Written in chunks over ticks
//       (cache_write_step, REG_CACHE_WRITE_MS per tick, writing ticks run the writer only), tmp file + rename.
// TICK: registry_tick(dt, ships, targets) from cmd_tick (0.25 s block). Targets/suspects alive check every tick; triggers; the seed slice
//       (own budget); the writer; then validation (2 km), radius reads, actor resolution, self-check (once after a pass and every 10 min),
//       stats and dump every 60 s. Each of these stages is skipped when its budget is spent.
// SEED TRIGGERS (no ship needed): first tick, every 10 min (safety refresh), a world load (player controller/object identity changed; a warp does not count) (checked every 1 s),
//       no alive rock (retry every 5 s while the world is not ready), a mining ship in a system no pass has covered yet (restarts a running
//       pass: the table is kept, only the lists of the pass are dropped).
// COMPARE: reg_cmp_run (SEH) walks g_rock_by_id: rows = registry vs actor (selfcheck, reg_check); missing = coverage (actors within 3 km, no entry).
// UNITS: metres (see mb_registry_logic.hpp). Positions of ships come from MineSt::anchor (uu / MV_UU_PER_M), not a new game call.
// USED BY: mb_rocks.hpp (rock_drain_dead calls reg_on_dead), mb_testcmd.hpp (registry_frame, cmd_tick), mb_devcmd.hpp (reg_dump, reg_query),
//          mb_bubble.hpp and mb_mining.hpp (reg_area_seeded: the area check of the bubble and the path check).
// INCLUDE ORDER: after mb_rocks.hpp, before mb_steer.hpp (uses alive_seh, g_rock_by_id, act_frame, item_kind from mb_rocks.hpp).
// OUTPUT: log lines 'REG seed start/near done/done', 'REG cache none|ignored|load|trusted|merged|reseed|written', 'REG area not ready',
//       'REG selfcheck SUMMARY', 'REG stats', 'REG slow', 'REG check', 'REG query row' (log filter keyword "reg ");
//       files MiningRegistry.txt (every 60 s and by reg_dump; header holds the last selfcheck SUMMARY), MiningRegCheck.txt (reg_check),
//       MiningRegQuery.txt (reg_query), MiningRegistryCache.bin.
// DEV CMDS: reg_dump, reg_query:<ship>:<radius_m>, reg_check:<ship>[:<n>] (mb_devcmd.hpp registry_dev).

#pragma once

#include "mb_registry_logic.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace
{
    // forward declarations of helpers defined in mb_warp.hpp (same unnamed namespace)
    bool get_chunk(UObject* o, int64_t c[3]);
    int sys_id(UObject* o);
    bool ch_player(int& psys, int64_t pch[3]);   // mb_chunkhold.hpp: the player's system (0/false when unreadable)
    UObject* ch_ctrl();                          // mb_chunkhold.hpp: the player controller (cached)

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
    constexpr int REG_NOWORLD_TICKS = 4;                // failed player reads in a row before the world counts as gone (4 x 0.25 s)
    constexpr ULONGLONG REG_EMPTY_RETRY_MS = 5000;      // seed retry while the table holds no alive rock (world not ready)
    constexpr double REG_MISSING_M = 3000.0;            // compare: a loaded actor this close with no registry entry counts as missing
    constexpr double REG_RAD_SEG_M = 1500.0;            // radius reads first for rocks this close to an approach segment (m)
    constexpr size_t REG_RAD_SEG_TICK = 40;             // GetRadius reads per tick for approach segment rocks (the general cap is REG_RAD_TICK)
    constexpr wchar_t REG_CACHE_FILE[] = L"MiningRegistryCache.bin";

    // WHAT: one mining ship as the registry sees it this tick: save id, system id, centre (m).
    struct RegShip { int sid = 0; int sys = 0; double p[3] = {0, 0, 0}; bool has_seg = false; double q[3] = {0, 0, 0}; };   // has_seg: approach to q (rock centre, m)

    // WHAT: the cache writer state (one file at a time). Records are streamed to <file>.tmp in chunks; the header is written last.
    struct CacheWrite
    {
        bool on = false;
        FILE* f = nullptr;
        std::filesystem::path tmp, dst;
        size_t next = 0;                   // next rock index to write
        uint64_t save_n = 0, rock_n = 0;
        uint64_t sum = REG_CACHE_FNV0;     // FNV-1a of the bytes after the header so far
        std::vector<uint8_t> buf;          // bytes not yet written
        std::vector<std::string> kinds;    // ore names written (record ore = index)
        std::unordered_map<std::string, int32_t> kidx;
        std::unordered_map<const void*, std::string> item_names;   // item object -> name (one reflection call per item)
        ULONGLONG t_begin = 0;
    };

    // WHAT: all registry state (heap, never freed: the engine may call the delete listener at shutdown).
    struct RegState
    {
        RockReg tab;
        std::vector<UObject*> save;        // per rock index: save object of the running pass (alive_seh before each use), nullptr = not bound
        std::vector<uint64_t> seen;        // per rock index: seed pass that last saw it
        std::vector<size_t> act_list;      // rock indices with a loaded actor handle
        std::vector<size_t> suspect;       // rock indices whose actor was deleted (kill only if the save is gone)
        std::vector<UObject*> seed_list;   // FindAllOf result of the running pass
        std::vector<UObject*> need;        // saves of the running pass that need a full read (new or revived, or cache rocks to re-read)
        std::vector<int> ring;             // per need item (filled by classify): seed ring, REG_RING_FAR = no ship in its system
        std::vector<size_t> order;         // need indices, nearest ring first
        std::vector<RegSrc> src;           // ships of the running pass (system, chunk)
        size_t bind_cur = 0, cls_cur = 0, ord_cur = 0, near_n = 0;   // cursors of the running pass
        size_t live_n = 0;                 // live saves seen by the running pass
        size_t changed = 0;                // rocks added or revived by the running pass
        bool seeding = false;
        bool ordered = false, near_done = false, sample_done = true;
        bool cache_tried = false;          // the cache file is read once per session (first pass)
        bool cache_pending = false;        // a cache was loaded: the next pass runs its sample check
        bool cache_ok = false;             // the cache file matches the table (loaded or written): no rewrite unless the table changed a lot
        uint64_t cache_save_n = 0;         // save count stored in the loaded cache
        uint64_t exe_size = 0;             // size of the game exe (cache key)
        bool notready_logged = false;      // 'REG area not ready' logged for this pass
        CacheWrite cw;
        uint64_t pass = 0;
        std::set<int> sys_done;            // systems a pass has covered (started with a mining ship there)
        int world_sys = 0;                 // player's system at the last trigger check (0 = unknown)
        bool world_ok = false;             // a game world is loaded (world_sys > 0): seeds may start
        bool world_wait = false;           // world changed: the next seed waits REG_EMPTY_RETRY_MS (freed saves of the old world)
        RegWorldTrack wt;                  // world identity (controller + PlayerObject), debounced no-world count
        bool seed_due = false;             // a seed is wanted; kept while the cache writer or a pass runs
        bool in_tick = false;             // set during registry_tick; still set at entry = the last tick faulted
        int tick_faults = 0;               // consecutive faulted ticks
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

    // WHAT: the executable size (cache key). 0 when unreadable (the cache is then not used).
    uint64_t reg_exe_size()
    {
        wchar_t b[1024] = {0};
        if (!GetModuleFileNameW(nullptr, b, 1024)) return 0;
        std::error_code ec;
        const auto n = std::filesystem::file_size(b, ec);
        return ec ? 0 : static_cast<uint64_t>(n);
    }

    // WHAT: display name of an ore item (run under SEH). "?" when the call faults.
    struct RegNameCtx { UObject* item = nullptr; std::wstring out; };
    std::string reg_item_name(UObject* item)
    {
        RegNameCtx c;
        c.item = item;
        if (!seh_run([](void* p) { auto* x = static_cast<RegNameCtx*>(p); x->out = item_kind(x->item); }, &c)) return "?";
        return u8(c.out);
    }

    // WHAT: ore name (UTF-8) of a rock: its first ore item, else the name read from the cache, "?" when none.
    std::string reg_ore_name(const RegState& s, const RegRock& r)
    {
        for (void* it : r.items) if (it) return reg_item_name(static_cast<UObject*>(it));
        if (r.ore >= 0 && static_cast<size_t>(r.ore) < s.tab.kinds.size()) return s.tab.kinds[static_cast<size_t>(r.ore)];
        return "?";
    }

    // WHAT: ore name of a save object (its first ore item), "?" when none. Used by the cache sample.
    std::string reg_save_ore(UObject* o)
    {
        auto* arr = o->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("ResourceItems"));
        for (int32_t k = 0; arr && k < clamp_tarray_num(arr->Num()); ++k) if ((*arr)[k]) return reg_item_name((*arr)[k]);
        return "?";
    }

    // WHAT: reads the full data of one save object into the table (skipped when dead, not alive, no system, no chunk, no id).
    //       true when the rock was added or revived.
    bool reg_seed_one(RegState& s, UObject* o)
    {
        if (!alive_seh(o)) return false;
        bool al = true;
        get_bool_prop(o, STR("bAlive"), al);
        if (!al) return false;
        const int sys = sys_id(o);
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        if (sys <= 0 || !get_chunk(o, ch) || !get_loc(o, loc)) return false;
        const int id = obj_id(o);
        if (id <= 0) return false;
        std::vector<void*> items;
        auto* arr = o->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("ResourceItems"));
        for (int32_t k = 0; arr && k < clamp_tarray_num(arr->Num()); ++k) if ((*arr)[k]) items.push_back((*arr)[k]);
        double c[3] = {0, 0, 0};
        reg_abs_m(ch, loc, g_raw_per_m, c);
        const size_t i = s.tab.add(id, sys, c, items);
        reg_side(s, i);
        s.tab.rocks[i].unverified = false;
        s.save[i] = o;
        s.seen[i] = s.pass;
        return true;
    }

    // WHAT: rocks alive in the table (count for the seed retry and the stats).
    size_t reg_alive_count(const RegState& s)
    {
        size_t n = 0;
        for (const RegRock& r : s.tab.rocks) if (r.alive) ++n;
        return n;
    }

    // WHAT: the player's system id (run under seh_run). sys = 0 when unreadable.
    struct RegWorldCtx { int sys = 0; uint64_t id = 0; };
    void reg_world_fn(void* p)
    {
        auto* x = static_cast<RegWorldCtx*>(p);
        int64_t pch[3] = {0, 0, 0};
        int ps = 0;
        if (ch_player(ps, pch)) x->sys = ps;
        if (x->sys > 0)
        {
            UObject* c = ch_ctrl();
            UObject* po = get_obj_prop(c, STR("PlayerObject"));
            x->id = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(c)) * 1000003ull) ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(po));
            if (x->id == 0) x->id = 1;
        }
    }

    // WHAT: frees the lists of the running pass (after a finish or an abort).
    void reg_seed_release(RegState& s)
    {
        std::vector<UObject*>().swap(s.seed_list);
        std::vector<UObject*>().swap(s.need);
        std::vector<int>().swap(s.ring);
        std::vector<size_t>().swap(s.order);
        s.src.clear();
        s.bind_cur = s.cls_cur = s.ord_cur = s.near_n = 0;
        s.ordered = false;
    }

    // WHAT: loads the cache file into the table: one read, one parse. The entries are unverified until a pass binds them (and samples them).
    // WHY: skips the ~3.5 min seed when the file still matches the game; a bad file is ignored and the normal seed runs.
    void reg_cache_load(RegState& s)
    {
        if (s.exe_size == 0) return;
        const auto path = get_dll_dir() / REG_CACHE_FILE;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) { log_line(STR("REG cache none (the first full seed writes it)")); return; }
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        try
        {
            std::vector<uint8_t> img;
            {
                std::ifstream f(path, std::ios::binary);
                if (!f) throw std::runtime_error("open");
                f.seekg(0, std::ios::end);
                const auto sz = static_cast<size_t>(f.tellg());
                f.seekg(0, std::ios::beg);
                img.resize(sz);
                if (sz && !f.read(reinterpret_cast<char*>(img.data()), static_cast<std::streamsize>(sz))) throw std::runtime_error("read");
            }
            RegCacheData d;
            std::string why;
            if (!reg_cache_parse(img.data(), img.size(), s.exe_size, d, why))
            {
                log_line(STR("REG cache ignored: ") + widen(why));
                return;
            }
            std::vector<uint8_t>().swap(img);
            s.tab.kinds = d.kinds;
            for (const RegCacheRec& c : d.recs)
            {
                const size_t i = s.tab.add(c.id, c.sys, c.c, {});
                reg_side(s, i);
                RegRock& r = s.tab.rocks[i];
                r.ore = c.ore;
                r.unverified = true;
                if (c.rad >= 0.0f) s.tab.set_radius(i, static_cast<double>(c.rad));
            }
            s.cache_pending = true;
            s.cache_save_n = d.h.save_n;
            s.cache_ok = true;
            log_line(STR("REG cache load n=") + std::to_wstring(d.recs.size()) + STR(" saves=") + std::to_wstring(d.h.save_n) + STR(" ms=") +
                     std::to_wstring(perf_ms_since(t0)) + STR(" (from cache, unverified)"));
        }
        catch (...)
        {
            log_line(STR("REG cache load fault: file ignored"));
        }
    }

    // WHAT: starts a pass: the cache is loaded (first pass only), one FindAllOf of SmallResourceAsteroidObject (all systems), the zones of the
    //       ships are set up. The saves are then processed in slices (reg_seed_slice).
    void reg_seed_begin(RegState& s, const std::vector<RegShip>& ships)
    {
        s.seed_ms = GetTickCount64();   // set first: a faulted find is retried at the next trigger, not every tick
        if (!s.cache_tried)
        {
            s.cache_tried = true;
            s.exe_size = reg_exe_size();
            reg_cache_load(s);
        }
        for (const RegShip& sh : ships) if (sh.sys > 0) s.sys_done.insert(sh.sys);
        LARGE_INTEGER a;
        QueryPerformanceCounter(&a);
        std::vector<UObject*> v;
        const bool found = find_all_seh(STR("SmallResourceAsteroidObject"), v);
        const double find_ms = perf_ms_since(a);
        if (!found)
        {
            log_line(STR("REG seed: FindAllOf faulted: ") + seh_describe(g_seh));
            return;
        }
        s.seed_list.swap(v);
        s.src.clear();
        for (const RegShip& sh : ships)
        {
            if (sh.sys <= 0) continue;
            RegSrc x;
            x.sys = sh.sys;
            reg_chunk_of_m(sh.p, x.c);
            s.src.push_back(x);
        }
        ++s.pass;
        s.tab.zones_begin(s.pass, s.src, REG_PRIORITY_CHUNKS);
        s.bind_cur = s.cls_cur = s.ord_cur = s.near_n = 0;
        s.live_n = 0;
        s.changed = 0;
        s.need.clear();
        s.ring.clear();
        s.order.clear();
        s.sample_done = !s.cache_pending;
        s.ordered = false;
        s.near_done = false;
        s.notready_logged = false;
        s.seeding = true;
        log_line(STR("REG seed start n=") + std::to_wstring(s.seed_list.size()) + STR(" pass=") + std::to_wstring(s.pass) + STR(" find_ms=") +
                 std::to_wstring(find_ms) + STR(" ships=") + std::to_wstring(s.src.size()));
    }

    // WHAT: ends the running pass without finishing it (a mining ship in a new system). The table is kept; the next pass binds again.
    void reg_seed_abort(RegState& s)
    {
        s.seeding = false;
        reg_seed_release(s);
    }

    // WHAT: drops every game object pointer of the registry (save objects of the table, lists of the running pass). The table rows stay.
    // WHY: after a world load the save objects are freed; a later read through them faults (mb_rocks.hpp: alive_seh cannot see freed UObjects).
    void reg_seed_unbind(RegState& s)
    {
        std::fill(s.save.begin(), s.save.end(), nullptr);
        reg_seed_release(s);
    }

    // WHAT: bind of one save: alive and bAlive, obj_id. A known alive rock is bound (save pointer, seen); any other save goes to 'need'.
    void reg_bind_one(RegState& s, UObject* o)
    {
        if (!alive_seh(o)) return;
        bool al = true;
        get_bool_prop(o, STR("bAlive"), al);
        if (!al) return;
        const int id = obj_id(o);
        if (id <= 0) return;
        ++s.live_n;
        const size_t i = s.tab.index_of(id);
        if (i != RockReg::npos && s.tab.rocks[i].alive)
        {
            reg_side(s, i);
            s.save[i] = o;
            s.seen[i] = s.pass;
            return;
        }
        s.need.push_back(o);
    }

    // WHAT: one-shot sample check of a loaded cache: the unverified rocks nearest to the ships and as many random ones are read (position,
    //       ore name) and compared with the table. reg_cache_verdict decides: trust, merge (new reads only) or reseed (all cache rocks re-read).
    // WHY: the file is trusted only after the game's own saves agree with it.
    void reg_sample_stage(RegState& s)
    {
        s.sample_done = true;
        s.cache_pending = false;
        std::vector<size_t> cand;
        for (size_t i = 0; i < s.tab.rocks.size(); ++i)
        {
            const RegRock& r = s.tab.rocks[i];
            if (r.unverified && r.alive && i < s.save.size() && s.save[i]) cand.push_back(i);
        }
        // nearest to any ship (same system), then random ones
        std::vector<std::pair<double, size_t>> by_dist;
        by_dist.reserve(cand.size());
        for (size_t i : cand)
        {
            const RegRock& r = s.tab.rocks[i];
            double best = 1e300;
            for (const RegShip& sh : s.ships)
            {
                if (sh.sys != r.sys) continue;
                const double d[3] = {r.c[0] - sh.p[0], r.c[1] - sh.p[1], r.c[2] - sh.p[2]};
                best = std::min(best, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
            }
            by_dist.push_back({best, i});
        }
        const size_t kn = std::min(REG_CACHE_SAMPLE_N, by_dist.size());
        std::partial_sort(by_dist.begin(), by_dist.begin() + static_cast<std::ptrdiff_t>(kn), by_dist.end());
        std::set<size_t> pick;
        for (size_t k = 0; k < kn; ++k) if (by_dist[k].first < 1e300) pick.insert(by_dist[k].second);
        std::mt19937_64 rng(static_cast<uint64_t>(GetTickCount64()));
        for (size_t t = 0; !cand.empty() && pick.size() < 2 * REG_CACHE_SAMPLE_N && t < 8 * REG_CACHE_SAMPLE_N; ++t) pick.insert(cand[rng() % cand.size()]);

        struct SampleCtx { RegState* s = nullptr; std::vector<size_t> idx; size_t checked = 0, bad = 0; };
        SampleCtx x;
        x.s = &s;
        x.idx.assign(pick.begin(), pick.end());
        const bool ok = seh_run([](void* p)
        {
            auto* c = static_cast<SampleCtx*>(p);
            RegState& st = *c->s;
            for (size_t i : c->idx)
            {
                const RegRock& r = st.tab.rocks[i];
                UObject* o = st.save[i];
                ++c->checked;
                int64_t ch[3] = {0, 0, 0};
                double loc[3] = {0, 0, 0}, am[3] = {0, 0, 0};
                if (!alive(o) || !get_chunk(o, ch) || !get_loc(o, loc)) { ++c->bad; continue; }
                reg_abs_m(ch, loc, g_raw_per_m, am);
                const std::string want = r.ore >= 0 ? st.tab.kinds[static_cast<size_t>(r.ore)] : std::string("?");
                if (!reg_same_pos(am, r.c) || reg_save_ore(o) != want) ++c->bad;
            }
        }, &x);
        if (!ok) ++x.bad;
        const RegCacheVerdict v = reg_cache_verdict(s.cache_save_n, s.live_n, x.checked, x.bad);
        if (v == RCV_RESEED)
        {
            for (size_t i : cand) if (s.save[i]) s.need.push_back(s.save[i]);
            log_line(STR("REG cache reseed: sample checked=") + std::to_wstring(x.checked) + STR(" bad=") + std::to_wstring(x.bad) +
                     STR(" cached rocks read again=") + std::to_wstring(cand.size()));
            return;
        }
        for (size_t i : cand) s.tab.rocks[i].unverified = false;
        log_line(widen(std::string(v == RCV_TRUST ? "REG cache trusted" : "REG cache merged") + " saves=" + std::to_string(s.live_n) +
                       " cache_saves=" + std::to_string(s.cache_save_n) + " checked=" + std::to_string(x.checked) + " bad=0 bound=" +
                       std::to_string(cand.size())));
    }

    // WHAT: ring of one 'need' save: system and chunk (cheap reads). A save that cannot be read gets REG_RING_FAR (it is also skipped by the read).
    void reg_classify_one(RegState& s, size_t i)
    {
        UObject* o = s.need[i];
        int ring = REG_RING_FAR;
        int64_t ch[3] = {0, 0, 0};
        if (alive_seh(o))
        {
            const int sys = sys_id(o);
            if (sys > 0 && get_chunk(o, ch)) ring = reg_ring_of(sys, ch, s.src);
        }
        s.ring.push_back(ring);
    }

    // WHAT: phase A check: when every save of rings <= REG_PRIORITY_CHUNKS is read, the zones of the pass are ready and the line is logged once.
    void reg_near_check(RegState& s)
    {
        if (s.near_done || !s.ordered || s.ord_cur < s.near_n) return;
        s.near_done = true;
        s.tab.zones_ready(s.pass);
        log_line(STR("REG seed near done n=") + std::to_wstring(s.near_n) + STR(" ms=") + std::to_wstring(GetTickCount64() - s.seed_ms) +
                 STR(" pass=") + std::to_wstring(s.pass));
    }

    // WHAT: ends a pass: rocks it did not see are killed; the self-check is due; the cache is written when it is missing or the table changed a lot.
    void reg_seed_finish(RegState& s)
    {
        s.seeding = false;
        // a pass that found no save at all (world not ready) kills nothing: every rock would look gone
        const size_t gone = s.live_n ? reg_kill_unseen(s.tab, s.seen, s.pass, s.ticks) : 0;
        s.tab.zones_ready(s.pass);
        s.self_due = true;
        const size_t alive = reg_alive_count(s);
        const size_t changed = s.changed + gone;
        log_line(STR("REG seed done n=") + std::to_wstring(alive) + STR(" saves=") + std::to_wstring(s.live_n) + STR(" new=") +
                 std::to_wstring(s.changed) + STR(" gone=") + std::to_wstring(gone) + STR(" pass=") + std::to_wstring(s.pass) + STR(" ms=") +
                 std::to_wstring(GetTickCount64() - s.seed_ms));
        if (alive > 0 && (!s.cache_ok || static_cast<double>(changed) > REG_CACHE_CHANGE_FRAC * static_cast<double>(alive)))
        {
            // the writer runs over the next ticks (cache_write_step): tmp file, placeholder header, records
            CacheWrite& w = s.cw;
            w.dst = get_dll_dir() / REG_CACHE_FILE;
            w.tmp = w.dst;
            w.tmp += L".tmp";
            if (_wfopen_s(&w.f, w.tmp.c_str(), L"wb") != 0) w.f = nullptr;
            if (!w.f) log_line(STR("REG cache write: cannot open the temp file"));
            else
            {
                uint8_t z[REG_CACHE_HDR] = {0};
                std::fwrite(z, 1, sizeof z, w.f);
                w.on = true;
                w.save_n = s.live_n;
                w.next = 0;
                w.rock_n = 0;
                w.sum = REG_CACHE_FNV0;
                w.buf.clear();
                w.kinds.clear();
                w.kidx.clear();
                w.item_names.clear();
                w.t_begin = GetTickCount64();
            }
        }
        reg_seed_release(s);
    }

    // WHAT: writer: flushes the pending bytes to the temp file.
    void cache_flush(CacheWrite& w)
    {
        if (w.buf.empty()) return;
        if (std::fwrite(w.buf.data(), 1, w.buf.size(), w.f) != w.buf.size()) throw std::runtime_error("write");
        w.sum = reg_cache_fnv(w.sum, w.buf.data(), w.buf.size());
        w.buf.clear();
    }

    // WHAT: writer abort: closes and removes the temp file; the old cache (if any) stays.
    void cache_write_abort(RegState& s, const char* why)
    {
        CacheWrite& w = s.cw;
        if (w.f) std::fclose(w.f);
        w.f = nullptr;
        w.on = false;
        std::error_code ec;
        std::filesystem::remove(w.tmp, ec);
        log_line(widen(std::string("REG cache write failed: ") + why));
    }

    // WHAT: writer: the tail (kinds, header with the checksum, close, rename). Run when every rock has been written.
    void cache_write_finish(RegState& s)
    {
        CacheWrite& w = s.cw;
        const uint64_t kofs = REG_CACHE_HDR + w.rock_n * REG_CACHE_REC;
        reg_cache_put_kinds(w.buf, w.kinds);
        cache_flush(w);
        RegCacheHdr h;
        h.exe_size = s.exe_size;
        h.save_n = w.save_n;
        h.rock_n = w.rock_n;
        h.kinds_off = kofs;
        h.sum = w.sum;
        uint8_t hb[REG_CACHE_HDR];
        reg_cache_put_hdr(hb, h);
        if (std::fseek(w.f, 0, SEEK_SET) != 0 || std::fwrite(hb, 1, sizeof hb, w.f) != sizeof hb) throw std::runtime_error("header");
        const bool closed = std::fclose(w.f) == 0;
        w.f = nullptr;
        w.on = false;
        if (!closed) throw std::runtime_error("close");
        std::filesystem::rename(w.tmp, w.dst);
        s.cache_ok = true;
        log_line(STR("REG cache written rocks=") + std::to_wstring(w.rock_n) + STR(" saves=") + std::to_wstring(w.save_n) + STR(" names=") +
                 std::to_wstring(w.kinds.size()) + STR(" ms=") + std::to_wstring(GetTickCount64() - w.t_begin));
    }

    // WHAT: one writer tick: up to budget_ms of records (encoded into buf, flushed in 64 KB pieces). Finishes when every rock is written.
    // WHY: the writer never blocks a tick for long; item names are read once per item object.
    void cache_write_step(RegState& s, double budget_ms)
    {
        CacheWrite& w = s.cw;
        if (!w.on) return;
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        try
        {
            while (w.next < s.tab.rocks.size() && !reg_over_budget(perf_ms_since(t0), budget_ms))
            {
                const RegRock& r = s.tab.rocks[w.next++];
                if (!r.alive) continue;
                RegCacheRec c;
                c.id = r.id;
                c.sys = r.sys;
                std::copy(r.c, r.c + 3, c.c);
                c.rad = r.rad_known ? static_cast<float>(r.rad_m) : -1.0f;
                if (!r.items.empty() && r.items[0])
                {
                    auto it = w.item_names.find(r.items[0]);
                    if (it == w.item_names.end()) it = w.item_names.emplace(r.items[0], reg_item_name(static_cast<UObject*>(r.items[0]))).first;
                    c.ore = -1;
                    const std::string& name = it->second;
                    auto kf = w.kidx.find(name);
                    if (kf == w.kidx.end())
                    {
                        c.ore = static_cast<int32_t>(w.kinds.size());
                        w.kidx[name] = c.ore;
                        w.kinds.push_back(name);
                    }
                    else c.ore = kf->second;
                }
                else if (r.ore >= 0 && static_cast<size_t>(r.ore) < s.tab.kinds.size())
                {
                    const std::string& name = s.tab.kinds[static_cast<size_t>(r.ore)];
                    auto kf = w.kidx.find(name);
                    if (kf == w.kidx.end())
                    {
                        c.ore = static_cast<int32_t>(w.kinds.size());
                        w.kidx[name] = c.ore;
                        w.kinds.push_back(name);
                    }
                    else c.ore = kf->second;
                }
                else c.ore = -1;
                uint8_t rec[REG_CACHE_REC];
                reg_cache_put_rec(rec, c);
                w.buf.insert(w.buf.end(), rec, rec + REG_CACHE_REC);
                ++w.rock_n;
                if (w.buf.size() >= 65536) cache_flush(w);
            }
            if (w.next >= s.tab.rocks.size()) cache_write_finish(s);
        }
        catch (...)
        {
            cache_write_abort(s, "I/O");
        }
    }

    // WHAT: seed budget of this tick: REG_SEED_A_BUDGET_MS during phase A of the first REG_PHASE_A_MS, else REG_BUDGET_MS.
    double reg_seed_budget(const RegState& s, ULONGLONG now)
    {
        return (!s.near_done && now - s.seed_ms < REG_PHASE_A_MS) ? REG_SEED_A_BUDGET_MS : REG_BUDGET_MS;
    }

    // WHAT: the seed slice of this tick: steps through the pass (bind, sample, classify, order, read) until the budget is spent.
    void reg_seed_slice(RegState& s, double budget)
    {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        while (s.seeding && !reg_over_budget(perf_ms_since(t0), budget))
        {
            if (s.bind_cur < s.seed_list.size()) { reg_bind_one(s, s.seed_list[s.bind_cur++]); continue; }
            if (!s.sample_done) { reg_sample_stage(s); continue; }
            if (s.cls_cur < s.need.size()) { reg_classify_one(s, s.cls_cur++); continue; }
            if (!s.ordered)
            {
                reg_order_rings(s.ring, REG_PRIORITY_CHUNKS, s.order, s.near_n);
                s.ordered = true;
                reg_near_check(s);
                continue;
            }
            if (s.ord_cur < s.order.size())
            {
                if (reg_seed_one(s, s.need[s.order[s.ord_cur++]])) ++s.changed;
                reg_near_check(s);
                continue;
            }
            reg_seed_finish(s);
        }
    }

    // WHAT: kills a rock when its save object is gone. Suspects (actor deleted) are checked first, then targeted rocks, every tick.
    // WHY: an actor delete alone is a stream-out, not a destroy: only the save object decides. A rock not bound yet (no save pointer) is not checked.
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
            if (i < s.save.size() && s.tab.rocks[i].alive && s.save[i] && !alive_seh(s.save[i])) s.tab.kill(s.tab.rocks[i].id, s.ticks);
        }
        s.suspect.swap(keep);
        for (int id : want)
        {
            const size_t i = s.tab.index_of(id);
            if (i == RockReg::npos || !s.tab.rocks[i].alive || i >= s.save.size() || !s.save[i]) continue;
            if (!alive_seh(s.save[i])) s.tab.kill(id, s.ticks);
        }
    }

    // WHAT: GetRadius of the rocks near a ship (or targeted), REG_RAD_TICK per tick. Each rock once; zero is cached too.
    void reg_radius_stage(RegState& s, const std::vector<RegShip>& ships)
    {
        std::vector<size_t> want;
        for (const RegShip& sh : ships) if (sh.has_seg) s.tab.radius_wanted_seg(sh.sys, sh.p, sh.q, REG_RAD_SEG_M, REG_RAD_SEG_TICK, want);
        for (const RegShip& sh : ships) s.tab.radius_wanted(sh.sys, {{sh.p[0], sh.p[1], sh.p[2]}}, REG_RAD_NEAR_M, want.size() + REG_RAD_TICK, want);
        for (int id : s.tab.targeted())
        {
            const size_t i = s.tab.index_of(id);
            if (i != RockReg::npos && !s.tab.rocks[i].rad_known && want.size() < REG_RAD_SEG_TICK + REG_RAD_TICK) want.push_back(i);
        }
        for (size_t i : want)
        {
            if (i >= s.save.size() || !s.tab.rocks[i].alive) continue;
            UObject* sv = s.save[i];
            if (!sv) continue;   // not bound yet
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
            if (s.save[i] && !alive_seh(s.save[i])) s.tab.kill(s.tab.rocks[i].id, s.ticks);
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
            const std::string ore = reg_ore_name(s, r);
            char b[512];
            std::snprintf(b, sizeof(b), "%d %s %.2f %.2f %.2f %.2f %.2f %d\n", r.id, ore.c_str(), r.c[0], r.c[1], r.c[2], pr.first,
                          r.rad_m, r.act ? 1 : 0);
            txt += b;
        }
        try { write_atomic(get_dll_dir() / name, txt); } catch (...) {}
        return out.size();
    }

    // WHAT: area check for the bubble and the path check: true when the area around p (m, system sys, one chunk each way) is seeded.
    //       false = unknown: the caller uses the rocks already in the table (the look-ahead sees only seeded rocks). Logs once per pass.
    bool reg_area_seeded(int sys, const double p[3])
    {
        RegState& s = reg_st();
        int64_t c[3] = {0, 0, 0};
        reg_chunk_of_m(p, c);
        if (s.tab.area_ready(sys, c, 1)) return true;
        if (!s.notready_logged)
        {
            s.notready_logged = true;
            log_line(STR("REG area not ready sys=") + std::to_wstring(sys) + STR(" chunk=") + std::to_wstring(c[0]) + STR(",") +
                     std::to_wstring(c[1]) + STR(",") + std::to_wstring(c[2]) + STR(" (seeded rocks only)"));
        }
        return false;
    }

    // WHAT: the registry tick (cmd_tick 0.25 s block). A writing tick runs the writer only. Otherwise: triggers, seed slice (own budget), then the
    //       other stages in order; a stage is skipped when its budget is spent.
    // WHY: the registry is the one updater, so no ship pays for rock reads of its own.
    void registry_tick(double dt, const std::vector<RegShip>& ships, const std::vector<int>& targets)
    {
        (void)dt;
        RegState& s = reg_st();
        if (s.in_tick)
        {
            // the previous tick never cleared the flag: it faulted (SEH in compat_run). Drop the pass and the object pointers.
            ++s.tick_faults;
            log_line(STR("REG tick faulted last time, seed aborted n=") + std::to_wstring(s.tick_faults));
            reg_seed_abort(s);
            reg_seed_unbind(s);
            s.seed_ms = GetTickCount64();
            s.world_wait = true;   // reseed after REG_EMPTY_RETRY_MS, not the 10 min refresh
        }
        struct TickGuard { RegState& s; ~TickGuard() { s.in_tick = false; } } guard{s};   // C++ exception clears the flag; an SEH fault does not unwind (flag stays)
        s.in_tick = true;
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        const ULONGLONG now = GetTickCount64();
        ++s.ticks;
        s.ships = ships;
        s.targets = targets;
        // world probe EVERY tick (one seh_run, the player's system only). A world load or a lost world (debounced) drops the pass and the save pointers; a warp does not
        // before any save is read this tick. WHY: the stages below read the save objects; freed ones must never be read after a world change.
        RegWorldCtx wc;
        const bool rd = seh_run(reg_world_fn, &wc) && wc.sys > 0;
        const RegWorldEv we = reg_world_step(s.wt, rd ? wc.id : 0, REG_NOWORLD_TICKS);
        if (rd) s.world_sys = wc.sys;   // a warp only updates the system: pointers stay bound
        s.world_ok = s.wt.ok;
        const bool world_bad = we != RWE_NONE || !s.world_ok || !rd;   // a failed read this tick: no save is read (freed objects), pointers stay until LOST/LOADED
        if (world_bad)
        {
            // save load or world gone: the old pass and the save pointers are dropped before any save is read this tick
            if (we != RWE_NONE)
            {
                log_line(std::wstring(STR("REG world ")) + (we == RWE_LOADED ? STR("loaded") : STR("lost")) + STR(", pointers dropped"));
                reg_seed_unbind(s);
                s.seed_due = false;   // a pending seed must not skip the REG_EMPTY_RETRY_MS wait after the new world
                s.world_sys = rd ? wc.sys : 0;
            }
            reg_seed_abort(s);
            s.seed_ms = now;
            s.world_wait = true;
        }
        if (!world_bad && !s.world_wait) reg_target_stage(s);   // deletes (suspects) and targeted rocks: always, cheap
        // seed triggers, no ship needed: first tick, 10 min refresh, world loaded/reseed after a fault, no alive rock (retry every 5 s),
        // a mining ship in a system no pass covered (also restarts a running pass)
        if (now - s.trig_ms >= REG_TRIG_MS)
        {
            s.trig_ms = now;
            if (s.world_wait && s.world_ok && now - s.seed_ms >= REG_EMPTY_RETRY_MS) { s.world_wait = false; s.seed_due = true; }
            if (!s.seeding && s.world_ok && reg_alive_count(s) == 0 && now - s.seed_ms >= REG_EMPTY_RETRY_MS) s.seed_due = true;
        }
        bool sys_new = false;
        if (now - s.sys_ms >= REG_SYS_MS)
        {
            s.sys_ms = now;
            for (const RegShip& sh : ships) if (sh.sys > 0 && !s.sys_done.count(sh.sys)) { sys_new = true; break; }
        }
        if (s.cw.on)
        {
            cache_write_step(s, REG_CACHE_WRITE_MS);   // a writing tick does the writer only
        }
        else
        {
            if (!s.seeding)
            {
                if (s.seed_ms == 0 || now - s.seed_ms >= REG_SEED_REFRESH_MS) s.seed_due = true;
                if ((s.seed_due || sys_new) && s.world_ok && !s.world_wait) { s.seed_due = false; reg_seed_begin(s, ships); }
            }
            else if (sys_new && s.world_ok && !s.world_wait)
            {
                reg_seed_abort(s);
                reg_seed_begin(s, ships);
            }
            if (s.seeding) s.seed_due = false;
            if (s.seeding && !s.world_ok) { reg_seed_abort(s); reg_seed_unbind(s); s.seed_ms = now; }
            if (s.seeding) reg_seed_slice(s, reg_seed_budget(s, now));
            // stats and dump: not budget-gated (cheap, once a minute)
            LARGE_INTEGER t1;
            QueryPerformanceCounter(&t1);
            if (now - s.stats_ms >= REG_STATS_MS)
            {
                s.stats_ms = now;
                reg_stats_stage(s);
                if (!ships.empty()) reg_dump_file(s, ships[0].sys, ships[0].p, L"MiningRegistry.txt");
            }
            if (!reg_over_budget(perf_ms_since(t1))) reg_validate_stage(s, ships);
            if (!world_bad && !s.world_wait && !reg_over_budget(perf_ms_since(t1))) reg_radius_stage(s, ships);
            if (!reg_over_budget(perf_ms_since(t1))) reg_actor_stage(s, ships);
            if (!reg_over_budget(perf_ms_since(t1))) reg_self_stage(s);
            if (s.self_ms == 0 && !s.seeding) s.self_ms = now;   // the periodic self-check counts from the first full pass
            if (!s.seeding && !s.self_due && now - s.self_ms >= REG_SELF_MS) { s.self_due = true; }
        }
        const double ms = perf_ms_since(t0);
        s.ms_sum += ms;
        if (ms > s.ms_worst) s.ms_worst = ms;
        ++s.stat_ticks;
        if (reg_over_budget(ms, REG_SEED_A_BUDGET_MS) && (s.slow_ms == 0 || now - s.slow_ms >= REG_SLOW_LOG_MS))
        {
            s.slow_ms = now;
            log_line(STR("REG slow ms=") + std::to_wstring(ms) + STR(" rocks=") + std::to_wstring(s.tab.rocks.size()));
        }
        s.tick_faults = 0;
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

    // WHAT: ore name of each query row (run under seh_run): reg_ore_name of the row's rock, "?" when none.
    struct RegOreCtx { const RegState* s = nullptr; const std::vector<std::pair<double, size_t>>* rows = nullptr; std::vector<std::string>* ore = nullptr; };
    void reg_ore_fn(void* p)
    {
        auto* x = static_cast<RegOreCtx*>(p);
        for (const auto& pr : *x->rows) x->ore->push_back(reg_ore_name(*x->s, x->s->tab.rocks[pr.second]));
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
        std::vector<std::string> ore;
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
            const std::string& ot = ore[k];
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
