// FILE: tests/test_shipcontrol.cpp
// WHAT: unit checks for the ShipControl owner registry (mods/ShipControl/sc_owner.hpp): claim, same-owner claim, refused claim, release, release_all, owner_of. No game needed.
// WHY: the registry decides which behaviour may drive a ship; the checks keep the refuse rules fixed.
// RUN: tests\run.ps1 (builds with cl, runs, exit code 0 = all PASS). Or by hand: see run.ps1.
// DEPENDS ON: mods/ShipControl/sc_owner.hpp (pure std, no UE headers).
// NOTE: CHECK counts failures and keeps going; the summary line prints the pass and fail counts.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sc_logic_path.hpp"
#include "sc_owner.hpp"
#include "sc_speed.hpp"   // SC2a: speed judge (pure part); includes sc_mscore.hpp (windows.h only)
#include "sc_logic_steer.hpp"   // SC2b A1: moved steering rules
#include "sc_frame.hpp"         // SC2b A1: frame, aim, calibration
#include "sc_move.hpp"          // SC2b A2: move jobs (ABI via sc_api.h, sc_view_raw)

static int g_pass = 0;
static int g_fail = 0;
#define NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))
#define NEARB(a, b, eps) (std::fabs((double)(a) - (double)(b)) <= (eps))   // value form, for use inside CHECK(...)

#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void t_warp_settle()
{
    CHECK(!warp_settle_done(1.0, 100.0));          // under the 2 s minimum: wait even when arrived
    CHECK(warp_settle_done(2.0, 300.0));           // arrived at the dock
    CHECK(!warp_settle_done(5.0, 1500.0));         // still far
    CHECK(!warp_settle_done(5.0, -1.0));           // position unknown: wait
    CHECK(warp_settle_done(30.0, -1.0));           // cap: go on after 30 s
    CHECK(warp_settle_done(5.0, 1000.0));          // exactly the arrival distance counts
}

