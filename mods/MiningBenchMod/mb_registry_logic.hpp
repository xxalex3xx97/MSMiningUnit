// FILE: mods/MiningBenchMod/mb_registry_logic.hpp
// WHAT: pure helpers around the MSCore rock registry (API 16): chunk size, save position -> metres, chunk of a metre position, the RegRock row
//       the consumers read, its blocking radius, and the mining-world gate. The registry itself (table, seed, cache) lives in MSCore.
// WHY separate: no UE / Windows calls, so tests/test_logic.cpp runs it alone.
// DEPENDS ON (exact): mb_logic.hpp (REG_RAD_UNKNOWN_M), std headers.
// UNITS: all positions and radii in METRES, absolute frame = chunk * REG_CHUNK_M + loc_raw / raw_per_m (REG_CHUNK_M = 2048 m, raw_per_m = 65536
//        by default). Same frame as DOCK_CHUNK_M in mb_mining.hpp and as MSCore's registry.
// USED BY: mb_registry.hpp (client), mb_mining.hpp, mb_bubble.hpp, mb_steer.hpp, mb_testcmd.hpp, tests/test_logic.cpp.
#pragma once
#include "mb_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    // WHAT: save chunk edge (m). 134217728 raw per chunk at 65536 raw per m.
    constexpr double REG_CHUNK_M = 2048.0;

    // WHAT: one rock row as the consumers read it (copied from MsRegRock by mb_registry.hpp). id = save object id, c = centre (m), rad_m valid when rad_known.
    struct RegRock
    {
        int id = 0;
        int sys = 0;
        bool alive = true;
        bool rad_known = false;
        double c[3] = {0, 0, 0};
        double rad_m = 0.0;
    };

    // WHAT: blocking/surface radius (m) of a registry rock: its read radius, or REG_RAD_UNKNOWN_M until the radius is read.
    // WHY: an unread rock counts as big as the conservative default, so the path check never misses it.
    inline double reg_eff_rad(const RegRock& r) { return r.rad_known ? std::max(0.0, r.rad_m) : REG_RAD_UNKNOWN_M; }

    // WHAT: absolute metres of a save object position: chunk (3 ints) * REG_CHUNK_M + loc_raw / raw_per_m.
    inline void reg_abs_m(const int64_t ch[3], const double loc_raw[3], double raw_per_m, double out[3])
    {
        for (int i = 0; i < 3; ++i) out[i] = static_cast<double>(ch[i]) * REG_CHUNK_M + loc_raw[i] / raw_per_m;
    }

    // WHAT: chunk of a metre position (floor(m / REG_CHUNK_M) per axis).
    inline void reg_chunk_of_m(const double p[3], int64_t c[3])
    {
        for (int i = 0; i < 3; ++i) c[i] = static_cast<int64_t>(std::floor(p[i] / REG_CHUNK_M));
    }

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

    // WHAT: true when the mining tick may run: a game world is loaded (world_ok) and no world change is pending (world_wait).
    // WHY: mining steps read ship and rock objects; while a save loads or the world is lost they are freed (fault freeze in mine_sample).
    inline bool mining_world_ready(bool world_ok, bool world_wait)
    {
        return world_ok && !world_wait;
    }
}
