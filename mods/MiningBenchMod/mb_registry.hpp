// FILE: mods/MiningBenchMod/mb_registry.hpp
// WHAT: thin client of the MSCore rock registry (native API 16, MSCore >= 0.8.15). The table, seed (from the save objects), disk cache
//       (<MSCore mod folder>\rockreg\RockRegCache.bin), radius reads and delete listener all live in MSCore; this file only calls its exports
//       (g_ms.rockreg_*, resolved in mb_mscore.hpp) and converts MsRegRock to RegRock (mb_registry_logic.hpp).
// WHY: one shared registry for the standalone mining mod (owner 1) and Fleet (owner 2): both installed seed ONCE, in MSCore.
// MISSING / OFF: an export missing or API < 16 logs ONCE "rock registry needs MSCore 0.8.15 (API 16)". A -2 (rockreg off or starting) logs ONCE
//       "rock registry not ready" and is retried on every call; any other negative is logged once per code. Every failure makes reg_state() report
//       world_ok = false for 2 s, so mining pauses (fail closed) instead of seeing "no rocks".
// FEED: reg_feed(1, ships, targets) from mb_testcmd.hpp registry_frame every ~0.25 s. MSCore merges the feeds of all mods and ticks once.
// UNITS: metres (see mb_registry_logic.hpp).
// DEV CMDS: reg_dump, reg_query:<ship>:<radius_m>, reg_check:<ship>[:<n>] (mb_devcmd.hpp registry_dev) -> MSCore_rockreg_report, files in the
//       MSCore rockreg folder (RockRegDump.txt, RockRegQuery.txt, RockRegCheck.txt).
// USED BY: mb_mining.hpp, mb_bubble.hpp, mb_steer.hpp, mb_testcmd.hpp, mb_devcmd.hpp.
// INCLUDE ORDER: after mb_rocks.hpp (find_by_id, alive, act_frame), before mb_steer.hpp. Needs mb_mscore.hpp (g_ms, ms_load).

#pragma once

#include "mb_registry_logic.hpp"

#include <set>
#include <utility>
#include <vector>

namespace
{
    int sys_id(UObject* o);   // mb_warp.hpp

    constexpr int REG_OWNER = 1;   // owner id of the standalone mining mod in the MSCore feed protocol (Fleet = 2)

    constexpr ULONGLONG REG_SYS_MS = 2000;           // mining ship system re-read period (mb_testcmd.hpp registry_frame)
    constexpr ULONGLONG REG_RETRY_MS = 10000;        // re-check of the registry exports while unresolved
    constexpr ULONGLONG REG_FAIL_PAUSE_MS = 2000;    // mining pauses this long after a failed call (fail closed)
    constexpr size_t REG_BUF_ROWS = 64;              // first buffer of reg_box / reg_seg
    constexpr size_t REG_BUF_MAX = 4096;             // largest retry buffer

    ULONGLONG g_reg_bad_until = 0;   // a registry call failed: reg_state() reports world_ok = false until then

    // WHAT: logs a line once per key (key = small int).
    void reg_log_once(int key, const std::wstring& text)
    {
        static std::set<int> seen;
        if (seen.insert(key).second) log_line(text);
    }

    // WHAT: a failed call (rc < 0). -2 = MSCore rockreg off or starting (transient, retried on every call, never latched). Other codes (-1 exception ...) logged once per code.
    //       Either way mining pauses for REG_FAIL_PAUSE_MS (reg_state) instead of seeing "no rocks".
    void reg_fail(int rc)
    {
        g_reg_bad_until = GetTickCount64() + REG_FAIL_PAUSE_MS;
        if (rc == -2) reg_log_once(-2, STR("rock registry not ready (MSCore rockreg off or starting)"));
        else reg_log_once(rc, STR("rock registry call failed rc=") + std::to_wstring(rc));
    }

    // WHAT: true when MSCore is loaded, reports API >= 16 and all nine registry exports resolved. Cached once resolved; while unresolved re-checked every REG_RETRY_MS.
    //       Missing export or API < 16 logs "needs MSCore 0.8.15 (API 16)" once.
    bool reg_ok()
    {
        static bool ok = false;
        static ULONGLONG next_try = 0;
        if (ok) return true;
        const ULONGLONG now = GetTickCount64();
        if (next_try && now < next_try) return false;
        next_try = now + REG_RETRY_MS;
        std::wstring err;
        if (!ms_load(err)) return false;   // MSCore missing: ms_load logs it; the mod cannot run without it anyway
        const MsApi& m = g_ms;
        ok = m.rockreg_feed && m.rockreg_state && m.rockreg_get && m.rockreg_box && m.rockreg_seg && m.rockreg_nearest_n &&
             m.rockreg_nearest_surface && m.rockreg_area_ready && m.rockreg_report;
        if (!ok) reg_log_once(1, STR("rock registry needs MSCore 0.8.15 (API 16)"));
        return ok;
    }