static void t_hazard_dir()
{
    // circle directions: base turned 90 deg per step, z kept
    const double base[3] = {1.0, 0.0, 0.5};
    double d[3];
    hazard_dir(base, 0, d); CHECK(d[0] == 1.0 && d[1] == 0.0 && d[2] == 0.5);
    hazard_dir(base, 1, d); NEAR(d[0], 0.0, 1e-9); NEAR(d[1], 1.0, 1e-9); CHECK(d[2] == 0.5);
    hazard_dir(base, 2, d); NEAR(d[0], -1.0, 1e-9); NEAR(d[1], 0.0, 1e-9);
    hazard_dir(base, 4, d); NEAR(d[0], 1.0, 1e-9); NEAR(d[1], 0.0, 1e-9);
    hazard_dir(base, -1, d); NEAR(d[0], 0.0, 1e-9); NEAR(d[1], -1.0, 1e-9);
}
static void t_path_block_geom()
{
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    double d = -1.0;
    const double front[3] = {500, 100, 0}, behind[3] = {-500, 0, 0}, beyond[3] = {1500, 0, 0}, tangent[3] = {500, 150, 0}, inside[3] = {500, 149.9, 0};
    CHECK(seg_sphere_blocks(a, b, front, 150, d));
    NEAR(d, 100.0, 1e-9);
    CHECK(!seg_sphere_blocks(a, b, behind, 600, d));           // behind the start
    CHECK(!seg_sphere_blocks(a, b, beyond, 600, d));           // past the end (even inside the radius there)
    CHECK(!seg_sphere_blocks(a, b, tangent, 150, d));          // distance == radius: clear
    CHECK(seg_sphere_blocks(a, b, inside, 150, d));            // just inside
    CHECK(!seg_sphere_blocks(a, a, front, 150, d));            // zero-length segment: nothing between
    // the blocker nearest along the path is returned
    std::vector<PathRock> rk(2);
    rk[0].c[0] = 800; rk[0].c[1] = 0; rk[0].c[2] = 0; rk[0].r = 100;
    rk[1].c[0] = 300; rk[1].c[1] = 0; rk[1].c[2] = 0; rk[1].r = 50;
    double dd = -1.0;
    CHECK(path_first_blocker(a, b, rk.data(), rk.size(), dd) == 1);
    CHECK(path_first_blocker(a, b, rk.data(), 1, dd) == 0);
    rk[0].r = 0.0; rk[1].r = 0.0;
    CHECK(path_first_blocker(a, b, rk.data(), rk.size(), dd) == -1);             // radius 0 never blocks
}
static void t_path_waypoint_geom()
{
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    const double c[3] = {500, 100, 0};
    double w[3] = {0, 0, 0};
    path_waypoint(a, b, c, 400.0, w);
    NEAR(w[0], 500.0, 1e-9); NEAR(w[1], 500.0, 1e-9); NEAR(w[2], 0.0, 1e-9);   // beside the blocker, away from the path
    const double v[3] = {w[0] - c[0], w[1] - c[1], w[2] - c[2]};
    NEAR(v[0] * 1000.0 + v[1] * 0.0 + v[2] * 0.0, 0.0, 1e-6);                 // perpendicular to the path
    NEAR(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 400.0, 1e-9);     // exactly radius + clearance from the blocker
    // blocker exactly on the path line: still perpendicular, still at radius
    const double on[3] = {500, 0, 0};
    path_waypoint(a, b, on, 300.0, w);
    NEAR(w[0], 500.0, 1e-9); NEAR(w[1], -300.0, 1e-9); NEAR(w[2], 0.0, 1e-9);
}
static void t_path2_gates()
{
    // replan gate: a waypoint flight blocks new plans; one plan per target per 20 s
    CHECK(replan_gate(true, 5, 0, 0, 1000) == RG_WP_ACTIVE);
    CHECK(replan_gate(false, 5, 5, 10000, 29999) == RG_COOLDOWN);   // same target, 19.9 s
    CHECK(replan_gate(false, 5, 5, 10000, 30000) == RG_OK);         // 20 s: allowed again
    CHECK(replan_gate(false, 6, 5, 10000, 11000) == RG_OK);         // another target: no cooldown
    CHECK(replan_gate(false, 5, 5, 0, 11000) == RG_OK);             // no plan yet (last_ms 0)
    // start grace: overlapping the start and (moving away or first 60 m inside) is ignored
    CHECK(grace_ignore(10.0, 12.0, 8.0, true, false));              // overlap, moving away
    CHECK(grace_ignore(10.0, 12.0, 8.0, false, true));              // overlap, first 60 m inside
    CHECK(!grace_ignore(10.0, 12.0, 8.0, false, false));            // overlap but ahead and clear of the first 60 m
    CHECK(!grace_ignore(45.0, 12.0, 8.0, true, true));              // not overlapping the start (45 > 12+8+20=40)
    CHECK(grace_ignore(39.9, 12.0, 8.0, true, false));              // inside the 40 m start zone
    // strikes: the third block of the same blocker on the same target skips the target
    CHECK(!strikes_reached(1) && !strikes_reached(2) && strikes_reached(3));
}
static void t_tune_geom_path()
{
    CHECK(std::fabs(block_r_m(10.0, 8.0) - 16.0) < 1e-9);          // 10 x 0.8 + 8
    CHECK(std::fabs(block_r_m(35.0, 8.0) - 36.0) < 1e-9);          // 35 x 0.8 + 8
    CHECK(std::fabs(block_r_m(-1.0, 8.0) - 8.0) < 1e-9);           // negative radius counts as 0
    // side points: dir along +X, centre at (100,0,0), offset 50: side +1 is -Y, side -1 is +Y
    const double c[3] = {100, 0, 0}, dx[3] = {1, 0, 0}, dz[3] = {0, 0, 1};
    double o[3];
    side_point(c, dx, 50.0, 1, o);
    CHECK(std::fabs(o[0] - 100) < 1e-9 && std::fabs(o[1] + 50) < 1e-9 && std::fabs(o[2]) < 1e-9);
    side_point(c, dx, 50.0, -1, o);
    CHECK(std::fabs(o[1] - 50) < 1e-9);
    side_point(c, dz, 50.0, 1, o);                                  // dir along Z: +Y fallback
    CHECK(std::fabs(o[1] - 50) < 1e-9 && std::fabs(o[0] - 100) < 1e-9);
    // candidates: 0 = +1.0x, 1 = -1.0x, 2 = +1.6x, 3 = -1.6x (block radius 20)
    double w[3];
    waypoint_candidate(0, c, dx, 20.0, w); CHECK(std::fabs(w[1] + 20) < 1e-9);
    waypoint_candidate(1, c, dx, 20.0, w); CHECK(std::fabs(w[1] - 20) < 1e-9);
    waypoint_candidate(2, c, dx, 20.0, w); CHECK(std::fabs(w[1] + 32) < 1e-9);
    waypoint_candidate(3, c, dx, 20.0, w); CHECK(std::fabs(w[1] - 32) < 1e-9);
    const bool none[4] = {false, false, false, false};
    const bool second[4] = {false, true, false, true};
    CHECK(waypoint_pick(none, 4) == -1);
    CHECK(waypoint_pick(second, 4) == 1);                            // first clear candidate in order
    // leg clearance through path_first_blocker: rock at 20 m off a 1 km leg blocks (block 36), at 50 m does not (block 14.4)
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    PathRock pr[2];
    pr[0].c[0] = 500; pr[0].c[1] = 20; pr[0].c[2] = 0; pr[0].r = block_r_m(35.0, 8.0);
    pr[1].c[0] = 500; pr[1].c[1] = 50; pr[1].c[2] = 0; pr[1].r = block_r_m(8.0, 8.0);
    double dd = 0.0;
    CHECK(path_first_blocker(a, b, pr, 1, dd) == 0);
    CHECK(path_first_blocker(a, b, pr + 1, 1, dd) == -1);
}

