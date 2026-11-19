// FILE: mods/ShipControl/sc_logic_path.hpp
// WHAT: pure path, warp and hazard geometry for the ship flights: far-trip warp settle (warp_settle_done), hazard move-away circle (hazard_dir),
//       rock blocking radius (block_r_m, ROCK_BLOCK_SCALE), first blocker on a segment (path_first_blocker, seg_sphere_blocks), waypoint
//       candidates and pick (side_point, waypoint_candidate, waypoint_pick, path_waypoint), path2 gates (replan_gate, grace_ignore, strikes_reached).
// WHY separate: these have no UE or Windows calls, so ShipControl (its unit checks) and MiningBenchMod (mb_logic.hpp includes this header) share one copy.
// DEPENDS ON (exact): <algorithm>, <cmath>, <cstddef>, <cstdint>. No UE4SS, no Windows.h, no mining types.
// USED BY: mods/MiningBenchMod/mb_logic.hpp (mirror copy next to it, build.ps1), tests/test_shipcontrol.cpp, tests/test_logic.cpp
// RULE: keep this file free of UE/Windows headers, mining types and STR(); use L"" literals. Move, do not copy: mb_logic.hpp must not redefine these.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
    constexpr double WARP_SETTLE_MIN_S = 2.0, WARP_SETTLE_MAX_S = 30.0, WARP_ARRIVE_M = 1000.0;

    // WHAT: far-trip warp settled (dock job stage 4): at least WARP_SETTLE_MIN_S, then the ship reads within WARP_ARRIVE_M of the dock (dock_dist_m >= 0) or WARP_SETTLE_MAX_S has passed.
    // WHY: the dock order is issued only after the warp has landed (no order runs during the teleport).
    inline bool warp_settle_done(double t, double dock_dist_m)
    {
        if (t < WARP_SETTLE_MIN_S) return false;
        return (dock_dist_m >= 0.0 && dock_dist_m <= WARP_ARRIVE_M) || t >= WARP_SETTLE_MAX_S;
    }

    constexpr double HAZARD_RADIUS_M = 5000.0;   // move-away point: this far from the damage start (m); circling keeps the ship >= 2 km away

    // WHAT: direction k of the move-away circle: base turned k * 90 degrees about Z (XY plane; z kept). k = 0 is base.
    inline void hazard_dir(const double base[3], int k, double out[3])
    {
        const int q = ((k % 4) + 4) % 4;
        double x = base[0], y = base[1];
        for (int i = 0; i < q; ++i) { const double nx = -y; y = x; x = nx; }   // +90 degrees per step
        out[0] = x; out[1] = y; out[2] = base[2];
    }

    constexpr double ROCK_BLOCK_SCALE = 0.8;         // blocking radius = rock radius x this + clearance (registry data unchanged)

    // WHAT: a rock as the path check sees it: centre (m) and blocking radius (m) = rock radius + clearance.
    struct PathRock { double c[3]; double r; };

    // WHAT: segment a->b (m) blocks a sphere (c, r) when the sphere centre projects inside the segment (0..1) and its perpendicular
    //       distance to the segment line is below r. out_d = that perpendicular distance. Tangent (distance == r) does not block.
    // WHY: the rock is only a hazard when it lies between the ship and the stand-off point; a rock behind or past the end does not count.
    inline bool seg_sphere_blocks(const double a[3], const double b[3], const double c[3], double r, double& out_d)
    {
        const double d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const double w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (l2 < 1e-12) { out_d = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); return false; }
        const double t = (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / l2;
        const double p[3] = {a[0] + d[0] * t, a[1] + d[1] * t, a[2] + d[2] * t};
        out_d = std::sqrt((c[0] - p[0]) * (c[0] - p[0]) + (c[1] - p[1]) * (c[1] - p[1]) + (c[2] - p[2]) * (c[2] - p[2]));
        return t >= 0.0 && t <= 1.0 && out_d < r;
    }

    // WHAT: index of the blocking rock nearest to a along a->b (smallest projection), -1 when none blocks. out_d = its perpendicular distance (m).
    // WHY pointer + count: the caller keeps one reused scratch array (no copy of the rock list per check).
    inline int path_first_blocker(const double a[3], const double b[3], const PathRock* rocks, size_t n, double& out_d)
    {
        int best = -1;
        double best_t = 1e300;
        for (size_t i = 0; i < n; ++i)
        {
            double d = 0.0;
            if (!seg_sphere_blocks(a, b, rocks[i].c, rocks[i].r, d)) continue;
            const double t = (rocks[i].c[0] - a[0]) * (b[0] - a[0]) + (rocks[i].c[1] - a[1]) * (b[1] - a[1]) + (rocks[i].c[2] - a[2]) * (b[2] - a[2]);
            if (t < best_t) { best_t = t; best = static_cast<int>(i); out_d = d; }
        }
        return best;
    }

    // WHAT: blocking radius (m) of a rock: its radius x ROCK_BLOCK_SCALE + clear_m (clearance, m).
    inline double block_r_m(double rock_r, double clear_m) { return std::max(0.0, rock_r) * ROCK_BLOCK_SCALE + clear_m; }

    // WHAT: waypoint candidate beside a blocker: point at distance off (m) from its centre c, perpendicular to the ship->target direction dir (unit),
    //       on side (+1 / -1). The perpendicular is dir x Z (+Y when dir is along Z).
    inline void side_point(const double c[3], const double dir[3], double off, int side, double out[3])
    {
        double u[3] = {dir[1], -dir[0], 0.0};
        double n = std::sqrt(u[0] * u[0] + u[1] * u[1]);
        if (n < 1e-9) { u[0] = 0.0; u[1] = 1.0; n = 1.0; }
        for (int i = 0; i < 3; ++i) out[i] = c[i] + static_cast<double>(side) * u[i] / n * off;
    }

    // WHAT: waypoint candidate k (0..3) beside a blocker: k 0 = +side at 1.0 x block radius, 1 = -side at 1.0x, 2 = +side at 1.6x, 3 = -side at 1.6x.
    //       c = blocker centre (m), u = unit ship->target direction, br = blocker block radius (m). out = point (m).
    inline void waypoint_candidate(int k, const double c[3], const double u[3], double br, double out[3])
    {
        const double factor = (k / 2) ? 1.6 : 1.0;
        const int side = (k % 2) ? -1 : 1;
        side_point(c, u, br * factor, side, out);
    }

    // WHAT: index of the first waypoint candidate whose two legs are clear (leg_clear[k] = ship->wp and wp->target both clear), -1 when none.
    inline int waypoint_pick(const bool* leg_clear, int n)
    {
        for (int k = 0; k < n; ++k) if (leg_clear[k]) return k;
        return -1;
    }

    // WHAT: waypoint beside a blocker: the point at distance r from the blocker centre c, perpendicular to the path a->b, on the side of c away from the path.
    //       When c lies on the path line, the side is perpendicular to the path and to +Z (fallback: perpendicular to the path and to +Y).
    // WHY: the ship flies around the blocker, never through its zone; the path is re-checked at the waypoint.
    inline void path_waypoint(const double a[3], const double b[3], const double c[3], double r, double out[3])
    {
        double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        double un = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        if (un < 1e-9) { u[0] = 1.0; u[1] = 0.0; u[2] = 0.0; un = 1.0; }
        for (double& x : u) x /= un;
        const double t = (c[0] - a[0]) * u[0] + (c[1] - a[1]) * u[1] + (c[2] - a[2]) * u[2];
        double n[3] = {c[0] - (a[0] + u[0] * t), c[1] - (a[1] + u[1] * t), c[2] - (a[2] + u[2] * t)};   // blocker minus its projection on the path line
        double nn = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nn < 1e-6)   // blocker on the path line: u x Z, else (u parallel to Z) the Y direction
        {
            n[0] = u[1]; n[1] = -u[0]; n[2] = 0.0;
            nn = std::sqrt(n[0] * n[0] + n[1] * n[1]);
            if (nn < 1e-6) { n[0] = 0.0; n[1] = 1.0; n[2] = 0.0; nn = 1.0; }
        }
        for (int i = 0; i < 3; ++i) out[i] = c[i] + n[i] / nn * r;
    }

    constexpr uint64_t REPLAN_COOLDOWN_MS = 20000;   // one waypoint plan per target per this many ms

    constexpr double START_GRACE_M = 20.0;           // a blocker overlapping the ship start: ship radius + rock radius + this (m)

    constexpr int STRIKE_MAX = 3;                    // same blocker on the same target this many times: the target is skipped

    enum ReplanGate { RG_OK = 0, RG_WP_ACTIVE = 1, RG_COOLDOWN = 2 };

    // WHAT: may path logic plan a waypoint now? wp_active: a waypoint flight of this ship runs (no new plan). Cooldown: the last waypoint plan was
    //       for the same target less than REPLAN_COOLDOWN_MS ago. last_ms 0 = none yet.
    inline ReplanGate replan_gate(bool wp_active, int tid, int last_tid, uint64_t last_ms, uint64_t now_ms)
    {
        if (wp_active) return RG_WP_ACTIVE;
        if (last_tid == tid && last_ms != 0 && now_ms - last_ms < REPLAN_COOLDOWN_MS) return RG_COOLDOWN;
        return RG_OK;
    }

    // WHAT: start grace. A blocker whose centre is within ship radius + rock radius + START_GRACE_M of the ship start is ignored when the
    //       ship moves away from it (away) or when the first 60 m of the segment are inside its block radius (first60_inside).
    inline bool grace_ignore(double start_d, double ship_r, double rock_r, bool away, bool first60_inside)
    {
        return start_d < ship_r + rock_r + START_GRACE_M && (away || first60_inside);
    }

    // WHAT: the same blocker blocked the same target this many times: skip the target.
    inline bool strikes_reached(int n) { return n >= STRIKE_MAX; }
}