    // WHAT: MsRegRock -> RegRock.
    RegRock reg_conv(const MsRegRock& m)
    {
        RegRock r;
        r.id = m.id;
        r.sys = m.sys;
        r.alive = (m.flags & 1) != 0;
        r.rad_known = (m.flags & 2) != 0;
        std::copy(m.c, m.c + 3, r.c);
        r.rad_m = m.rad_m;
        return r;
    }

    // WHAT: rock id -> out. true when MSCore knows it (dead rocks too: check out.alive).
    bool reg_get(int id, RegRock& out)
    {
        if (id <= 0 || !reg_ok()) return false;
        MsRegRock m{};
        const int rc = g_ms.rockreg_get(id, &m);
        if (rc < 0) reg_fail(rc);
        if (rc != 1) return false;
        out = reg_conv(m);
        return true;
    }

    // WHAT: one native call into the reusable buffer (REG_BUF_ROWS rows). When the returned total is larger, one retry with a buffer of the total (max REG_BUF_MAX).
    //       call(MsRegRock* out, int max) returns the total. Returns the rows now in buf (0 on failure).
    template <class F> int reg_rows(std::vector<MsRegRock>& buf, F call)
    {
        if (buf.size() < REG_BUF_ROWS) buf.resize(REG_BUF_ROWS);
        int n = call(buf.data(), static_cast<int>(buf.size()));
        if (n < 0) { reg_fail(n); return 0; }
        if (static_cast<size_t>(n) > buf.size())
        {
            buf.resize(std::min(static_cast<size_t>(n), REG_BUF_MAX));
            n = call(buf.data(), static_cast<int>(buf.size()));
            if (n < 0) { reg_fail(n); return 0; }
        }
        return std::min(n, static_cast<int>(buf.size()));
    }

    // WHAT: alive rocks of system sys inside the box lo..hi (m). out is cleared first.
    void reg_box(int sys, const double lo[3], const double hi[3], std::vector<RegRock>& out)
    {
        static std::vector<MsRegRock> buf;   // scratch (game thread only)
        out.clear();
        if (!reg_ok()) return;
        const int got = reg_rows(buf, [&](MsRegRock* o, int mx) { return g_ms.rockreg_box(sys, lo, hi, o, mx); });
        for (int i = 0; i < got; ++i) out.push_back(reg_conv(buf[static_cast<size_t>(i)]));
    }

    // WHAT: alive rocks of system sys whose centre is within margin (m) of the segment a..b. out is cleared first.
    void reg_seg(int sys, const double a[3], const double b[3], double margin, std::vector<RegRock>& out)
    {
        static std::vector<MsRegRock> buf;
        out.clear();
        if (!reg_ok()) return;
        const int got = reg_rows(buf, [&](MsRegRock* o, int mx) { return g_ms.rockreg_seg(sys, a, b, margin, o, mx); });
        for (int i = 0; i < got; ++i) out.push_back(reg_conv(buf[static_cast<size_t>(i)]));
    }

    // WHAT: the n alive rocks nearest to p (centre distance, m, within max_m), nearest first, holding item (nullptr = any ore), skipping ids in skip.
    //       out = (distance m, rock). Cleared first. One call: the buffers hold n rows and MSCore returns at most n.
    void reg_nearest_n(int sys, UObject* item, const double p[3], const std::set<int>* skip, double max_m, int n, std::vector<std::pair<double, RegRock>>& out)
    {
        static std::vector<MsRegRock> buf;
        static std::vector<double> dist;
        static std::vector<int> ids;
        out.clear();
        if (n <= 0 || !reg_ok()) return;
        ids.clear();
        if (skip) ids.assign(skip->begin(), skip->end());
        if (buf.size() < static_cast<size_t>(n)) { buf.resize(static_cast<size_t>(n)); dist.resize(static_cast<size_t>(n)); }
        const int rc = g_ms.rockreg_nearest_n(sys, static_cast<void*>(item), p, ids.empty() ? nullptr : ids.data(), static_cast<int>(ids.size()),
                                              max_m, n, buf.data(), dist.data());
        if (rc < 0) { reg_fail(rc); return; }
        for (int i = 0; i < std::min(rc, n); ++i) out.emplace_back(dist[static_cast<size_t>(i)], reg_conv(buf[static_cast<size_t>(i)]));
    }

    // WHAT: the rock of smallest surface distance (centre distance - radius, unread radius = REG_RAD_UNKNOWN_M) to p; skip_id > 0 is left out.
    //       true when found: surf = that distance (m), r = the rock.
    bool reg_nearest_surface(int sys, const double p[3], int skip_id, double& surf, RegRock& r)
    {
        if (!reg_ok()) return false;
        MsRegRock m{};
        double s = 0.0;
        const int rc = g_ms.rockreg_nearest_surface(sys, p, skip_id, REG_RAD_UNKNOWN_M, &m, &s);
        if (rc < 0) reg_fail(rc);
        if (rc != 1) return false;
        surf = s;
        r = reg_conv(m);
        return true;
    }