// SC2b A1: moved steering rules (sc_logic_steer.hpp) and frame / aim maths (sc_math.hpp, sc_frame.hpp).
// Vectors: approach_throttle, approach_timeout_s, lowpower_throttle_cap, power_no_power = test_logic.cpp.
static void t_moved_steer()
{
    CHECK(approach_throttle(5000.0, 40.0, 10) == 10);
    CHECK(approach_throttle(500.0, 40.0, 10) == 10);
    CHECK(approach_throttle(250.0, 40.0, 10) == 5);
    CHECK(approach_throttle(250.0, 40.0, 4) == 2);
    CHECK(approach_throttle(1.0, 0.0, 10) == 1);
    CHECK(approach_throttle(0.0, 0.0, 10) == 0);
    CHECK(approach_throttle(-3.0, 0.0, 10) == 0);
    CHECK(approach_throttle(250.0, 40.0, 0) == 0);
    for (double d = 1.0; d < 600.0; d += 7.0) { const int t = approach_throttle(d, 0.0, 10); CHECK(t >= 1 && t <= 10); }
    NEAR(approach_timeout_s(300.0, true, 0.333), 300.0 / 0.333, 1e-9);
    CHECK(approach_timeout_s(300.0, false, 0.333) == 300.0);
    CHECK(approach_timeout_s(300.0, true, 0.0) == 300.0);
    CHECK(approach_timeout_s(300.0, true, 1.0) == 300.0);
    CHECK(lowpower_throttle_cap(10, true, 5) == 5);
    CHECK(lowpower_throttle_cap(3, true, 5) == 3);
    CHECK(lowpower_throttle_cap(10, false, 5) == 10);
    CHECK(lowpower_throttle_cap(10, true, 0) == 10);
    CHECK(power_no_power(40.0, 0.0, 0.0));
    CHECK(!power_no_power(0.0, 0.0, 0.0));
    CHECK(!power_no_power(200.0, 80.0, 0.4));
}

