// FILE: mods/ShipControl/sc_logic_bubble.hpp
// WHAT: pure bubble ladder of the ShipControl move jobs: look-ahead, brake rule, ladder step, activation gate, flight phase, fix, resume step,
//       hit decision, pass, plan, nudge point and the fly point (chunk + raw loc of a flight target). No game calls.
// WHY: moved from mb_logic.hpp (the mining copies stay until SC3-C) so the ladder is checked without the game (tests/test_shipcontrol.cpp).
// DEPENDS ON (exact): <algorithm>, <cmath>, <cstdint>, sc_logic_path.hpp. Pure std: no windows.h, no mining types, no STR().
// USED BY: tests/test_shipcontrol.cpp (SC3-A2: sc_bubble_run.hpp)
// GAME FACTS: none. Units: m, m/s, s. Raw = 65536 per m, chunk = 134217728 raw (2048 m) (bub_offset_pos).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sc_logic_path.hpp"   // block_r_m, path_waypoint (shared with MiningBenchMod)

namespace
{
    constexpr double BUBBLE_LOOK_MIN_M = 100.0;      // look-ahead never shorter than this (m)
    constexpr double BUBBLE_LOOK_S = 4.0;            // look-ahead = speed x this (s), along the nose
    constexpr double BUBBLE_BRAKE_V = 3.0;           // braking ends below this speed (m/s)
    constexpr int BUBBLE_MAX_REPLAN = 2;             // replans per target before the target is given up
    constexpr double BUBBLE_SHIP_R_M = 12.0;         // ship CollisionRadius when the game read fails (m)
    constexpr double BUBBLE_SLOW_MS = 2.0;           // one ship's bubble pass over this many ms is slow ('BUBBLE slow')
    constexpr int BUBBLE_MAX_WP = 2;                 // waypoints around a blocker per flight (waypoint and ore phases)
    constexpr double BUBBLE_NUDGE_M = 100.0;         // mining drift nudge: move this far away from the rock (m)
    constexpr double BUBBLE_ARRIVE_M = 30.0;         // a resumed flight is done when the ship is this close to its destination (m)

    // WHAT: look-ahead length (m) for speed_mps (m/s): speed x BUBBLE_LOOK_S, at least BUBBLE_LOOK_MIN_M.
    inline double bubble_look_ahead(double speed_mps) { return (std::max)(BUBBLE_LOOK_MIN_M, (std::max)(0.0, speed_mps) * BUBBLE_LOOK_S); }
    // WHAT: clearance (m) for a ship of collision radius ship_r (m) and bubble clearance clr (m, config bubble_clearance_m). The rock block radius is added by block_r_m.
    inline double bubble_clear_m(double ship_r, double clr) { return ship_r + clr; }

    // WHAT: bubble ladder action. NONE = nothing; BRAKE = hit seen, throttle cut (the log line names the next action); REPLAN = after braking,
    //       path_guard against the target with the blocker skipped; RETARGET = after BUBBLE_MAX_REPLAN replans, the target is given up.
    enum BubbleAct { BBA_NONE = 0, BBA_BRAKE = 1, BBA_REPLAN = 2, BBA_RETARGET = 3 };
    // WHAT: ladder state of one ship. braking = throttle held at 0 until speed < BUBBLE_BRAKE_V. pending = action after the stop.
    //       replans = replans done for rock. rock = the target the count belongs to (a new target starts at 0).
    //       blk / blk_d = last blocker id and its distance to the nose line (m), kept for the replan and the log.
    //       phase = flight phase of the last hit. wp = waypoints used in this flight. resume = a waypoint is flying; dst = the flight destination (m)
    //       to fly again when the waypoint ends (WAYPOINT and ORE phases). why = last activation reason (status line only).
    //       blk_c / blk_rr = centre (m) and radius (m) of the last blocker (waypoint geometry).
    struct BubbleState { bool braking = false; int replans = 0; int pending = BBA_NONE; int rock = 0; int blk = 0; double blk_d = 0.0;
                         int prev_id = 0; double prev_d = 0.0; int phase = 0; int wp = 0; bool resume = false; double dst[3] = {0, 0, 0}; int why = 4;   // phase: BubblePhase, why: BubbleWhy (4 = BW_IDLE)
                         double blk_c[3] = {0, 0, 0}; double blk_rr = 0.0; };
    // WHAT: one ladder step. hit = a blocker lies in the bubble, speed_mps = current speed, rock = current target id.
    //       Clear -> hit: BRAKE, pending = REPLAN, or RETARGET after BUBBLE_MAX_REPLAN replans. Braking -> speed < BUBBLE_BRAKE_V: returns pending
    //       (REPLAN or RETARGET) once and counts it. Otherwise NONE.
    // WHY: brake first, then replan, then give up: the ship never flies on at speed toward a rock.
    inline BubbleAct bubble_step(BubbleState& s, bool hit, double speed_mps, int rock)
    {
        if (s.rock != rock) { s.rock = rock; s.replans = 0; s.braking = false; s.pending = BBA_NONE; }
        if (!s.braking)
        {
            if (!hit) return BBA_NONE;
            s.braking = true;
            s.pending = s.replans < BUBBLE_MAX_REPLAN ? BBA_REPLAN : BBA_RETARGET;
            return BBA_BRAKE;
        }
        if (speed_mps >= BUBBLE_BRAKE_V) return BBA_NONE;
        const BubbleAct act = static_cast<BubbleAct>(s.pending);
        s.braking = false;
        s.pending = BBA_NONE;
        if (act == BBA_REPLAN) ++s.replans; else s.replans = 0;
        return act;
    }

