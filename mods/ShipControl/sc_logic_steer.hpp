// FILE: mods/ShipControl/sc_logic_steer.hpp
// WHAT: pure steering rules of the ShipControl move job: approach Throttle, phase 1 decision, low-power state, aim view offset, ore refresh, surface distance. No game calls.
// WHY: the rules are checked without the game (tests/test_shipcontrol.cpp). Values and formulas ported 1:1 from MiningBenchMod mb_steer.hpp (move_aim_step, view_aim_drive) and mb_logic.hpp.
//      The mining copies stay until SC2b slice C.
// DEPENDS ON (exact): <algorithm>, <cmath>. Pure std: no windows.h, no sc_math.hpp.
// USED BY: sc_frame.hpp, tests/test_shipcontrol.cpp
// GAME FACTS: none. Units: m, s, deg, Throttle steps (game steps -10..+10).
#pragma once

#include <algorithm>
#include <cmath>

namespace
{
    // WHAT: heading error below this (deg): phase 1 face done. Mining MV_FACE_DONE_DEG.
    constexpr double SM_FACE_DONE_DEG = 2.0;
    // WHAT: face fails after this many s in phase 1. Mining MV_FACE_TIMEOUT_S.
    constexpr double SM_FACE_TIMEOUT_S = 30.0;
    // WHAT: approach enters phase 2 below this heading error (deg). Mining MV_APPROACH_START_DEG.
    constexpr double SM_APPROACH_START_DEG = 10.0;
    // WHAT: heading error above this (deg) forces the approach Throttle target to 0. Mining MV_APPROACH_HEAD_DEG.
    constexpr double SM_APPROACH_HEAD_DEG = 10.0;
    // WHAT: approach fails after this many s in total. Mining MV_APPROACH_TIMEOUT_S.
    constexpr double SM_APPROACH_TIMEOUT_S = 300.0;
    // WHAT: top approach Throttle step (steps). Mining MV_APPROACH_MAX_THR.
    constexpr int SM_APPROACH_MAX_THR = 10;
    // WHAT: applied step rises by 1 at most once per this many s; it falls straight to the target. Mining MV_THR_RAMP_S.
    constexpr double SM_THR_RAMP_S = 0.5;
    // WHAT: overshoot brake step (steps). Mining MV_BRAKE_THR.
    constexpr int SM_BRAKE_THR = -2;
    // WHAT: brake until speed is below this (m/s). Mining MV_BRAKE_DONE_MS.
    constexpr double SM_BRAKE_DONE_MS = 1.0;
    // WHAT: rate sample period (s). Mining MV_SAMPLE_S.
    constexpr double SM_SAMPLE_S = 0.25;
    // WHAT: aim offset magnitude cap (deg). Mining MV_VIEW_MAX_DEG.
    constexpr double SM_VIEW_MAX_DEG = 20.0;
    // WHAT: dead zone added to the heading error for the aim offset (deg). Mining MV_VIEW_DEADZONE_DEG.
    constexpr double SM_VIEW_DEADZONE_DEG = 7.0;
    // WHAT: resend period of the aim offset (s). Mining MV_VIEW_PERIOD_S.
    constexpr double SM_VIEW_PERIOD_S = 0.1;
    // WHAT: raw ViewRotation units per degree. Mining VIEW_RAW_PER_DEG (mb_steer.hpp).
    constexpr double SM_VIEW_RAW_PER_DEG = 65536.0;
    // WHAT: ore actor is re-read within this distance (m). Mining ORE_REFRESH_M (aim_calc_ore).
    constexpr double SM_ORE_REFRESH_M = 100.0;
    // WHAT: uu (Unreal units) per m. Mining MV_UU_PER_M.
    constexpr double SM_UU_PER_M = 100.0;
    // WHAT: raw fixed-point units per m (65536). Mining RAW_PER_METER / g_raw_per_m default.
    constexpr double SM_RAW_PER_M = 65536.0;
    // WHAT: chunk size in m, and the same in raw units. Mining AF_CHUNK_M (2048 m) and the 134217728 raw of pt_rloc.
    constexpr double SM_CHUNK_M = 2048.0;
    constexpr double SM_CHUNK_RAW = 134217728.0;