static void t_lp_state_from()
{
    bool lp = false;
    int thr = -1;
    double frac = -1.0;
    CHECK(lp_state_from(true, 0.0, 40.0, 0.0, false, false, true, 5, 0.333, lp, thr, frac) && lp && thr == 5);   // grid dead, limits ok
    NEAR(frac, 0.333, 1e-12);
    CHECK(!lp_state_from(true, 80.0, 200.0, 0.4, false, false, true, 5, 0.333, lp, thr, frac) && !lp && thr == 0 && frac == 0.0);   // brown-out
    CHECK(lp_state_from(false, 0.0, 0.0, 0.0, true, true, true, 5, 0.333, lp, thr, frac) && lp && thr == 5);   // no grid, flag known and on
    CHECK(!lp_state_from(false, 0.0, 0.0, 0.0, false, false, true, 5, 0.333, lp, thr, frac) && !lp);          // flag unknown
    CHECK(!lp_state_from(true, 0.0, 40.0, 0.0, false, false, false, 5, 0.333, lp, thr, frac) && !lp && thr == 0 && frac == 0.0);   // dead, limits unreadable
}

static void t_phase1_steer()
{
    CHECK(phase1_decide(false, 1.9, 0.0) == P1_FACE_OK);
    CHECK(phase1_decide(false, 5.0, 30.0) == P1_FACE_TIMEOUT);
    CHECK(phase1_decide(false, 5.0, 29.9) == P1_NONE);
    CHECK(phase1_decide(true, 9.9, 0.0) == P1_TO_PHASE2);
    CHECK(phase1_decide(true, 10.0, 0.0) == P1_NONE);
}

static void t_approach_step()
{
    ApproachOut o = approach_step(5000.0, 0.0, 0.0, 10, false, 0, 0, 0.0, 0.25);
    CHECK(o.target == 10 && o.thr == 0 && NEARB(o.thr_t, 0.25, 1e-12));          // ramp not yet
    ApproachOut o2 = approach_step(5000.0, 0.0, 0.0, 10, false, 0, o.thr, o.thr_t, 0.25);
    CHECK(o2.thr == 1 && o2.thr_t == 0.0);                                       // 0.5 s: one step up
    o = approach_step(100.0, 0.0, 0.0, 10, false, 0, 5, 0.3, 0.25);
    CHECK(o.target == 2 && o.thr == 2 && o.thr_t == 0.0);                       // falls straight to target
    o = approach_step(0.0, 5.0, 0.0, 10, false, 0, 3, 0.2, 0.25);
    CHECK(o.target == -2 && o.thr == -2 && !o.done);                             // past stop, still moving: brake
    o = approach_step(-10.0, 5.0, 0.0, 10, false, 0, 3, 0.2, 0.25);
    CHECK(o.target == -2 && o.thr == -2 && !o.done);
    o = approach_step(0.0, 0.5, 0.0, 10, false, 0, -2, 0.0, 0.25);
    CHECK(o.done);                                                               // nearly stopped: done
    o = approach_step(5000.0, 0.0, 12.0, 10, false, 0, 5, 0.0, 0.25);
    CHECK(o.target == 0 && o.thr == 0);                                          // heading error > 10 deg
    o = approach_step(5000.0, 0.0, 0.0, 4, false, 0, 0, 0.5, 0.5);
    CHECK(o.target == 4 && o.thr == 1);                                          // steps cap
    int thr = 0;
    double tt = 0.0;
    for (int i = 0; i < 100; ++i)
    {
        o = approach_step(5000.0, 0.0, 0.0, 10, true, 3, thr, tt, 0.25);
        thr = o.thr;
        tt = o.thr_t;
        CHECK(o.target <= 3 && thr <= 3);                                        // low-power cap 3
    }
    CHECK(thr == 3);
    o = approach_step(5000.0, 0.0, 0.0, 10, false, 0, -2, 0.0, 0.25);
    CHECK(o.thr == 0);                                                           // pushed back out: restart from 0
}