    // WHAT: bubble brake only for a blocker ahead (cos_ahead > 0) and closing. Closing: range to it fell since the last pass (same blocker),
    //       or first sight while moving (speed > 0). Beside, behind or opening: no brake.
    inline bool bubble_brake_ok(double cos_ahead, double dist_now, double dist_prev, bool same_prev, double speed_mps)
    {
        if (cos_ahead <= 0.0) return false;
        if (!same_prev) return speed_mps > 0.0;
        return dist_now < dist_prev - 0.01;
    }

    // WHAT: why the bubble does or does not act on a ship. ACTIVE = our steering (move job with thrust) or speed > BUBBLE_BRAKE_V with no game autopilot.
    enum BubbleWhy { BW_ACTIVE = 0, BW_AUTOPILOT = 1, BW_DOCK_AUTOPILOT = 2, BW_TRAVEL = 3, BW_IDLE = 4 };
    // WHAT: activation rule. The game autopilot (bAutopilot, bDockingAutopilot) and warp travel always win: the game has its own avoidance there.
    //       thrust_job = an approach move job (our steering). Otherwise active only when moving faster than BUBBLE_BRAKE_V.
    inline BubbleWhy bubble_why(bool autopilot, bool dock_autopilot, bool travelling, bool thrust_job, double speed_mps)
    {
        if (autopilot) return BW_AUTOPILOT;
        if (dock_autopilot) return BW_DOCK_AUTOPILOT;
        if (travelling) return BW_TRAVEL;
        if (thrust_job || speed_mps > BUBBLE_BRAKE_V) return BW_ACTIVE;
        return BW_IDLE;
    }
    inline const char* bubble_why_name(BubbleWhy w)
    {
        switch (w)
        {
        case BW_ACTIVE: return "active (our steering or moving)";
        case BW_AUTOPILOT: return "inactive: game autopilot engaged (bAutopilot=1)";
        case BW_DOCK_AUTOPILOT: return "inactive: game docking autopilot engaged (bDockingAutopilot=1)";
        case BW_TRAVEL: return "inactive: travelling (warp)";
        default: return "inactive: not moving (speed <= 3 m/s, no move job)";
        }
    }

    // WHAT: flight phase the bubble protects. SEEK = rock approach (move job, target rock); WAYPOINT = point flight (waypoint, undock or return leg,
    //       nudge); ORE = flight to floating ore; MINE = no job, moving (drift while mining or idle).
    enum BubblePhase { BPH_NONE = 0, BPH_SEEK = 1, BPH_WAYPOINT = 2, BPH_ORE = 3, BPH_MINE = 4 };
    // WHAT: phase of a ship from its move job flags. drift = no move job and moving.
    inline BubblePhase bubble_phase(bool approach_job, bool pt_flight, bool ore_flight, bool drift)
    {
        if (approach_job && ore_flight) return BPH_ORE;
        if (approach_job && pt_flight) return BPH_WAYPOINT;
        if (approach_job) return BPH_SEEK;
        if (drift) return BPH_MINE;
        return BPH_NONE;
    }
    inline const char* bubble_phase_name(BubblePhase p)
    {
        switch (p)
        {
        case BPH_SEEK: return "seek";
        case BPH_WAYPOINT: return "waypoint";
        case BPH_ORE: return "ore";
        case BPH_MINE: return "mine";
        default: return "none";
        }
    }

