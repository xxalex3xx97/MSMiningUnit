// FILE: tests/test_shipcontrol.cpp
// WHAT: unit checks for the ShipControl owner registry (mods/ShipControl/sc_owner.hpp): claim, same-owner claim, refused claim, release, release_all, owner_of. No game needed.
// WHY: the registry decides which behaviour may drive a ship; the checks keep the refuse rules fixed.
// RUN: tests\run.ps1 (builds with cl, runs, exit code 0 = all PASS). Or by hand: see run.ps1.
// DEPENDS ON: mods/ShipControl/sc_owner.hpp (pure std, no UE headers).
// NOTE: CHECK counts failures and keeps going; the summary line prints the pass and fail counts.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sc_logic_path.hpp"
#include "sc_owner.hpp"
#include "sc_speed.hpp"   // SC2a: speed judge (pure part); includes sc_mscore.hpp (windows.h only)

static int g_pass = 0;
static int g_fail = 0;
#define NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

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

    std::printf("test_shipcontrol: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