static void t_view_and_ore()
{
    CHECK(!ore_refresh_due(150.0, false, 0.0));
    CHECK(ore_refresh_due(50.0, false, 0.0));
    CHECK(ore_refresh_due(50.0, true, 1.0));
    CHECK(!ore_refresh_due(50.0, true, 0.5));
    double yaw = -1.0, pit = -1.0;
    bool aligned = false;
    const double up[3] = {0, 0, 1}, dn[3] = {0, -1, 0}, zero[3] = {0, 0, 0};
    CHECK(view_offset(1.9, up, yaw, pit, aligned) && aligned && yaw == 0.0 && pit == 0.0);
    CHECK(view_offset(5.0, up, yaw, pit, aligned) && !aligned && NEARB(yaw, 12.0, 1e-9) && NEARB(pit, 0.0, 1e-9));
    CHECK(view_offset(5.0, dn, yaw, pit, aligned) && NEARB(yaw, 0.0, 1e-9) && NEARB(pit, 12.0, 1e-9));
    CHECK(view_offset(18.0, up, yaw, pit, aligned) && NEARB(yaw, 20.0, 1e-9));
    CHECK(view_offset(5.0, zero, yaw, pit, aligned) && yaw == 0.0 && pit == 0.0);
    const double ship[3] = {0, 0, 0}, tgt[3] = {300, 400, 0};
    NEAR(remain_dist_m(ship, tgt, 100.0), 400.0, 1e-9);
}

static void t_aim_and_rloc()
{
    ScFrame f;   // at the origin, nose +X, right +Y, up +Z
    ScAim r;
    const double ahead[3] = {1000, 0, 0}, behind[3] = {-1000, 0, 0}, right[3] = {0, 1000, 0}, above[3] = {0, 0, 1000}, here[3] = {0, 0, 0};
    CHECK(aim_pt(f, ahead, r) && NEARB(r.err_deg, 0.0, 1e-9) && NEARB(r.dist_m, 10.0, 1e-9));
    CHECK(aim_pt(f, behind, r) && NEARB(r.err_deg, 180.0, 1e-6));
    CHECK(aim_pt(f, right, r) && NEARB(r.err_deg, 90.0, 1e-6) && r.a_body[2] > 0.99);   // right: turn axis +U
    CHECK(aim_pt(f, above, r) && r.a_body[1] < -0.99);                                 // above: turn axis -R
    CHECK(!aim_pt(f, here, r));                                                        // point at the ship: degenerate

    ScFrame g;
    g.loc[0] = 1000; g.loc[1] = 2000; g.loc[2] = 3000;
    const int64_t c0[3] = {0, 0, 0};
    const double l0[3] = {0, 0, 0};
    const double tl[3] = {655360.0, 0, 0};   // 10 m raw
    double rl[3] = {0, 0, 0};
    CHECK(pt_rloc_calc(g, 5, c0, l0, 5, c0, tl, rl));
    NEAR(rl[0], 2000.0, 1e-9); NEAR(rl[1], 2000.0, 1e-9); NEAR(rl[2], 3000.0, 1e-9);   // same chunk: loc + 1000 uu
    const int64_t c1[3] = {1, 0, 0};
    const double z[3] = {0, 0, 0};
    CHECK(pt_rloc_calc(g, 5, c0, l0, 5, c1, z, rl));
    NEAR(rl[0], 1000.0 + 204800.0, 1e-6);                                                  // one chunk: + 2048 m
    CHECK(!pt_rloc_calc(g, 5, c0, l0, 6, c0, tl, rl));                                     // other system
}

