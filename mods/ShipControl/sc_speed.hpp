// FILE: mods/ShipControl/sc_speed.hpp
// WHAT: speed monitor for watched ships. Each poll step it samples position and speed limit through MSCore (MSCore_ship_flight),
//       judges v / lim, and zeroes the ship (MSCore_ship_zero) while the ratio is above SC_SPEED_ABORT.
// WHY: user safety rule (no speed override): a ship must stay inside its own speed limit. Ported from MiningBenchMod mb_steer.hpp
//      (spd_sample, spd_update). Constants and the abort decision are kept identical.
// DEPENDS ON (exact): sc_mscore.hpp (MsFlight, FL_*, FF_*, ms_load, ms_flight, ms_zero), sc_math.hpp (dist3), <cstring>, <map>, <string>, <vector>
// USED BY: dllmain.cpp (SC_watch, SC_tick_speed, SC_speed, SC_set_tune), tests/test_shipcontrol.cpp (speed_judge).
// GAME FACTS: position = ship ACTOR location (uu) when FF_ACTOR_FRAME is set. Without an actor: chunk * 2048 m + loc_raw (the object fallback).
//             Limit = GetModifiedMaxSpeed, else GetMaxSpeed (raw per s), times speed_k. Raw per m = 65536 (mining default; not configurable here).
// PER SHIP: each watched ship has its own SpeedMon, keyed by ship id.
#pragma once

#include "sc_mscore.hpp"
#include "sc_math.hpp"
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    // WHAT: ratio v / lim above which the ship is zeroed (and reported once). Same as MiningBenchMod MV_SPEED_ABORT.
    constexpr double SC_SPEED_ABORT = 1.1;
    // WHAT: ratio above which a sample is a position glitch (dropped, no zero). Same as MiningBenchMod MV_SPEED_GLITCH.
    constexpr double SC_SPEED_GLITCH = 10.0;
    // WHAT: unit scales. uu = Unreal units (100 per m); chunk = 2048 m cells; raw = game fixed point (65536 per m).
    constexpr double SC_UU_PER_M = 100.0;
    constexpr double SC_CHUNK_M = 2048.0;
    constexpr double SC_RAW_PER_M = 65536.0;
    // WHAT: speed_k default (mining steer_probe_speed_k default 60). SC_set_tune replaces it.
    constexpr double SC_SPEED_K_DEFAULT = 60.0;

    // WHAT: speed monitor state of one ship. p = last position (raw). v = speed (raw/s). lim = limit (raw/s).
    //       started / have / aborted = first sample taken / v valid / over-limit already reported. sid = ship id of the samples.
    struct SpeedMon
    {
        double p[3] = {0, 0, 0};
        double v = 0.0;
        double lim = 0.0;
        bool started = false, have = false, aborted = false;
        int sid = 0;
    };

    // WHAT: result of speed_judge. NONE = no decision (no valid speed or no limit). GLITCH = sample dropped. OK = within abort ratio.
    //       OVER = above abort ratio, already reported. OVER_NEW = above abort ratio, first time (report it).
    enum SpeedJudge { SJ_NONE = 0, SJ_GLITCH = 1, SJ_OK = 2, SJ_OVER = 3, SJ_OVER_NEW = 4 };

    // WHAT: judges one monitor state (no game calls). ratio = v / lim when a decision was made.
    //       Glitch: have cleared. Ratio below 1: the over-limit report re-arms. OVER and OVER_NEW: the caller zeroes the ship.
    // WHY: the abort decision is pure, so it is checked without the game (tests/test_shipcontrol.cpp).
    int speed_judge(SpeedMon& sp, double& ratio)
    {
        ratio = 0.0;
        if (!sp.have || sp.lim <= 0.0) return SJ_NONE;
        ratio = sp.v / sp.lim;
        if (ratio > SC_SPEED_GLITCH) { sp.have = false; return SJ_GLITCH; }
        if (ratio < 1.0) sp.aborted = false;
        if (ratio <= SC_SPEED_ABORT) return SJ_OK;
        if (sp.aborted) return SJ_OVER;
        sp.aborted = true;
        return SJ_OVER_NEW;
    }

    // WHAT: one poll sample of ship sid into sp (MSCore_ship_flight POSE | LIMITS). false = no sample this step.
    //       Gone or unreadable ship: the monitor is reset as mining does (started / have off, sid 0). No position: sp is left as is.
    // WHY: positions come from the ship ACTOR (uu), not the chunk-relative save object, which jumps at chunk changes (mining v/lim=217 bug).
    bool speed_sample(int sid, double dt, double speed_k, SpeedMon& sp)
    {
        MsFlight f;
        std::memset(&f, 0, sizeof(f));
        f.size = static_cast<int32_t>(sizeof(MsFlight));
        if (ms_flight(sid, FL_POSE | FL_LIMITS, &f) != 1 || !(f.flags & FF_ALIVE))
        {
            sp.started = false; sp.have = false; sp.sid = 0;
            return false;
        }
        if (sp.sid != sid) { sp = SpeedMon(); sp.sid = sid; }
        double p[3] = {0, 0, 0};
        if (f.flags & FF_ACTOR_FRAME)
        {
            for (int i = 0; i < 3; ++i) p[i] = f.act_loc[i] / SC_UU_PER_M * SC_RAW_PER_M;
        }
        else if (f.flags & FF_OBJ_POSE)
        {
            for (int i = 0; i < 3; ++i) p[i] = (static_cast<double>(f.ch[i]) * SC_CHUNK_M + f.loc_raw[i] / SC_RAW_PER_M) * SC_RAW_PER_M;
        }
        else return false;
        if (sp.started && dt > 0.0) { sp.v = dist3(p, sp.p) / dt; sp.have = true; }
        std::memcpy(sp.p, p, sizeof(p));
        sp.started = true;
        // GetModifiedMaxSpeed, else GetMaxSpeed (a failed read is 0 in MsFlight, so this matches mining's ok_m / ok_x order)
        const int64_t src = f.max_mod_raw > 0 ? f.max_mod_raw : f.max_base_raw;
        sp.lim = static_cast<double>(src) * speed_k;
        return true;
    }

    // WHAT: one watched ship that went over the abort ratio on this step (host is told once).
    struct SpeedAbort { int sid; double ratio; };

    // WHAT: speed state of the module: watched ships (sid -> owner), their monitors, speed_k from SC_set_tune.
    struct SpeedState
    {
        std::map<int, std::string> watch;
        std::map<int, SpeedMon> mon;
        double speed_k = SC_SPEED_K_DEFAULT;
    };
    SpeedState g_spd;

    // WHAT: one poll step for every watched ship. Over-limit ships are zeroed on every step; the first step over is added to fresh.
    //       Returns the number of fresh aborts, or -1 when MSCore is not usable (nothing sampled, nothing zeroed).
    // WHY: fail closed: no MSCore means no speed guard, so the caller is told (the mining side then keeps its own monitor).
    int speed_tick(double dt, std::vector<SpeedAbort>& fresh)
    {
        if (g_spd.watch.empty()) return 0;
        std::wstring why;
        if (!ms_load(why)) return -1;
        for (const auto& w : g_spd.watch)
        {
            SpeedMon& sp = g_spd.mon[w.first];
            if (!speed_sample(w.first, dt, g_spd.speed_k, sp)) continue;
            double ratio = 0.0;
            const int j = speed_judge(sp, ratio);
            if (j == SJ_OVER || j == SJ_OVER_NEW) ms_zero(w.first, why);
            if (j == SJ_OVER_NEW) fresh.push_back({w.first, ratio});
        }
        return static_cast<int>(fresh.size());
    }
}