    // Moved verbatim from mb_logic.hpp (approach Throttle, approach timeout, low-power cap, no-power test).
    constexpr double APPROACH_RAMP_M = 500.0;        // approach: full Throttle beyond this distance to the stand-off point (m), linear ramp inside

    // WHAT: approach Throttle step for the distance to the stand-off point (m). max_thr beyond APPROACH_RAMP_M; inside it linear down to 1
    //       (ceil, so the last metres still move); 0 at or past the stand-off point. speed_mps is not used by the ramp (the caller brakes on it).
    // WHY: full speed on the long leg, a smooth slow-down near the rock. Ramp is distance-only: no measured deceleration exists.
    inline int approach_throttle(double dist_to_standoff_m, double speed_mps, int max_thr)
    {
        (void)speed_mps;
        if (max_thr < 1 || dist_to_standoff_m <= 0.0) return 0;
        if (dist_to_standoff_m >= APPROACH_RAMP_M) return max_thr;
        return std::clamp(static_cast<int>(std::ceil(max_thr * dist_to_standoff_m / APPROACH_RAMP_M)), 1, max_thr);
    }

    // WHAT: approach timeout (s): base, or base / max_frac in low power (max_frac = FlightModel MaxLowPowerSpeed, a fraction 0..1). Other max_frac -> base.
    inline double approach_timeout_s(double base, bool lp, double max_frac)
    {
        if (!lp || max_frac <= 0.0 || max_frac >= 1.0) return base;
        return base / max_frac;
    }

    // WHAT: Throttle step cap: min(cap, lp_thr) in low power when lp_thr > 0 (FlightModel LowPowerThrottle), else cap.
    inline int lowpower_throttle_cap(int cap, bool lp, int lp_thr) { return (lp && lp_thr > 0) ? (std::min)(cap, lp_thr) : cap; }

    // WHAT: no power: the grid consumes (cons > 0), produces nothing (prod <= 0) and efficiency is 0. This is the lost state (grace LOWPOWER_GRACE_S).
    inline bool power_no_power(double cons, double prod, double eff) { return cons > 0.0 && prod <= 0.0 && eff <= 0.0; }

    // WHAT: low-power state of the approach. dead = no power: grid read -> power_no_power, else the low-power flag when known and on.
    //       lp = dead and the low-power limits read (lim_ok). thr / frac are set only when lp, else 0. Returns lp.
    // WHY: same rule as mining power_dead + lp_state (mb_steer.hpp), pure so it can be checked.
    inline bool lp_state_from(bool grid_ok, double prod, double cons, double eff, bool lp_known, bool lp_on, bool lim_ok, int lp_thr, double lp_frac,
                              bool& lp, int& thr, double& frac)
    {
        const bool dead = grid_ok ? power_no_power(cons, prod, eff) : (lp_known && lp_on);
        lp = dead && lim_ok;
        thr = lp ? lp_thr : 0;
        frac = lp ? lp_frac : 0.0;
        return lp;
    }

    // WHAT: phase 1 outcome. NONE = keep aiming. FACE_OK = face done. FACE_TIMEOUT = face failed. TO_PHASE2 = approach starts.
    enum Phase1 { P1_NONE, P1_FACE_OK, P1_FACE_TIMEOUT, P1_TO_PHASE2 };

    // WHAT: phase 1 decision (move_aim_step step 3). Face (approach false): err < SM_FACE_DONE_DEG ok; pt >= SM_FACE_TIMEOUT_S timeout.
    //       Approach: err < SM_APPROACH_START_DEG -> phase 2.
    inline Phase1 phase1_decide(bool approach, double err, double pt)
    {
        if (!approach)
        {
            if (err < SM_FACE_DONE_DEG) return P1_FACE_OK;
            if (pt >= SM_FACE_TIMEOUT_S) return P1_FACE_TIMEOUT;
            return P1_NONE;
        }
        return err < SM_APPROACH_START_DEG ? P1_TO_PHASE2 : P1_NONE;
    }