static void t_qcal_and_frame()
{
    const double q[4] = {0.8660254037844386, 0.16666666666666666, 0.3333333333333333, 0.3333333333333333};   // unit, generic axis
    double fw[3], R[3], U[3];
    qcal_out(q, 0, 0, fw);
    qcal_out(q, 0, 2, R);
    qcal_out(q, 0, 4, U);
    int qm = -1, ax[3] = {-1, -1, -1};
    CHECK(qcal_pick(q, fw, R, U, qm, ax) && qm == 0 && ax[0] == 0 && ax[1] == 2 && ax[2] == 4);
    const double ident[4] = {1, 0, 0, 0}, X[3] = {1, 0, 0}, Y[3] = {0, 1, 0}, Z[3] = {0, 0, 1};
    CHECK(!qcal_pick(q, X, Y, Z, qm, ax));                                                  // axes not from this quat
    double a2[3], b2[3], c2[3];
    qcal_apply(q, 0, ax, a2, b2, c2);
    NEAR(dot3(a2, b2), 0.0, 1e-9); NEAR(norm3(a2), 1.0, 1e-9);
    NEAR(quat_angle_deg(ident, ident), 0.0, 1e-9);
    const double half = std::sqrt(0.5);
    const double quarter[4] = {half, 0, 0, half};   // 90 deg about Z
    NEAR(quat_angle_deg(ident, quarter), 90.0, 1e-6);

    MsFlight f;
    std::memset(&f, 0, sizeof(f));
    ScFrame out;
    std::wstring why;
    f.flags = FF_ACTOR_FRAME;
    f.act_loc[0] = 1; f.act_loc[1] = 2; f.act_loc[2] = 3;
    f.fw[0] = 0; f.fw[1] = 1; f.fw[2] = 0;
    f.R[0] = -1; f.R[1] = 0; f.R[2] = 0;
    f.U[2] = 1;
    CHECK(frame_from_flight(f, out, why) && out.loc[2] == 3.0 && out.fw[1] == 1.0 && out.R[0] == -1.0);   // actor frame copied
    std::memset(&f, 0, sizeof(f));
    f.flags = FF_OBJ_POSE;
    CHECK(!frame_from_flight(f, out, why) && why == L"ship frame unreadable");          // object pose, no calibration
    f.ch[0] = 1; f.ch[1] = 2; f.ch[2] = 3;
    f.loc_raw[0] = 65536.0 * 5; f.loc_raw[2] = 65536.0 * 7;
    f.quat[0] = 1.0;
    g_calib = ScCalib();
    g_calib.ok = true;
    CHECK(frame_from_flight(f, out, why) && NEARB(out.loc[0], 205300.0, 1e-6) && NEARB(out.loc[1], 409600.0, 1e-6) && NEARB(out.loc[2], 615100.0, 1e-6));
    g_calib = ScCalib();

    std::memset(&f, 0, sizeof(f));
    f.flags = FF_ACTOR_FRAME | FF_OBJ_POSE;
    f.quat[0] = q[0]; f.quat[1] = q[1]; f.quat[2] = q[2]; f.quat[3] = q[3];
    qcal_out(q, 0, 0, f.fw);
    qcal_out(q, 0, 2, f.R);
    qcal_out(q, 0, 4, f.U);
    CHECK(sc_calib_learn(f) && g_calib.ok && g_calib.qmode == 0 && g_calib.ax[0] == 0 && g_calib.ax[1] == 2 && g_calib.ax[2] == 4);
    CHECK(!sc_calib_learn(f));                                                              // learned once
    g_calib = ScCalib();
    f.flags = FF_OBJ_POSE;
    CHECK(!sc_calib_learn(f) && !g_calib.ok);                                               // no actor frame: cannot teach
}

// SC2b A2: move-job ABI sizes (also checked by static_asserts in sc_api.h) and the pure view raw conversion of sc_move.hpp.
static void t_move_abi()
{
    CHECK(SC_API_LEVEL == 3);
    CHECK(sizeof(ScHost) == 80 && SC_HOST_SIZE_L2 == 24);
    CHECK(sizeof(ScMoveReq) == 128 && sizeof(ScMoveStatus) == 64);
    CHECK(sc_view_raw(1.0) == 65536 && sc_view_raw(-0.5) == -32768 && sc_view_raw(0.0) == 0);   // llround of deg * 65536
}

