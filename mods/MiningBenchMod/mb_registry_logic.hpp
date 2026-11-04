// FILE: mods/MiningBenchMod/mb_registry_logic.hpp
// WHAT: pure rock registry. Table of rocks (seeded from save data, centre in metres, radius in metres, ore items, alive flag, optional actor
//       handle kept as an opaque pointer), a chunk-cell grid index per system, local queries (within, nearest, nearest_n, nearest_surface,
//       first_blocker, near_path), destroy event ring with a sequence number, targeted-rock set, floating ore table, per-tick time budget test.
// WHY separate: no UE / Windows calls, so tests/test_logic.cpp and the benchmark run it alone. The game side (mb_registry.hpp) fills and
//       updates it; ships only read it.
// DEPENDS ON (exact): mb_logic.hpp (PathRock, path_first_blocker, seg_point_dist, path_box_reject, PATH_RMAX_M), std headers.
// UNITS: all positions and radii in METRES, absolute frame = chunk * REG_CHUNK_M + loc_raw / raw_per_m (REG_CHUNK_M = 2048 m = 134217728 raw
//        per chunk; raw_per_m = g_raw_per_m, 65536 by default). Same frame as DOCK_CHUNK_M in mb_mining.hpp.
// GRID: cell = REG_CHUNK_M cube per (system, cell x, y, z). A query visits only the cells its AABB overlaps; a query whose cell count
//       exceeds REG_GRID_MAX_CELLS scans the system list instead (correct, slower).
// POINTERS: act / item / key are opaque. This file never dereferences them.
// USED BY: mb_registry.hpp (game side, fills and reads), tests/test_logic.cpp (checks + benchmark).
#pragma once
#include "mb_logic.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
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
    constexpr size_t REG_SEED_TICK = 200;       // save objects read (seed) per tick
    constexpr size_t REG_RAD_TICK = 10;         // GetRadius reads per tick
    constexpr size_t REG_ALIVE_TICK = 64;       // round-robin alive checks per tick
    constexpr size_t REG_K2_TICK = 50;          // K2_GetActorLocation / ore reads per tick
    constexpr size_t REG_VALID_TICK = 10;       // save validations (2 km of a ship) per tick
    constexpr double REG_NEAR_M = 2000.0;       // a ship this close validates the save object of a rock (game side)
    constexpr double REG_RAD_NEAR_M = 5000.0;   // radius reads only for rocks this close to a ship or to a targeted rock

    // WHAT: one rock of the table. id = save object id (ObjectId). c = centre (m, absolute). rad_m valid when rad_known (0 = read and zero).
    // WHY items: the ore items of the save object (ResourceItems), opaque keys. act: loaded actor handle or nullptr (game side only).
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
    };

    // WHAT: one destroy event: rock id, tick it was seen, sequence number (1, 2, ...).
    struct RegEvent { int id = 0; uint64_t tick = 0; uint64_t seq = 0; };

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

    // WHAT: the rock table with its indexes. One instance for the game (mb_registry.hpp); the tests make their own.
    class RockReg
    {
    public:
        uint64_t version = 0;      // bumped on every add, remove (kill), revive and actor change
        uint64_t event_seq = 0;    // sequence of the last destroy event (0 = none yet)
        std::vector<RegRock> rocks;
        std::vector<RegOre> ore;

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
        size_t nearest_surface(int sys, const double p[3], const void* skip_act, double& surf_m) const
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
                    if (skip_act && rk.act == skip_act) continue;
                    const double d[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                    const double s = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - std::max(0.0, rk.rad_m);
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
                p.r = std::max(0.0, rk.rad_m) + clear_m + (hazard && hazard->count(rk.id) ? hazard_extra_m : 0.0);
                pr.push_back(p);
                who.push_back(i);
            });
            const int k = path_first_blocker(a, b, pr.data(), pr.size(), out_d);
            return k < 0 ? npos : who[static_cast<size_t>(k)];
        }

        // WHAT: true when a rock (not skip_act) lies within zone_m of the path a->b by surface distance (slow-down zone of the approach).
        bool near_path(int sys, const double a[3], const double b[3], const void* skip_act, double zone_m) const
        {
            const double lo[3] = {std::min(a[0], b[0]) - PATH_RMAX_M, std::min(a[1], b[1]) - PATH_RMAX_M, std::min(a[2], b[2]) - PATH_RMAX_M};
            const double hi[3] = {std::max(a[0], b[0]) + PATH_RMAX_M, std::max(a[1], b[1]) + PATH_RMAX_M, std::max(a[2], b[2]) + PATH_RMAX_M};
            bool hit = false;
            for_box(sys, lo, hi, [&](size_t i)
            {
                if (hit) return;
                const RegRock& rk = rocks[i];
                if (!rk.alive || rk.sys != sys || (skip_act && rk.act == skip_act)) return;
                if (path_box_reject(a, b, rk.c, PATH_RMAX_M)) return;
                const double sd = seg_point_dist(a, b, rk.c);
                if (sd <= PATH_RMAX_M && sd - std::max(0.0, rk.rad_m) <= zone_m) hit = true;
            });
            return hit;
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
}