    // WHAT: result of approach_step. thr / thr_t = applied step and ramp timer. target = step wanted. done = stopped past the stop distance (caller ends the job, Throttle 0).
    struct ApproachOut { int thr; double thr_t; int target; bool done; };

    // WHAT: one approach Throttle step (move_aim_step steps 8-9, mb_steer.hpp). remaining = surface - stop distance (m); v_ms = speed (m/s);
    //       err = heading error (deg); steps = ship Throttle steps; lp / lp_thr = low power and its Throttle cap.
    //       remaining <= 0: brake (SM_BRAKE_THR) while v_ms >= SM_BRAKE_DONE_MS, else done. Otherwise cap = lowpower_throttle_cap(min(SM_APPROACH_MAX_THR, steps)),
    //       target = 0 when err > SM_APPROACH_HEAD_DEG, else approach_throttle(remaining). The step rises by 1 per SM_THR_RAMP_S and falls straight to target.
    inline ApproachOut approach_step(double remaining, double v_ms, double err, int steps, bool lp, int lp_thr, int thr, double thr_t, double dt)
    {
        ApproachOut o{thr, thr_t, 0, false};
        if (remaining <= 0.0)
        {
            if (v_ms < SM_BRAKE_DONE_MS) { o.done = true; o.thr = 0; o.thr_t = 0.0; return o; }
            o.target = SM_BRAKE_THR;
            o.thr = SM_BRAKE_THR;
            o.thr_t = 0.0;
            return o;
        }
        if (o.thr < 0) o.thr = 0;   // pushed back out after a brake: restart forward from 0
        const int cap = lowpower_throttle_cap((std::min)(SM_APPROACH_MAX_THR, steps), lp, lp_thr);
        o.target = err > SM_APPROACH_HEAD_DEG ? 0 : approach_throttle(remaining, v_ms, cap);
        if (o.target > o.thr)
        {
            o.thr_t += dt;
            if (o.thr_t >= SM_THR_RAMP_S) { o.thr_t = 0.0; ++o.thr; }
        }
        else { o.thr = o.target; o.thr_t = 0.0; }
        return o;
    }

    // WHAT: the ore actor is re-read when within SM_ORE_REFRESH_M (m) and not read yet, or the last read is 1 s old.
    inline bool ore_refresh_due(double dist_m, bool fresh, double ore_t) { return dist_m <= SM_ORE_REFRESH_M && (!fresh || ore_t >= 1.0); }

    // WHAT: aim offset of one view send (view_aim_drive steps 2-4, mb_steer.hpp). err = heading error (deg); a = unit rotation axis in body axes.
    //       err < SM_FACE_DONE_DEG: yaw = pit = 0, aligned = true. Else mag = min(err + SM_VIEW_DEADZONE_DEG, SM_VIEW_MAX_DEG),
    //       yaw = mag * (a . Z) / |a|, pit = mag * (a . -Y) / |a| (yaw axis = body +Z, pitch axis = body -Y). |a| <= 1e-9 gives 0.
    //       Always returns true: yaw / pit are written.
    inline bool view_offset(double err, const double a[3], double& yaw_deg, double& pit_deg, bool& aligned)
    {
        aligned = err < SM_FACE_DONE_DEG;
        yaw_deg = 0.0;
        pit_deg = 0.0;
        if (aligned) return true;
        const double n = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (n <= 1e-9) return true;
        const double mag = (std::min)(err + SM_VIEW_DEADZONE_DEG, SM_VIEW_MAX_DEG);
        yaw_deg = mag * a[2] / n;
        pit_deg = mag * (-a[1]) / n;
        return true;
    }

    // WHAT: surface distance (m) to a target: |tgt - ship| - rad_m. Points in m. rad_m = 0 for points and ore.
    inline double remain_dist_m(const double ship_m[3], const double tgt_m[3], double rad_m)
    {
        const double dx = tgt_m[0] - ship_m[0], dy = tgt_m[1] - ship_m[1], dz = tgt_m[2] - ship_m[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz) - rad_m;
    }
}