    // WHAT: the fix for the stop of one ladder event. SEEK: replan (path_guard) or skip (give up the target).
    //       WAYPOINT / ORE: waypoint while wp_used < BUBBLE_MAX_WP, else skip. MINE: nudge when near a rock, else resume (brake only).
    enum BubbleFix { BF_NONE = 0, BF_REPLAN = 1, BF_WAYPOINT = 2, BF_SKIP = 3, BF_NUDGE = 4, BF_RESUME = 5 };
    inline BubbleFix bubble_fix(BubblePhase ph, int act, int wp_used, bool near_rock)
    {
        if (ph == BPH_MINE) return near_rock ? BF_NUDGE : BF_RESUME;
        if (act == BBA_RETARGET) return BF_SKIP;
        if (ph == BPH_SEEK) return BF_REPLAN;
        if (ph == BPH_WAYPOINT || ph == BPH_ORE) return wp_used < BUBBLE_MAX_WP ? BF_WAYPOINT : BF_SKIP;
        return BF_NONE;
    }
    inline const char* bubble_fix_name(BubbleFix f)
    {
        switch (f)
        {
        case BF_REPLAN: return "replan";
        case BF_WAYPOINT: return "waypoint";
        case BF_SKIP: return "skip";
        case BF_NUDGE: return "nudge";
        case BF_RESUME: return "resume";
        default: return "none";
        }
    }

    // WHAT: activation gate of one pass (legacy bubble tick order). BGT_SKIP = nothing this pass; BGT_ERASE = drop the ship's state; BGT_RUN = run the ladder.
    enum BubbleGate { BGT_SKIP = 0, BGT_ERASE = 1, BGT_RUN = 2 };
    // WHAT: warp wait: skip, why unchanged. Game autopilot, docking autopilot, travel: erase. Idle (not braking): skip, wp reset unless resuming.
    //       No job and not braking: wp reset. Paused (slow pass) and not braking: skip. Otherwise run.
    inline BubbleGate bubble_gate(BubbleState& bs, bool warp_wait, bool has_job, BubbleWhy why, bool paused)
    {
        if (warp_wait) return BGT_SKIP;
        bs.why = static_cast<int>(why);
        if (why == BW_AUTOPILOT || why == BW_DOCK_AUTOPILOT || why == BW_TRAVEL) return BGT_ERASE;
        if (why == BW_IDLE && !bs.braking) { if (!bs.resume) bs.wp = 0; return BGT_SKIP; }
        if (!bs.braking && !bs.resume && !has_job) bs.wp = 0;
        if (!bs.braking && paused) return BGT_SKIP;
        return BGT_RUN;
    }

    // WHAT: resume of a waypoint flight: BRS_NONE = no resume pending or a move job runs; BRS_ARRIVED = within BUBBLE_ARRIVE_M of the destination;
    //       BRS_FLY = fly to the destination again.
    enum ResumeStep { BRS_NONE = 0, BRS_ARRIVED = 1, BRS_FLY = 2 };
    inline ResumeStep bubble_resume_step(bool resume, bool job_active, double dist_m)
    {
        if (!resume || job_active) return BRS_NONE;
        return dist_m <= BUBBLE_ARRIVE_M ? BRS_ARRIVED : BRS_FLY;
    }

    // WHAT: one hit decision. raw = blocker id from the query (0 = clear), p = ship (m), fw = nose unit vector, bc = blocker centre (m), v = speed (m/s).
    //       Returns the blocker id that counts (0 when the blocker is not closing or behind). dnow = range to the blocker centre (m, 0 when clear).
    //       Keeps prev_id / prev_d for the next pass.
    inline int bubble_hit_decide(BubbleState& bs, int raw, const double p[3], const double fw[3], const double bc[3], double v, double& dnow)
    {
        int bid = raw;
        dnow = 0.0;
        const bool same_prev = raw != 0 && bs.prev_id == raw;
        if (raw)
        {
            const double d3[3] = {bc[0] - p[0], bc[1] - p[1], bc[2] - p[2]};
            dnow = std::sqrt(d3[0] * d3[0] + d3[1] * d3[1] + d3[2] * d3[2]);
            const double cosA = dnow > 1e-9 ? (d3[0] * fw[0] + d3[1] * fw[1] + d3[2] * fw[2]) / dnow : 0.0;
            if (!bubble_brake_ok(cosA, dnow, bs.prev_d, same_prev, v)) bid = 0;
        }
        bs.prev_id = raw;
        bs.prev_d = dnow;
        return bid;
    }