    // WHAT: area check for the bubble and the path check (as before: log only, the callers use the rocks already known): true when the area around p
    //       (m, system sys, one chunk each way) is seeded in MSCore. Logs "REG area not ready" once until an area is seen ready.
    bool reg_area_seeded(int sys, const double p[3])
    {
        static bool logged = false;
        if (reg_ok())
        {
            const int rc = g_ms.rockreg_area_ready(sys, p);
            if (rc < 0) reg_fail(rc);
            if (rc == 1) { logged = false; return true; }
        }
        if (!logged)
        {
            logged = true;
            log_line(STR("REG area not ready sys=") + std::to_wstring(sys) + STR(" (seeded rocks only)"));
        }
        return false;
    }

    // WHAT: registry state. All false / 0 when the registry is unavailable (missing export, any failed call in the last REG_FAIL_PAUSE_MS), so mining pauses.
    struct RegStateV
    {
        bool world_ok = false, world_wait = false, seeding = false, near_done = false;
        long long rocks = 0, alive = 0;
        uint64_t version = 0;
    };
    RegStateV reg_state()
    {
        RegStateV r;
        if (!reg_ok()) return r;
        MsRegState s{};
        const int rc = g_ms.rockreg_state(&s);
        if (rc != 1) { reg_fail(rc < 0 ? rc : -1); return r; }
        r.world_ok = s.world_ok != 0 && GetTickCount64() >= g_reg_bad_until;
        r.world_wait = s.world_wait != 0;
        r.seeding = s.seeding != 0;
        r.near_done = s.near_done != 0;
        r.rocks = s.rocks;
        r.alive = s.alive;
        r.version = s.version;
        return r;
    }

    // WHAT: feeds ships and targeted rock ids to MSCore (owner 1). MSCore merges the feeds of all mods and ticks the registry when due.
    void reg_feed(int owner, const std::vector<MsRegShip>& ships, const std::vector<int>& targets)
    {
        if (!reg_ok()) return;
        const int rc = g_ms.rockreg_feed(owner, ships.empty() ? nullptr : ships.data(), static_cast<int>(ships.size()),
                                         targets.empty() ? nullptr : targets.data(), static_cast<int>(targets.size()));
        if (rc < 0) reg_fail(rc);
    }

    // WHAT: a ship's centre (m), system and ok flag (run under seh_run). ok = false when the ship or its frame is unreadable.
    struct RegShipPos { int sid = 0; int sys = 0; double p[3] = {0, 0, 0}; bool ok = false; };
    void reg_ship_pos_fn(void* v)
    {
        auto* x = static_cast<RegShipPos*>(v);
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), x->sid);
        ActFrame f;
        if (!alive(ship) || !act_frame(ship, f)) return;
        x->sys = sys_id(ship);
        for (int k = 0; k < 3; ++k) x->p[k] = f.loc[k] / MV_UU_PER_M;
        x->ok = x->sys > 0;
    }

    // WHAT: dev commands. reg_dump (kind 0), reg_query:<ship>:<radius_m> (kind 1), reg_check:<ship>[:<n>] (kind 2): MSCore writes the report file
    //       and returns a one-line text. Ship position = live actor location / 100.
    std::wstring registry_dev(int sid, const std::string& cmd)
    {
        if (!reg_ok()) return STR("error: rock registry needs MSCore 0.8.15 (API 16)");
        int kind = 0, ship = sid;
        double arg = 0.0;
        if (cmd.rfind("reg_check:", 0) == 0)
        {
            size_t rn = 0;
            if (!reg_check_parse(cmd, ship, rn)) return STR("error: usage reg_check:<ship>[:<n>] (n 1..100, default 30)");
            kind = 2;
            arg = static_cast<double>(rn);
        }
        else if (cmd != "reg_dump")
        {   // reg_query:<ship>:<radius_m>
            const size_t c1 = cmd.find(':');
            const size_t c2 = c1 == std::string::npos ? std::string::npos : cmd.find(':', c1 + 1);
            if (c2 == std::string::npos) return STR("error: usage reg_query:<ship>:<radius_m>");
            ship = std::atoi(cmd.substr(c1 + 1, c2 - c1 - 1).c_str());
            arg = std::atof(cmd.substr(c2 + 1).c_str());
            if (arg <= 0.0) return STR("error: radius must be > 0");
            kind = 1;
        }
        RegShipPos sp;
        sp.sid = ship;
        if (!seh_run(reg_ship_pos_fn, &sp)) return STR("error: probe fault ") + seh_describe(g_seh);
        if (!sp.ok) return STR("error: ship unreadable");
        wchar_t out[256] = {0};
        const int rc = g_ms.rockreg_report(kind, sp.sys, sp.p, arg, out, 256);
        if (rc < 0) reg_fail(rc);
        return rc == 1 ? std::wstring(out) : STR("error: rock registry report failed (rc ") + std::to_wstring(rc) + STR(")");
    }
}