int main()
{
    std::string why;

    // free ship: first claim wins
    {
        ScOwners o;
        CHECK(o.owner_of(7).empty());
        CHECK(o.claim(7, "mining", why));
        CHECK(o.owner_of(7) == "mining");
    }
    // same owner again: ok, still held
    {
        ScOwners o;
        o.claim(7, "mining", why);
        CHECK(o.claim(7, "mining", why));
        CHECK(o.owner_of(7) == "mining");
    }
    // other owner: refused, reason names the holder, holder unchanged
    {
        ScOwners o;
        o.claim(7, "mining", why);
        why.clear();
        CHECK(!o.claim(7, "attack", why));
        CHECK(why == "owned by mining");
        CHECK(o.owner_of(7) == "mining");
    }
    // release by holder frees the ship; a second owner can then claim it
    {
        ScOwners o;
        o.claim(7, "mining", why);
        CHECK(o.release(7, "mining", why));
        CHECK(o.owner_of(7).empty());
        CHECK(o.claim(7, "attack", why));
    }
    // release by non-holder refused, ship keeps its owner
    {
        ScOwners o;
        o.claim(7, "mining", why);
        why.clear();
        CHECK(!o.release(7, "attack", why));
        CHECK(why == "owned by mining");
        CHECK(o.owner_of(7) == "mining");
    }
    // release of a free ship is a no-op success
    {
        ScOwners o;
        CHECK(o.release(9, "mining", why));
    }
    // release_all frees only that owner's ships and returns the count
    {
        ScOwners o;
        o.claim(1, "mining", why);
        o.claim(2, "mining", why);
        o.claim(3, "attack", why);
        CHECK(o.release_all("mining") == 2);
        CHECK(o.owner_of(1).empty());
        CHECK(o.owner_of(2).empty());
        CHECK(o.owner_of(3) == "attack");
        CHECK(o.release_all("mining") == 0);
    }

    t_warp_settle();
    t_hazard_dir();
    t_path_block_geom();
    t_path_waypoint_geom();
    t_path2_gates();
    t_tune_geom_path();

    // SC2a: speed judge (sc_speed.hpp, pure) and owner refusal for SC_zero / SC_watch (sc_owner.hpp may_act)
    {
        SpeedMon sp;
        double r = 0.0;
        CHECK(speed_judge(sp, r) == SJ_NONE);          // no valid speed yet
        sp.have = true; sp.v = 50.0; sp.lim = 100.0;
        CHECK(speed_judge(sp, r) == SJ_OK && r == 0.5);
        sp.v = 110.0;                                  // ratio 1.1 is not above the abort ratio
        CHECK(speed_judge(sp, r) == SJ_OK);
        sp.v = 120.0;                                  // first step above: report once
        CHECK(speed_judge(sp, r) == SJ_OVER_NEW && sp.aborted);
        CHECK(speed_judge(sp, r) == SJ_OVER);          // still over: no second report
        sp.v = 90.0;                                   // back under 1.0 re-arms the report
        CHECK(speed_judge(sp, r) == SJ_OK && !sp.aborted);
        sp.v = 130.0;
        CHECK(speed_judge(sp, r) == SJ_OVER_NEW);
        sp.v = 1100.0;                                 // ratio 11 > glitch ratio: sample dropped
        CHECK(speed_judge(sp, r) == SJ_GLITCH && !sp.have);
        sp.have = true; sp.lim = 0.0;                  // no limit: no decision
        CHECK(speed_judge(sp, r) == SJ_NONE);
    }
    {
        ScOwners o;
        CHECK(o.may_act(5, "mining", why));            // free ship: any owner may act
        o.claim(5, "attack", why);
        why.clear();
        CHECK(!o.may_act(5, "mining", why) && why == "owned by attack");
        CHECK(o.may_act(5, "attack", why));            // holder may act
    }

    t_moved_steer();
    t_lp_state_from();
    t_phase1_steer();
    t_approach_step();
    t_view_and_ore();
    t_aim_and_rloc();
    t_qcal_and_frame();
    t_move_abi();

    std::printf("test_shipcontrol: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