    // WHAT: one ladder pass after the hit decision. bid = blocker that counts (0 = none), ph = phase of the ship, d = blocker distance to the nose line (m),
    //       c / rock_r = blocker centre (m) and radius (m), rock = current target id, v = speed (m/s). Stores the blocker while not braking the phase.
    //       Returns the ladder action (bubble_step).
    inline BubbleAct bubble_pass(BubbleState& bs, int bid, int ph, double d, const double c[3], double rock_r, int rock, double v)
    {
        if (bid)
        {
            bs.blk = bid;
            bs.blk_d = d;
            for (int i = 0; i < 3; ++i) bs.blk_c[i] = c[i];
            bs.blk_rr = rock_r;
            if (!bs.braking) bs.phase = ph;
        }
        if (bs.rock != rock) bs.wp = 0;   // bubble_step resets the count on a new rock too
        return bubble_step(bs, bid != 0, v, rock);
    }

    // WHAT: what the ladder event does in the engine: BEV_REPLAN = replan (SEEK), BEV_SKIP = drop the flight or give the target up,
    //       BEV_FLY = fly a point (waypoint or nudge), BEV_NONE = brake only. BFR_* = why the point is flown (BEV_FLY only).
    enum BubbleEv { BEV_NONE = 0, BEV_REPLAN = 1, BEV_SKIP = 2, BEV_FLY = 3 };
    enum FlyReason { BFR_NONE = 0, BFR_WAYPOINT = 1, BFR_NUDGE = 2, BFR_RESUME = 3 };
    struct BubblePlan { BubbleFix fix; int ev; int reason; };
    // WHAT: plan of one ladder event: the fix (bubble_fix) mapped to the engine event. A waypoint needs a destination (have_dst) and the blocker
    //       geometry (have_geom), else skip. Nudge = fly away from the rock. Replan = path_guard. Resume / none = brake only.
    inline BubblePlan bubble_plan(int phase, int act, int wp_used, bool near_rock, bool have_dst, bool have_geom)
    {
        BubblePlan r{bubble_fix(static_cast<BubblePhase>(phase), act, wp_used, near_rock), BEV_NONE, BFR_NONE};
        switch (r.fix)
        {
        case BF_REPLAN: r.ev = BEV_REPLAN; break;
        case BF_WAYPOINT:
            if (have_dst && have_geom) { r.ev = BEV_FLY; r.reason = BFR_WAYPOINT; }
            else r.ev = BEV_SKIP;
            break;
        case BF_NUDGE: r.ev = BEV_FLY; r.reason = BFR_NUDGE; break;
        case BF_SKIP: r.ev = BEV_SKIP; break;
        default: break;
        }
        return r;
    }

    // WHAT: nudge point (m): BUBBLE_NUDGE_M away from the rock centre nc, along p - nc. Degenerate p == nc: no division by zero (p stays on the spot).
    inline void bubble_nudge_point(const double p[3], const double nc[3], double wp[3])
    {
        const double a[3] = {p[0] - nc[0], p[1] - nc[1], p[2] - nc[2]};
        const double n = (std::max)(1e-9, std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]));
        for (int i = 0; i < 3; ++i) wp[i] = p[i] + a[i] / n * BUBBLE_NUDGE_M;
    }

    // WHAT: chunk + raw loc moved by off_m metres (65536 raw per m). A chunk is 134217728 raw (2048 m): the carry goes to the chunk, loc stays in 0..134217728.
    inline void bub_offset_pos(const int64_t ch0[3], const double loc0[3], const double off_m[3], int64_t ch[3], double loc[3])
    {
        const double raw_per_m = 65536.0;
        const double chunk_raw = 134217728.0;
        for (int i = 0; i < 3; ++i)
        {
            const double raw = loc0[i] + off_m[i] * raw_per_m;
            const double carry = std::floor(raw / chunk_raw);
            ch[i] = ch0[i] + static_cast<int64_t>(carry);
            loc[i] = raw - carry * chunk_raw;
        }
    }
    // WHAT: chunk + raw loc of the flight point wp (m) for a ship at ps (m) whose chunk + raw loc is ch0 / loc0.
    inline void bubble_fly_point(const double ps[3], const double wp[3], const int64_t ch0[3], const double loc0[3], int64_t ch[3], double loc[3])
    {
        const double off[3] = {wp[0] - ps[0], wp[1] - ps[1], wp[2] - ps[2]};
        bub_offset_pos(ch0, loc0, off, ch, loc);
    }

    // WHAT: ship collision radius (m) from the game read: coll_raw / 65536 when the read is ok and positive, else BUBBLE_SHIP_R_M.
    inline double ship_r_from_flight(bool coll_ok, int64_t coll_raw)
    {
        return (coll_ok && coll_raw > 0) ? static_cast<double>(coll_raw) / 65536.0 : BUBBLE_SHIP_R_M;
    }
}
