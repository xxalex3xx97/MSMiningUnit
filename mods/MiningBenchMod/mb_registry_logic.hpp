// FILE: mods/MiningBenchMod/mb_registry_logic.hpp
// WHAT: pure rock registry. Table of rocks (seeded from save data, centre in metres, radius in metres, ore items, alive flag, optional actor
//       handle kept as an opaque pointer), a chunk-cell grid index per system, local queries (within, nearest, nearest_n, nearest_surface,
//       first_blocker), destroy event ring with a sequence number, targeted-rock set, floating ore table, per-tick time budget test,
//       seed ordering by chunk rings (nearest first), seeded areas (zones), kill of rocks a pass did not see, and the disk cache format
//       (encode, parse, verdict of a sample check).
// WHY separate: no UE / Windows calls, so tests/test_logic.cpp and the benchmark run it alone. The game side (mb_registry.hpp) fills and
//       updates it; ships only read it.
// DEPENDS ON (exact): mb_logic.hpp (PathRock, path_first_blocker, seg_point_dist, path_box_reject, PATH_RMAX_M), std headers.
// UNITS: all positions and radii in METRES, absolute frame = chunk * REG_CHUNK_M + loc_raw / raw_per_m (REG_CHUNK_M = 2048 m = 134217728 raw
//        per chunk; raw_per_m = g_raw_per_m, 65536 by default). Same frame as DOCK_CHUNK_M in mb_mining.hpp.
// GRID: cell = REG_CHUNK_M cube per (system, cell x, y, z). A query visits only the cells its AABB overlaps; a query whose cell count
//        exceeds REG_GRID_MAX_CELLS scans the system list instead (correct, slower).
// POINTERS: act / item / key are opaque. This file never dereferences them.
// SEED ORDER: a save's ring = chunk distance (Chebyshev, chunks) to the nearest ship of its system at pass start; no ship in the system = far
//        (last). Phase A = rings <= REG_PRIORITY_CHUNKS (read first). Zones record the seeded areas (reg_area_ready).
// CACHE FILE (MiningRegistryCache.bin): header 48 B (magic, version, exe size, save count, rock count, kinds offset, FNV-1a checksum of
//        everything after the header), rock_n records of 40 B (id, sys, abs x y z as double m, radius float m or -1, ore index or -1),
//        then the ore name table (u32 count, per name u16 length + UTF-8 bytes). Little endian (x86).
// USED BY: mb_registry.hpp (game side, fills and reads), tests/test_logic.cpp (checks + benchmark).
#pragma once
#include "mb_logic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    // WHAT: save chunk edge (m). 134217728 raw per chunk at 65536 raw per m.
    constexpr double REG_CHUNK_M = 2048.0;
    // WHAT: destroy events kept for events_since (ring size).
    constexpr int REG_EVENT_CAP = 64;
    // WHAT: a grid query over more cells than this scans the system list instead.
    constexpr size_t REG_GRID_MAX_CELLS = 4096;
    // WHAT: first search radius of the expanding nearest search (m); it grows x4 up to REG_SEARCH_MAX_M.
    constexpr double REG_SEARCH_START_M = 4096.0;
    constexpr double REG_SEARCH_MAX_M = 1.0e7;
    // WHAT: largest rock radius assumed by nearest_surface (m). Rocks seen so far: 8 m and 70 m. Bigger rocks would be found late.
    constexpr double REG_RAD_MAX_M = 1000.0;
    // WHAT: registry work budget per tick (ms) and the per-tick caps of each stage.
    constexpr double REG_BUDGET_MS = 1.0;
    constexpr double REG_SEED_A_BUDGET_MS = 3.0;   // seed budget while phase A runs (first REG_PHASE_A_MS of a pass)
    constexpr uint64_t REG_PHASE_A_MS = 60000;     // phase A window from the pass start
    constexpr double REG_CACHE_WRITE_MS = 1.5;     // cache write budget per tick (writing ticks run the writer only)
    constexpr size_t REG_RAD_TICK = 10;         // GetRadius reads per tick
    constexpr size_t REG_ALIVE_TICK = 64;       // round-robin alive checks per tick
    constexpr size_t REG_K2_TICK = 50;          // K2_GetActorLocation / ore reads per tick
    constexpr size_t REG_VALID_TICK = 10;       // save validations (2 km of a ship) per tick
    constexpr double REG_NEAR_M = 2000.0;       // a ship this close validates the save object of a rock (game side)
    constexpr double REG_RAD_NEAR_M = 5000.0;   // radius reads only for rocks this close to a ship or to a targeted rock

    // WHAT: seed order and seeded areas. Phase A = saves within REG_PRIORITY_CHUNKS chunks (Chebyshev) of a ship of their system.
    //       Rings above REG_RING_CAP count as REG_RING_CAP; a save in a system with no ship is REG_RING_FAR (last).
    constexpr int REG_PRIORITY_CHUNKS = 12;
    constexpr int REG_RING_CAP = 4096;
    constexpr int REG_RING_FAR = REG_RING_CAP + 1;
    constexpr size_t REG_ZONE_KEEP = 32;        // ready zones kept (oldest dropped)
    // WHAT: cache check: tolerance of a sampled position (m, 1 cm), sample size per part (nearest + random), and the rewrite threshold.
    constexpr double REG_CACHE_TOL_M = 0.01;
    constexpr size_t REG_CACHE_SAMPLE_N = 50;
    constexpr double REG_CACHE_CHANGE_FRAC = 0.005;   // a seed that changed more than 0.5% of the rocks rewrites the cache

    // WHAT: one rock of the table. id = save object id (ObjectId). c = centre (m, absolute). rad_m valid when rad_known (0 = read and zero).
    // WHY items: the ore items of the save object (ResourceItems), opaque keys. act: loaded actor handle or nullptr (game side only).
    // ore: index into RockReg::kinds (ore name from the cache) or -1; unverified: loaded from the cache and not yet checked by a pass.
    struct RegRock
    {
        int id = 0;
        int sys = 0;
        double c[3] = {0, 0, 0};
        double rad_m = 0.0;
        bool rad_known = false;
        bool alive = true;
        void* act = nullptr;
        std::vector<void*> items;
        uint64_t destroyed_at = 0;   // tick of the destroy event (0 = never)
        int32_t ore = -1;
        bool unverified = false;
    };

    // WHAT: one destroy event: rock id, tick it was seen, sequence number (1, 2, ...).
    struct RegEvent { int id = 0; uint64_t tick = 0; uint64_t seq = 0; };

    // WHAT: blocking/surface radius (m) of a registry rock: its read radius, or REG_RAD_UNKNOWN_M until the radius is read.
    // WHY: an unread rock counts as big as the conservative default, so the path check never misses it.
    inline double reg_eff_rad(const RegRock& r) { return r.rad_known ? std::max(0.0, r.rad_m) : REG_RAD_UNKNOWN_M; }

    // WHAT: one floating ore entry: opaque key (actor pointer), position (m), alive flag.
    struct RegOre { const void* key = nullptr; double p[3] = {0, 0, 0}; bool alive = true; };

    // WHAT: grid cell key: system and cell index (floor(metres / REG_CHUNK_M)) per axis.
    struct RegCell
    {
        int sys; int64_t x, y, z;
        bool operator==(const RegCell& o) const { return sys == o.sys && x == o.x && y == o.y && z == o.z; }
    };
    struct RegCellHash
    {
        size_t operator()(const RegCell& c) const
        {
            uint64_t h = static_cast<uint64_t>(c.sys) * 0x9E3779B97F4A7C15ull;
            h ^= static_cast<uint64_t>(c.x) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
            h ^= static_cast<uint64_t>(c.y) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
            h ^= static_cast<uint64_t>(c.z) * 0x27D4EB2F165667C5ull + (h << 6) + (h >> 2);
            return static_cast<size_t>(h);
        }
    };

    // WHAT: cell index of a metre position on one axis.
    inline int64_t reg_cell_idx(double v) { return static_cast<int64_t>(std::floor(v / REG_CHUNK_M)); }

    // WHAT: absolute metres of a save object position: chunk (3 ints) * REG_CHUNK_M + loc_raw / raw_per_m.
    // WHY: save Location is chunk-relative; this is the only conversion the table uses.
    inline void reg_abs_m(const int64_t ch[3], const double loc_raw[3], double raw_per_m, double out[3])
    {
        for (int i = 0; i < 3; ++i) out[i] = static_cast<double>(ch[i]) * REG_CHUNK_M + loc_raw[i] / raw_per_m;
    }

    // WHAT: chunk of a metre position (floor(m / REG_CHUNK_M) per axis).
    inline void reg_chunk_of_m(const double p[3], int64_t c[3])
    {
        for (int i = 0; i < 3; ++i) c[i] = static_cast<int64_t>(std::floor(p[i] / REG_CHUNK_M));
    }

    // WHAT: Chebyshev distance (chunks) of two chunk triples.
    inline int64_t reg_cheb(const int64_t a[3], const int64_t b[3])
    {
        int64_t m = 0;
        for (int i = 0; i < 3; ++i)
        {
            int64_t d = a[i] - b[i];
            if (d < 0) d = -d;
            if (d > m) m = d;
        }
        return m;
    }

    // WHAT: a ship of the running seed pass: system and chunk.
    struct RegSrc { int sys = 0; int64_t c[3] = {0, 0, 0}; };

    // WHAT: seed ring of a save (system sys, chunk ch): Chebyshev chunk distance to the nearest ship of the same system (capped at
    //       REG_RING_CAP). REG_RING_FAR when no ship is in the system.
    inline int reg_ring_of(int sys, const int64_t ch[3], const std::vector<RegSrc>& src)
    {
        int64_t best = -1;
        for (const RegSrc& s : src)
        {
            if (s.sys != sys) continue;
            const int64_t d = reg_cheb(ch, s.c);
            if (best < 0 || d < best) best = d;
        }
        if (best < 0) return REG_RING_FAR;
        return static_cast<int>(std::min<int64_t>(best, REG_RING_CAP));
    }

    // WHAT: order of the items by ring (counting sort; array order inside a ring). order = item indices, nearest ring first.
    //       near_n = number of items with ring <= near_r (they are the first near_n of order).
    inline void reg_order_rings(const std::vector<int>& ring, int near_r, std::vector<size_t>& order, size_t& near_n)
    {
        std::vector<size_t> cnt(static_cast<size_t>(REG_RING_FAR) + 1, 0);
        for (int r : ring) ++cnt[static_cast<size_t>(r)];
        std::vector<size_t> start(cnt.size(), 0);
        for (size_t k = 1; k < cnt.size(); ++k) start[k] = start[k - 1] + cnt[k - 1];
        order.assign(ring.size(), 0);
        for (size_t i = 0; i < ring.size(); ++i) order[start[static_cast<size_t>(ring[i])]++] = i;
        near_n = 0;
        for (int r = 0; r <= near_r && static_cast<size_t>(r) < cnt.size(); ++r) near_n += cnt[static_cast<size_t>(r)];
    }

    // WHAT: true when the registry work of this tick is over budget (elapsed_ms > budget_ms). Stages stop and resume next tick.
    inline bool reg_over_budget(double elapsed_ms, double budget_ms = REG_BUDGET_MS) { return elapsed_ms > budget_ms; }

    // WHAT: reg_check defaults: rows when n is not given, and the cap.
    constexpr size_t REG_CHECK_DEF_N = 30;
    constexpr size_t REG_CHECK_MAX_N = 100;

    // WHAT: "reg_check:<ship>" or "reg_check:<ship>:<n>" -> sid and n (default 30, capped at 100). false when the ship id is not positive or n is not a positive whole number.
    inline bool reg_check_parse(const std::string& cmd, int& sid, size_t& n)
    {
        const std::string pre = "reg_check:";
        if (cmd.rfind(pre, 0) != 0) return false;
        std::vector<std::string> p;
        for (size_t st = pre.size();;)
        {
            const size_t c = cmd.find(':', st);
            p.push_back(cmd.substr(st, c == std::string::npos ? std::string::npos : c - st));
            if (c == std::string::npos) break;
            st = c + 1;
        }
        if (p.empty() || p.size() > 2) return false;
        if (p[0].empty() || p[0].find_first_not_of("0123456789") != std::string::npos) return false;
        sid = std::atoi(p[0].c_str());
        if (sid <= 0) return false;
        n = REG_CHECK_DEF_N;
        if (p.size() == 2)
        {
            if (p[1].empty() || p[1].find_first_not_of("0123456789") != std::string::npos) return false;
            n = static_cast<size_t>(std::strtoull(p[1].c_str(), nullptr, 10));
            if (n == 0) return false;
            n = std::min(n, REG_CHECK_MAX_N);
        }
        return true;
    }

    // WHAT: the summary line of a compare: "SUMMARY n=.. max_err_m=.. mean_err_m=.. missing=..". missing < 0 = not checked (printed n/a).
    inline std::string reg_summary_line(size_t n, double max_e, double mean_e, long long missing)
    {
        char b[192];
        if (missing < 0) std::snprintf(b, sizeof(b), "SUMMARY n=%zu max_err_m=%.3f mean_err_m=%.3f missing=n/a", n, max_e, mean_e);
        else std::snprintf(b, sizeof(b), "SUMMARY n=%zu max_err_m=%.3f mean_err_m=%.3f missing=%lld", n, max_e, mean_e, missing);
        return b;
    }

    // WHAT: a seeded area: the chunks within r chunks (Chebyshev) of chunk c in system sys. ready = every save in it has been read (phase A done).
    struct RegZone { int sys = 0; int64_t c[3] = {0, 0, 0}; int64_t r = 0; uint64_t pass = 0; bool ready = false; };

    // WHAT: the rock table with its indexes. One instance for the game (mb_registry.hpp); the tests make their own.
    class RockReg
    {
    public:
        uint64_t version = 0;      // bumped on every add, remove (kill), revive and actor change
        uint64_t event_seq = 0;    // sequence of the last destroy event (0 = none yet)
        std::vector<RegRock> rocks;
        std::vector<RegOre> ore;
        std::vector<std::string> kinds;   // ore names of the cache (RegRock::ore indexes this)
        std::vector<RegZone> zones;

        // WHAT: adds a rock (id > 0), or updates the centre and items of a known id. Returns its stable index.
        // WHY: the seed reads save objects once; a second add of the same id only refreshes it and never duplicates the grid entry.
        size_t add(int id, int sys, const double c[3], const std::vector<void*>& items)
        {
            auto it = by_id_.find(id);
            if (it != by_id_.end())
            {
                RegRock& r = rocks[it->second];
                if (!r.alive) { r.alive = true; ++version; }
                std::copy(c, c + 3, r.c);
                r.items = items;
                return it->second;
            }
            const size_t i = rocks.size();
            RegRock r;
            r.id = id; r.sys = sys; r.items = items;
            std::copy(c, c + 3, r.c);
            rocks.push_back(r);
            by_id_[id] = i;
            grid_[cell_key(sys, c)].push_back(i);
            sys_list_[sys].push_back(i);
            ++version;
            return i;
        }

        // WHAT: index of a rock by id, or npos. A dead rock is found too (check alive).
        static constexpr size_t npos = static_cast<size_t>(-1);
        size_t index_of(int id) const { auto it = by_id_.find(id); return it == by_id_.end() ? npos : it->second; }
        // WHAT: true when rock id is in the table and alive.
        bool is_alive(int id) const { const size_t i = index_of(id); return i != npos && rocks[i].alive; }
        // WHAT: the rock by id, or nullptr when unknown. Check alive yourself.
        const RegRock* get(int id) const { const size_t i = index_of(id); return i == npos ? nullptr : &rocks[i]; }

        // WHAT: marks a rock destroyed at tick: alive = false, version bumped, one event pushed (seq = ++event_seq). false when unknown or already dead.
        // WHY: the single place a rock dies, so every ship sees it through is_alive and events_since.
        bool kill(int id, uint64_t tick)
        {
            const size_t i = index_of(id);
            if (i == npos || !rocks[i].alive) return false;
            RegRock& r = rocks[i];
            r.alive = false;
            r.destroyed_at = tick;
            ++version;
            ++event_seq;
            ring_[static_cast<size_t>((event_seq - 1) % REG_EVENT_CAP)] = RegEvent{id, tick, event_seq};
            return true;
        }

        // WHAT: sets the loaded actor handle of a rock (nullptr = not loaded). Bumps version when it changes.
        void set_act(size_t i, void* act)
        {
            if (i >= rocks.size() || rocks[i].act == act) return;
            if (rocks[i].act) act_idx_.erase(rocks[i].act);
            rocks[i].act = act;
            if (act) act_idx_[act] = i;
            ++version;
        }
        // WHAT: index of the rock whose loaded actor is act, or npos.
        size_t index_of_act(const void* act) const { auto it = act_idx_.find(act); return it == act_idx_.end() ? npos : it->second; }

        // WHAT: sets the radius of a rock (m), marks it known (0 is a valid cached value).
        void set_radius(size_t i, double rad_m) { if (i < rocks.size()) { rocks[i].rad_m = rad_m; rocks[i].rad_known = true; } }

        // WHAT: destroy events with seq > seq_seen, oldest first, into out. false = overrun: the ring no longer holds every event after seq_seen
        //       (the caller must re-check its targets). out holds what is left.
        bool events_since(uint64_t seq_seen, std::vector<RegEvent>& out) const
        {
            out.clear();
            if (seq_seen >= event_seq) return true;
            const uint64_t oldest = event_seq > static_cast<uint64_t>(REG_EVENT_CAP) ? event_seq - REG_EVENT_CAP + 1 : 1;
            const bool full = seq_seen + 1 >= oldest;
            for (uint64_t s = std::max(seq_seen + 1, oldest); s <= event_seq; ++s) out.push_back(ring_[static_cast<size_t>((s - 1) % REG_EVENT_CAP)]);
            return full;
        }

        // ---- seeded areas ----
        // WHAT: starts a pass: drops the zones that are not ready (aborted or unfinished passes), adds one zone of radius r per ship (not ready yet).
        void zones_begin(uint64_t pass, const std::vector<RegSrc>& src, int64_t r)
        {
            zones.erase(std::remove_if(zones.begin(), zones.end(), [](const RegZone& z) { return !z.ready; }), zones.end());
            for (const RegSrc& s : src)
            {
                RegZone z;
                z.sys = s.sys;
                std::copy(s.c, s.c + 3, z.c);
                z.r = r;
                z.pass = pass;
                zones.push_back(z);
            }
            while (zones.size() > REG_ZONE_KEEP) zones.erase(zones.begin());
        }
        // WHAT: marks the zones of pass ready (phase A of the pass is done: every save within their radius is read).
        void zones_ready(uint64_t pass) { for (RegZone& z : zones) if (z.pass == pass) z.ready = true; }
        // WHAT: true when the area of chunk c in system sys, r chunks around it (Chebyshev), lies inside a ready zone.
        //       False = unknown: the saves there may not be in the table yet.
        bool area_ready(int sys, const int64_t c[3], int64_t r) const
        {
            for (const RegZone& z : zones) if (z.ready && z.sys == sys && reg_cheb(z.c, c) + r <= z.r) return true;
            return false;
        }

        // ---- targeted rocks (every ship's m.rock / move target) ----
        // WHAT: registers (on = true) or drops a rock id from the targeted set. The game side alive-checks these every tick.
        void target_set(int id, bool on)
        {
            if (on) targeted_.insert(id); else targeted_.erase(id);
        }
        const std::set<int>& targeted() const { return targeted_; }

        // ---- queries (pure reads; no game calls) ----
        // WHAT: visits the rocks of system sys whose cells overlap the AABB lo..hi (metres). f(index) is called once per rock; f checks bounds itself.
        template <class F>
        void for_box(int sys, const double lo[3], const double hi[3], F f) const
        {
            const int64_t x0 = reg_cell_idx(lo[0]), x1 = reg_cell_idx(hi[0]);
            const int64_t y0 = reg_cell_idx(lo[1]), y1 = reg_cell_idx(hi[1]);
            const int64_t z0 = reg_cell_idx(lo[2]), z1 = reg_cell_idx(hi[2]);
            const double ncell = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1) * static_cast<double>(z1 - z0 + 1);
            if (ncell > static_cast<double>(REG_GRID_MAX_CELLS))
            {
                auto it = sys_list_.find(sys);
                if (it != sys_list_.end()) for (size_t i : it->second) f(i);
                return;
            }
            for (int64_t x = x0; x <= x1; ++x)
                for (int64_t y = y0; y <= y1; ++y)
                    for (int64_t z = z0; z <= z1; ++z)
                    {
                        auto it = grid_.find(RegCell{sys, x, y, z});
                        if (it != grid_.end()) for (size_t i : it->second) f(i);
                    }
        }

        // WHAT: rocks alive within r_m of p (centre distance), appended to out (indices).
        size_t within(int sys, const double p[3], double r_m, std::vector<size_t>& out) const
        {
            const size_t n0 = out.size();
            const double lo[3] = {p[0] - r_m, p[1] - r_m, p[2] - r_m}, hi[3] = {p[0] + r_m, p[1] + r_m, p[2] + r_m};
            const double r2 = r_m * r_m;
            for_box(sys, lo, hi, [&](size_t i)
            {
                const RegRock& r = rocks[i];
                if (!r.alive || r.sys != sys) return;
                const double d[3] = {r.c[0] - p[0], r.c[1] - p[1], r.c[2] - p[2]};
                if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= r2) out.push_back(i);
            });
            return out.size() - n0;
        }

        // WHAT: true when rock r passes the filters of the nearest queries: alive, ore item held (item == nullptr = any), not skipped.
        static bool passes(const RegRock& r, const void* item, const void* skip_act, const std::set<int>* skip_ids)
        {
            if (!r.alive) return false;
            if (item && std::find(r.items.begin(), r.items.end(), item) == r.items.end()) return false;
            if (skip_act && r.act == skip_act) return false;
            if (skip_ids && skip_ids->count(r.id)) return false;
            return true;
        }

        // WHAT: nearest rock (centre distance, m) of system sys holding ore item, not skipped, within max_m (max_m <= 0 = no limit).
        //       Returns its index or npos; dist_m = its distance.
        // WHY expanding box: a near rock is found with a small query; the result is final once the best one lies inside the searched radius.
        size_t nearest(int sys, const void* item, const double p[3], const void* skip_act, const std::set<int>* skip_ids, double max_m, double& dist_m) const
        {
            const double cap = max_m > 0.0 ? max_m : REG_SEARCH_MAX_M;
            std::vector<size_t> cand;
            for (double r = std::min(REG_SEARCH_START_M, cap);; r = std::min(r * 4.0, cap))
            {
                cand.clear();
                within(sys, p, r, cand);
                size_t best = npos;
                double bd = 1e300;
                for (size_t i : cand)
                {
                    const RegRock& rk = rocks[i];
                    if (!passes(rk, item, skip_act, skip_ids)) continue;
                    const double d[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                    const double dd = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    if (dd < bd) { bd = dd; best = i; }
                }
                if (best != npos && bd <= r) { dist_m = bd; return best; }
                if (r >= cap) return npos;
            }
        }

        // WHAT: up to n rocks of the filters, nearest first, within max_m (max_m > 0 required). out = (distance m, index).
        size_t nearest_n(int sys, const void* item, const double p[3], const void* skip_act, const std::set<int>* skip_ids, double max_m, size_t n,
                         std::vector<std::pair<double, size_t>>& out) const
        {
            out.clear();
            std::vector<size_t> cand;
            within(sys, p, max_m, cand);
            for (size_t i : cand)
            {
                const RegRock& rk = rocks[i];
                if (!passes(rk, item, skip_act, skip_ids)) continue;
                const double d[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                out.push_back({std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), i});
            }
            const size_t k = std::min(n, out.size());
            std::partial_sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(k), out.end(),
                              [](const std::pair<double, size_t>& a, const std::pair<double, size_t>& b) { return a.first < b.first; });
            out.resize(k);
            return k;
        }

        // WHAT: nearest rock by SURFACE distance (centre distance - radius, m) of system sys, skipping skip_act. surf_m = that distance.
        //       npos when none. The search is final once the best surface distance + REG_RAD_MAX_M lies inside the searched radius.
        size_t nearest_surface(int sys, const double p[3], const void* skip_act, double& surf_m, int skip_id = 0) const
        {
            std::vector<size_t> cand;
            for (double r = REG_SEARCH_START_M;; r = std::min(r * 4.0, REG_SEARCH_MAX_M))
            {
                cand.clear();
                within(sys, p, r, cand);
                size_t best = npos;
                double bs = 1e300;
                for (size_t i : cand)
                {
                    const RegRock& rk = rocks[i];
                    if ((skip_act && rk.act == skip_act) || (skip_id && rk.id == skip_id)) continue;
                    const double d[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                    const double s = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - reg_eff_rad(rk);
                    if (s < bs) { bs = s; best = i; }
                }
                if (best != npos && bs + REG_RAD_MAX_M <= r) { surf_m = bs; return best; }
                if (r >= REG_SEARCH_MAX_M) { if (best != npos) { surf_m = bs; return best; } return npos; }
            }
        }

        // WHAT: first rock that blocks the straight path a->b (m) in system sys. A rock blocks with radius rad + clear_m (+ hazard_extra_m when its id
        //       is in hazard). skip_act and skip_id are left out. Only rocks within PATH_RMAX_M of the path are tested. out_d = blocker distance to path.
        //       npos = clear. Reuses path_first_blocker (mb_logic.hpp).
        size_t first_blocker(int sys, const double a[3], const double b[3], double clear_m, const void* skip_act, int skip_id,
                             const std::set<int>* hazard, double hazard_extra_m, double& out_d) const
        {
            const double lo[3] = {std::min(a[0], b[0]) - PATH_RMAX_M, std::min(a[1], b[1]) - PATH_RMAX_M, std::min(a[2], b[2]) - PATH_RMAX_M};
            const double hi[3] = {std::max(a[0], b[0]) + PATH_RMAX_M, std::max(a[1], b[1]) + PATH_RMAX_M, std::max(a[2], b[2]) + PATH_RMAX_M};
            std::vector<PathRock> pr;
            std::vector<size_t> who;
            for_box(sys, lo, hi, [&](size_t i)
            {
                const RegRock& rk = rocks[i];
                if (!rk.alive || rk.sys != sys || (skip_act && rk.act == skip_act) || rk.id == skip_id) return;
                if (path_box_reject(a, b, rk.c, PATH_RMAX_M) || seg_point_dist(a, b, rk.c) > PATH_RMAX_M) return;
                PathRock p;
                std::copy(rk.c, rk.c + 3, p.c);
                p.r = reg_eff_rad(rk) + clear_m + (hazard && hazard->count(rk.id) ? hazard_extra_m : 0.0);
                pr.push_back(p);
                who.push_back(i);
            });
            const int k = path_first_blocker(a, b, pr.data(), pr.size(), out_d);
            return k < 0 ? npos : who[static_cast<size_t>(k)];
        }

        // WHAT: up to max_n alive rocks of sys with radius not known yet whose cells lie within r_m of the segment a->b (metres). Appended to out (indices, no duplicates).
        // WHY: the approach segment's rocks get their radius first (the path check uses it).
        void radius_wanted_seg(int sys, const double a[3], const double b[3], double r_m, size_t max_n, std::vector<size_t>& out) const
        {
            const double lo[3] = {std::min(a[0], b[0]) - r_m, std::min(a[1], b[1]) - r_m, std::min(a[2], b[2]) - r_m};
            const double hi[3] = {std::max(a[0], b[0]) + r_m, std::max(a[1], b[1]) + r_m, std::max(a[2], b[2]) + r_m};
            for_box(sys, lo, hi, [&](size_t i)
            {
                const RegRock& rk = rocks[i];
                if (out.size() >= max_n || !rk.alive || rk.rad_known || rk.sys != sys) return;
                if (path_box_reject(a, b, rk.c, r_m) || seg_point_dist(a, b, rk.c) > r_m) return;
                if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
            });
        }

        // WHAT: up to max_n alive rocks of sys with radius not known yet, within r_m of any point in refs (metres). Appended to out (indices, no duplicates).
        // WHY: the radius is read only for rocks near a ship or a targeted rock.
        void radius_wanted(int sys, const std::vector<std::array<double, 3>>& refs, double r_m, size_t max_n, std::vector<size_t>& out) const
        {
            std::vector<size_t> cand;
            for (const auto& p : refs)
            {
                cand.clear();
                within(sys, p.data(), r_m, cand);
                for (size_t i : cand)
                {
                    if (out.size() >= max_n) return;
                    if (rocks[i].rad_known || std::find(out.begin(), out.end(), i) != out.end()) continue;
                    out.push_back(i);
                }
            }
        }

        // ---- floating ore table ----
        // WHAT: adds or refreshes a floating ore entry (alive) at p (m).
        void ore_put(const void* key, const double p[3])
        {
            auto it = ore_idx_.find(key);
            if (it == ore_idx_.end())
            {
                RegOre o; o.key = key;
                ore_idx_[key] = ore.size();
                ore.push_back(o);
                it = ore_idx_.find(key);
            }
            RegOre& o = ore[it->second];
            std::copy(p, p + 3, o.p);
            o.alive = true;
        }
        // WHAT: marks an ore entry gone. false when unknown or already gone.
        bool ore_kill(const void* key)
        {
            auto it = ore_idx_.find(key);
            if (it == ore_idx_.end() || !ore[it->second].alive) return false;
            ore[it->second].alive = false;
            return true;
        }
        // WHAT: true when the ore entry exists and is alive.
        bool ore_ok(const void* key) const { auto it = ore_idx_.find(key); return it != ore_idx_.end() && ore[it->second].alive; }
        // WHAT: alive ore entries within r_m of p (m).
        int ore_within(const double p[3], double r_m) const
        {
            int n = 0;
            const double r2 = r_m * r_m;
            for (const RegOre& o : ore)
            {
                if (!o.alive) continue;
                const double d[3] = {o.p[0] - p[0], o.p[1] - p[1], o.p[2] - p[2]};
                if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= r2) ++n;
            }
            return n;
        }
        // WHAT: drops dead ore entries (keeps the table small). Call after a full ore pass.
        void ore_prune()
        {
            std::vector<RegOre> keep;
            for (const RegOre& o : ore) if (o.alive) keep.push_back(o);
            ore.swap(keep);
            ore_idx_.clear();
            for (size_t i = 0; i < ore.size(); ++i) ore_idx_[ore[i].key] = i;
        }

    private:
        std::unordered_map<int, size_t> by_id_;
        std::unordered_map<const void*, size_t> act_idx_;
        std::unordered_map<RegCell, std::vector<size_t>, RegCellHash> grid_;
        std::unordered_map<int, std::vector<size_t>> sys_list_;
        std::unordered_map<const void*, size_t> ore_idx_;
        std::set<int> targeted_;
        RegEvent ring_[REG_EVENT_CAP];

        static RegCell cell_key(int sys, const double c[3]) { return RegCell{sys, reg_cell_idx(c[0]), reg_cell_idx(c[1]), reg_cell_idx(c[2])}; }
    };

    // WHAT: kills the alive rocks that a pass did not see (seen[i] != pass). Returns the count. Called when a pass finishes.
    // WHY: a rock whose save object is gone (or not alive) is dropped only after a full pass, never on a partial one.
    inline size_t reg_kill_unseen(RockReg& t, const std::vector<uint64_t>& seen, uint64_t pass, uint64_t tick)
    {
        size_t gone = 0;
        for (size_t i = 0; i < t.rocks.size(); ++i)
        {
            if (!t.rocks[i].alive) continue;
            if (i < seen.size() && seen[i] == pass) continue;
            if (t.kill(t.rocks[i].id, tick)) ++gone;
        }
        return gone;
    }

    // WHAT: world tracker. id = identity of the loaded world (0 = unreadable), read each tick. LOADED = a different readable world than before
    //       (menu -> game, save load): drop the object pointers. LOST = unreadable for bad_max ticks in a row (debounce): same drop, world not ok.
    //       Same id, other system (a warp) = NONE.
    struct RegWorldTrack { uint64_t id = 0; int bad = 0; bool ok = false; };
    enum RegWorldEv { RWE_NONE = 0, RWE_LOADED = 1, RWE_LOST = 2 };
    inline RegWorldEv reg_world_step(RegWorldTrack& t, uint64_t id, int bad_max)
    {
        if (id != 0)
        {
            t.bad = 0;
            t.ok = true;
            if (id == t.id) return RWE_NONE;
            t.id = id;
            return RWE_LOADED;
        }
        if (t.bad < bad_max) ++t.bad;
        if (t.bad == bad_max && t.ok) { t.ok = false; t.id = 0; return RWE_LOST; }
        return RWE_NONE;
    }

    // ---- disk cache: format and checks (no file I/O here; the game side reads and writes the bytes) ----
    constexpr uint32_t REG_CACHE_MAGIC = 0x52474352;   // bytes 'R' 'C' 'G' 'R' (little endian)
    constexpr uint32_t REG_CACHE_VER = 1;
    constexpr size_t REG_CACHE_HDR = 48;               // header bytes
    constexpr size_t REG_CACHE_REC = 40;               // record bytes
    constexpr uint64_t REG_CACHE_FNV0 = 1469598103934665603ull;   // FNV-1a offset basis

    // WHAT: header fields. exe_size = size of the game exe the table was seeded with; save_n = live save objects at seed; rock_n = records;
    //       kinds_off = byte offset of the ore name table; sum = FNV-1a of every byte after the header.
    struct RegCacheHdr { uint64_t exe_size = 0, save_n = 0, rock_n = 0, kinds_off = 0, sum = 0; };
    // WHAT: one record: rock id, system, centre (m, absolute), radius (m, -1 = not read), ore name index (-1 = none).
    struct RegCacheRec { int32_t id = 0, sys = 0; double c[3] = {0, 0, 0}; float rad = -1.0f; int32_t ore = -1; };
    struct RegCacheData { RegCacheHdr h; std::vector<RegCacheRec> recs; std::vector<std::string> kinds; };

    // WHAT: FNV-1a over n bytes, continuing hash h.
    inline uint64_t reg_cache_fnv(uint64_t h, const uint8_t* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }
    template <class T> inline void reg_put(uint8_t* p, T v) { std::memcpy(p, &v, sizeof(T)); }
    template <class T> inline T reg_get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof(T)); return v; }

    // WHAT: writes the 48 header bytes to p (magic, version, fields).
    inline void reg_cache_put_hdr(uint8_t* p, const RegCacheHdr& h)
    {
        reg_put<uint32_t>(p, REG_CACHE_MAGIC);
        reg_put<uint32_t>(p + 4, REG_CACHE_VER);
        reg_put<uint64_t>(p + 8, h.exe_size);
        reg_put<uint64_t>(p + 16, h.save_n);
        reg_put<uint64_t>(p + 24, h.rock_n);
        reg_put<uint64_t>(p + 32, h.kinds_off);
        reg_put<uint64_t>(p + 40, h.sum);
    }
    // WHAT: writes one 40-byte record to p.
    inline void reg_cache_put_rec(uint8_t* p, const RegCacheRec& r)
    {
        reg_put<int32_t>(p, r.id);
        reg_put<int32_t>(p + 4, r.sys);
        reg_put<double>(p + 8, r.c[0]);
        reg_put<double>(p + 16, r.c[1]);
        reg_put<double>(p + 24, r.c[2]);
        reg_put<float>(p + 32, r.rad);
        reg_put<int32_t>(p + 36, r.ore);
    }
    // WHAT: appends the ore name table: u32 count, then per name u16 length and the bytes (names cut at 65535 bytes).
    inline void reg_cache_put_kinds(std::vector<uint8_t>& out, const std::vector<std::string>& kinds)
    {
        const size_t at = out.size();
        out.resize(at + 4);
        reg_put<uint32_t>(&out[at], static_cast<uint32_t>(kinds.size()));
        for (const std::string& k : kinds)
        {
            const size_t n = std::min<size_t>(k.size(), 65535);
            const size_t b = out.size();
            out.resize(b + 2 + n);
            reg_put<uint16_t>(&out[b], static_cast<uint16_t>(n));
            if (n) std::memcpy(&out[b + 2], k.data(), n);
        }
    }

    // WHAT: a whole cache image in memory (tests; the game side streams the same bytes). d.h.exe_size and d.h.save_n are taken as given.
    inline std::vector<uint8_t> reg_cache_encode(const RegCacheData& d)
    {
        std::vector<uint8_t> out(REG_CACHE_HDR + d.recs.size() * REG_CACHE_REC);
        for (size_t i = 0; i < d.recs.size(); ++i) reg_cache_put_rec(&out[REG_CACHE_HDR + i * REG_CACHE_REC], d.recs[i]);
        const size_t kofs = out.size();
        reg_cache_put_kinds(out, d.kinds);
        RegCacheHdr h = d.h;
        h.rock_n = d.recs.size();
        h.kinds_off = kofs;
        h.sum = reg_cache_fnv(REG_CACHE_FNV0, out.data() + REG_CACHE_HDR, out.size() - REG_CACHE_HDR);
        reg_cache_put_hdr(out.data(), h);
        return out;
    }

    // WHAT: parses a cache image. false + why when it must be ignored: short, magic, version, exe size (or 0), layout, checksum, name table,
    //       or a record that is not sane (id/sys <= 0, non-finite centre, bad radius, ore index out of range).
    inline bool reg_cache_parse(const uint8_t* p, size_t n, uint64_t exe_size, RegCacheData& out, std::string& why)
    {
        out = RegCacheData();
        if (n < REG_CACHE_HDR) { why = "short file"; return false; }
        if (reg_get<uint32_t>(p) != REG_CACHE_MAGIC) { why = "magic"; return false; }
        if (reg_get<uint32_t>(p + 4) != REG_CACHE_VER) { why = "version"; return false; }
        RegCacheHdr& h = out.h;
        h.exe_size = reg_get<uint64_t>(p + 8);
        h.save_n = reg_get<uint64_t>(p + 16);
        h.rock_n = reg_get<uint64_t>(p + 24);
        h.kinds_off = reg_get<uint64_t>(p + 32);
        h.sum = reg_get<uint64_t>(p + 40);
        if (exe_size == 0 || h.exe_size != exe_size) { why = "exe size changed"; return false; }
        if (h.rock_n > (n - REG_CACHE_HDR) / REG_CACHE_REC) { why = "rock count"; return false; }
        if (h.kinds_off != REG_CACHE_HDR + h.rock_n * REG_CACHE_REC || h.kinds_off > n) { why = "layout"; return false; }
        if (reg_cache_fnv(REG_CACHE_FNV0, p + REG_CACHE_HDR, n - REG_CACHE_HDR) != h.sum) { why = "checksum"; return false; }
        size_t q = static_cast<size_t>(h.kinds_off);
        if (q + 4 > n) { why = "name table"; return false; }
        const uint32_t nk = reg_get<uint32_t>(p + q);
        q += 4;
        for (uint32_t k = 0; k < nk; ++k)
        {
            if (q + 2 > n) { why = "name table"; return false; }
            const size_t len = reg_get<uint16_t>(p + q);
            q += 2;
            if (q + len > n) { why = "name table"; return false; }
            out.kinds.emplace_back(reinterpret_cast<const char*>(p + q), len);
            q += len;
        }
        if (q != n) { why = "trailing bytes"; return false; }
        out.recs.resize(static_cast<size_t>(h.rock_n));
        for (size_t i = 0; i < out.recs.size(); ++i)
        {
            const uint8_t* r = p + REG_CACHE_HDR + i * REG_CACHE_REC;
            RegCacheRec& c = out.recs[i];
            c.id = reg_get<int32_t>(r);
            c.sys = reg_get<int32_t>(r + 4);
            c.c[0] = reg_get<double>(r + 8);
            c.c[1] = reg_get<double>(r + 16);
            c.c[2] = reg_get<double>(r + 24);
            c.rad = reg_get<float>(r + 32);
            c.ore = reg_get<int32_t>(r + 36);
            const bool fin = std::isfinite(c.c[0]) && std::isfinite(c.c[1]) && std::isfinite(c.c[2]);
            const bool rad_ok = c.rad == -1.0f || (std::isfinite(c.rad) && c.rad >= 0.0f);
            if (c.id <= 0 || c.sys <= 0 || !fin || !rad_ok || c.ore < -1 || c.ore >= static_cast<int32_t>(nk)) { why = "record"; return false; }
        }
        return true;
    }

    // WHAT: verdict of a loaded cache after its sample check (RCV_TRUST: keep all, skip the seed; RCV_MERGE: keep the matching rocks, read the
    //       new ones; RCV_RESEED: read every cached rock again). save_cache = save count of the cache, save_live = live saves now,
    //       sample_n = rocks compared, sample_bad = rocks that differ (position > REG_CACHE_TOL_M or ore name) or could not be read.
    enum RegCacheVerdict { RCV_TRUST = 0, RCV_MERGE = 1, RCV_RESEED = 2 };
    inline RegCacheVerdict reg_cache_verdict(uint64_t save_cache, uint64_t save_live, size_t sample_n, size_t sample_bad)
    {
        if (sample_n == 0 || sample_bad > 0) return RCV_RESEED;
        return save_cache == save_live ? RCV_TRUST : RCV_MERGE;
    }
    // WHAT: true when two centres (m) are within REG_CACHE_TOL_M on every axis.
    inline bool reg_same_pos(const double a[3], const double b[3])
    {
        return std::fabs(a[0] - b[0]) <= REG_CACHE_TOL_M && std::fabs(a[1] - b[1]) <= REG_CACHE_TOL_M && std::fabs(a[2] - b[2]) <= REG_CACHE_TOL_M;
    }
}
