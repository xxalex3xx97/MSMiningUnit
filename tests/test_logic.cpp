// FILE: tests/test_logic.cpp
// WHAT: unit checks for mb_logic.hpp (parsing, command line, belt, fuel, cargo, dock and hull rules) and mb_math.hpp (quaternion, vector, angle). No game needed.
// WHY: these functions decide what the mining loop does; the checks keep their rules fixed.
// RUN: tests\run.ps1 (builds with cl, runs, exit code 0 = all PASS). Or by hand: see run.ps1.
// DEPENDS ON: mods/MiningBenchMod/mb_logic.hpp, mods/MiningBenchMod/mb_math.hpp (mb_math needs <cmath>, included first).
// NOTE: CHECK counts failures and keeps going; the summary line prints the pass and fail counts.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "mb_logic.hpp"
#include "mb_math.hpp"
#include "mb_registry_logic.hpp"
#include "mb_wd_logic.hpp"

static int g_pass = 0;
static int g_fail = 0;
static const char* g_cur = "";

#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::fprintf(stderr, "FAIL [%s] %s:%d: %s\n", g_cur, __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

// reset the global config state the apply/eff tests touch
static void reset_globals()
{
    g_cfg = Config();
    g_ship_kv.clear();
    g_item_idx.clear();
    g_dock_tags.clear();
    g_ship_names.clear();
}

// ---------- mb_math.hpp ----------

static void t_qmul_identity()
{
    const double id[4] = {1, 0, 0, 0};
    const double b[4] = {0.5, 0.5, 0.5, 0.5};
    double o[4];
    qmul(id, b, o);
    for (int i = 0; i < 4; ++i) CHECK(o[i] == b[i]);
    qmul(b, id, o);
    for (int i = 0; i < 4; ++i) CHECK(o[i] == b[i]);
}

static void t_qconj()
{
    const double a[4] = {1, 2, 3, 4};
    double o[4];
    qconj(a, o);
    CHECK(o[0] == 1 && o[1] == -2 && o[2] == -3 && o[3] == -4);
}

static void t_cross_dot_norm_dist()
{
    const double x[3] = {1, 0, 0}, y[3] = {0, 1, 0};
    double z[3];
    cross3(x, y, z);
    CHECK(z[0] == 0 && z[1] == 0 && z[2] == 1);
    const double p[3] = {1, 2, 3}, q[3] = {4, 5, 6};
    CHECK(dot3(p, q) == 32);
    const double v[3] = {3, 4, 0};
    CHECK(norm3(v) == 5);
    const double a[3] = {1, 1, 1}, b[3] = {4, 5, 1};
    CHECK(dist3(a, b) == 5);
}

static void t_quat_angle()
{
    const double id[4] = {1, 0, 0, 0};
    CHECK(quat_angle_deg(id, id) == 0.0);

    // 90 deg about Z
    const double s = std::sqrt(0.5);
    const double q90[4] = {s, 0, 0, s};
    NEAR(quat_angle_deg(id, q90), 90.0, 1e-6);

    // same rotation with w < 0 (q and -q): short way round, still 90 deg
    const double q90n[4] = {-s, 0, 0, -s};
    NEAR(quat_angle_deg(id, q90n), 90.0, 1e-6);

    // 180 deg about Z
    const double q180[4] = {0, 0, 0, 1};
    NEAR(quat_angle_deg(id, q180), 180.0, 1e-6);
}

static void t_rotvec_zero()
{
    const double id[4] = {1, 0, 0, 0};
    double rv[3];
    rotvec(id, rv);
    CHECK(rv[0] == 0 && rv[1] == 0 && rv[2] == 0);
    const double tiny[4] = {1, 1e-12, 0, 0};
    rotvec(tiny, rv);
    CHECK(rv[0] == 0 && rv[1] == 0 && rv[2] == 0);
}

// ---------- mb_logic.hpp: text and parsing ----------

static void t_trim()
{
    CHECK(trim("  a b \r\n") == "a b");
    CHECK(trim("   ") == "");
    CHECK(trim("") == "");
}

static void t_to_int()
{
    CHECK(to_int("12", 7) == 12);
    CHECK(to_int("x", 7) == 7);
    CHECK(to_int("", 7) == 7);
    CHECK(to_int("99999999999999", 7) == 7);  // out of range -> default
    CHECK(to_int("12abc", 7) == 12);          // ACTUAL: std::stoi reads the leading digits
}

static void t_parse_distance()
{
    CHECK(parse_distance("5km") == 5000.0);
    CHECK(parse_distance("2500m") == 2500.0);
    CHECK(parse_distance("10") == 10.0);
    CHECK(parse_distance(" 5 km ") == 5000.0);
    CHECK(parse_distance("5KM") == 5000.0);
    CHECK(parse_distance("1e3km") == 1000000.0);
    CHECK(parse_distance("0") == 0.0);
    CHECK(parse_distance("") == -1.0);
    CHECK(parse_distance("abc") == -1.0);
    CHECK(parse_distance("-5") == -1.0);
    CHECK(parse_distance("km") == -1.0);
    CHECK(parse_distance("5kmm") == -1.0);
    CHECK(parse_distance("5 x") == -1.0);

    // non-finite results are rejected (review finding 13)
    CHECK(parse_distance("inf") == -1.0);
    CHECK(parse_distance("infkm") == -1.0);
    CHECK(parse_distance("-inf") == -1.0);
    CHECK(parse_distance("nan") == -1.0);
    CHECK(parse_distance("nankm") == -1.0);
    CHECK(parse_distance("1e308km") == -1.0);   // overflows to inf after the km multiply
    // ACTUAL behaviour kept (not changed): hex is accepted through strtod
    CHECK(parse_distance("0x10") == 16.0);
    CHECK(parse_distance("0x10m") == 16.0);
}

static void t_parse_items()
{
    auto a = parse_items("ITEM_A:5,ITEM_B:0");
    CHECK(a.size() == 2);
    CHECK(a[0].first == "ITEM_A" && a[0].second == 5);
    CHECK(a[1].first == "ITEM_B" && a[1].second == 0);

    auto b = parse_items("ITEM_A");                 // no ':' -> amount 0
    CHECK(b.size() == 1 && b[0].first == "ITEM_A" && b[0].second == 0);

    auto c = parse_items("a:1,,b:2,");              // empty segments skipped, order kept
    CHECK(c.size() == 2 && c[0].first == "a" && c[1].first == "b" && c[1].second == 2);

    auto d = parse_items(" a : 3 , b:x ");          // spaces trimmed, bad amount -> 0
    CHECK(d.size() == 2 && d[0].first == "a" && d[0].second == 3 && d[1].first == "b" && d[1].second == 0);

    CHECK(parse_items("").empty());
    CHECK(parse_items(",,").empty());

    auto e = parse_items("a:1:2");                  // ACTUAL: split at the last ':' -> name "a:1", amount 2
    CHECK(e.size() == 1 && e[0].first == "a:1" && e[0].second == 2);
}

static void t_ok_res()
{
    CHECK(ok_res(L"ok"));
    CHECK(ok_res(L"ok: done"));
    CHECK(!ok_res(L"error: ship not found"));
    CHECK(!ok_res(L""));
    CHECK(!ok_res(L"Ok"));                          // case-sensitive
}

// ---------- mb_logic.hpp: config apply and eff_cfg ----------

static void t_apply_kv_empty_keeps_old()
{
    reset_globals();
    Config c;
    c.amount = 7;
    c.resource = "keep";
    std::map<std::string, std::string> kv = {{"amount", ""}, {"resource", ""}};
    apply_kv(c, kv, false);
    CHECK(c.amount == 7);
    CHECK(c.resource == "keep");
}

static void t_apply_kv_dock_per_ship()
{
    Config c;
    c.dock_name_filter = "Old Station";
    std::map<std::string, std::string> kv = {{"dock_id", "123"}};
    apply_kv(c, kv, true);                          // per_ship: inherited filter cleared so the id wins
    CHECK(c.dock_id == 123);
    CHECK(c.dock_name_filter.empty());

    Config g;
    g.dock_name_filter = "Old Station";
    apply_kv(g, kv, false);                         // global: filter kept
    CHECK(g.dock_id == 123);
    CHECK(g.dock_name_filter == "Old Station");

    Config n;
    std::map<std::string, std::string> kvn = {{"dock_id", "Station Alpha"}};
    apply_kv(n, kvn, false);                        // non-digit = name, not id
    CHECK(n.dock_id == 0);
    CHECK(n.dock_id_name == "Station Alpha");
}

static void t_apply_kv_items_empty_clears()
{
    reset_globals();
    g_cfg.resource = "old";
    g_ship_kv[5] = {{"items", ""}};
    Config c = eff_cfg(5);
    CHECK(c.has_items);
    CHECK(c.items.empty());
    CHECK(c.resource.empty());
    CHECK(c.amount == 0);
}

static void t_dock_lists()
{
    reset_globals();
    CHECK(split_csv(" 1, ,2 ,") == (std::vector<std::string>{"1", "2"}));
    CHECK(dock_list("1, 2,,x,0,3") == (std::vector<int>{1, 2, 3}));
    g_cfg.ore_docks[2] = {9};
    g_ship_kv[5] = {{"unload_docks_cobalt", "5,6"}, {"refuel_docks", "7"}};
    Config c = eff_cfg(5);
    CHECK((c.ore_docks[2] == std::vector<int>{5, 6}));
    CHECK((c.refuel_docks == std::vector<int>{7}));
    CHECK(c.ore_docks[0].empty());
    g_ship_kv[5] = {{"unload_docks_cobalt", ""}};          // present but empty clears the list
    c = eff_cfg(5);
    CHECK(c.ore_docks[2].empty());
    CHECK((eff_cfg(6).ore_docks[2] == std::vector<int>{9})); // other ship keeps the global list
}

static void t_eff_cfg_items_row()
{
    reset_globals();
    g_ship_kv[7] = {{"items", "ITEM_A:4,ITEM_B:9"}};
    g_item_idx[7] = 1;
    Config c = eff_cfg(7);
    CHECK(c.resource == "ITEM_B" && c.amount == 9);

    g_item_idx[7] = 9;                              // out of range -> row 0
    c = eff_cfg(7);
    CHECK(c.resource == "ITEM_A" && c.amount == 4);

    g_item_idx[7] = -1;                             // negative -> row 0
    c = eff_cfg(7);
    CHECK(c.resource == "ITEM_A" && c.amount == 4);
}

static void t_eff_cfg_does_not_touch_global()
{
    reset_globals();
    g_cfg.amount = 3;
    g_ship_kv[9] = {{"amount", "50"}, {"items", "X:1"}};
    Config c = eff_cfg(9);
    CHECK(c.resource == "X" && c.amount == 1);
    CHECK(g_cfg.amount == 3);                       // global unchanged
    CHECK(!g_cfg.has_items);
    Config other = eff_cfg(10);                     // no section: plain global copy
    CHECK(other.amount == 3);
}

// ---------- mb_logic.hpp: command line ----------

static void t_parse_cmd_line()
{
    int seq = -1, sid = -1;
    std::string cmd;
    CHECK(parse_cmd_line("123|41529|start", seq, sid, cmd));
    CHECK(seq == 123 && sid == 41529 && cmd == "start");

    CHECK(parse_cmd_line("1730000123456|41529|stop", seq, sid, cmd));   // 13-digit ms epoch: last 9 kept
    CHECK(seq == 123456);

    CHECK(parse_cmd_line("1|2|  dock  ", seq, sid, cmd));               // cmd trimmed
    CHECK(cmd == "dock");

    CHECK(parse_cmd_line("1|2|a|b", seq, sid, cmd));                    // ACTUAL: cmd keeps later pipes
    CHECK(cmd == "a|b");

    CHECK(!parse_cmd_line("", seq, sid, cmd));
    CHECK(!parse_cmd_line("1|2", seq, sid, cmd));                       // one pipe
    CHECK(!parse_cmd_line("x|2|cmd", seq, sid, cmd));                   // seq not digits
    CHECK(!parse_cmd_line("-1|2|cmd", seq, sid, cmd));                  // sign not allowed
    CHECK(!parse_cmd_line("|2|cmd", seq, sid, cmd));                    // empty seq
    CHECK(!parse_cmd_line("1||cmd", seq, sid, cmd));                    // empty ship
    CHECK(!parse_cmd_line("1|-2|cmd", seq, sid, cmd));                  // ship not digits
    CHECK(!parse_cmd_line("1|2|", seq, sid, cmd));                      // empty cmd
    CHECK(!parse_cmd_line("1|2|   ", seq, sid, cmd));                   // blank cmd
}

static void t_cmd_seq_is_new()
{
    CHECK(cmd_seq_is_new(5, 4));
    CHECK(!cmd_seq_is_new(5, 5));
    CHECK(cmd_seq_is_new(0, 999999999));            // wrap-around is still new
}

// ---------- mb_logic.hpp: belt and warp ----------

static void t_belt()
{
    CHECK(belt_off(""));
    CHECK(belt_off("none"));
    CHECK(!belt_off("auto"));
    CHECK(!belt_off("1008"));

    CHECK(belt_want("1008") == 1008);
    CHECK(belt_want("auto") == 0);
    CHECK(belt_want("") == 0);
    CHECK(belt_want("none") == 0);
    CHECK(belt_want("12a") == 0);
    CHECK(belt_want("-5") == 0);

    CHECK(belt_score(5, 5, 5, 100.0) == -2.0);      // wanted wins over current
    CHECK(belt_score(3, 0, 3, 250.0) == -1.0);      // current sector
    CHECK(belt_score(7, 0, 3, 250.0) == 250.0);     // other: distance
}

static void t_csq()
{
    const int64_t a[3] = {0, 0, 0}, b[3] = {1, 2, 2};
    CHECK(csq(a, b) == 9.0);
    CHECK(csq(b, a) == 9.0);
}

static void t_sector_contains()
{
    const int64_t c[3] = {0, 0, 0}, he[3] = {2, 2, 2};
    const int64_t in1[3] = {2, -2, 0}, edge[3] = {-2, 2, 2};
    const int64_t out1[3] = {3, 0, 0}, out2[3] = {0, 0, -3};
    CHECK(sector_contains(c, he, in1));
    CHECK(sector_contains(c, he, edge));            // box edge is inside
    CHECK(!sector_contains(c, he, out1));
    CHECK(!sector_contains(c, he, out2));
}

static void t_warp_fuel()
{
    CHECK(warp_fuel_need(0.0) == 0);
    CHECK(warp_fuel_need(1.0) == 2);                // 2.048 km -> 1 item, x2 round trip
    CHECK(warp_fuel_need(25.0) == 4);               // 51.2 km -> 2 items, x2
    CHECK(warp_fuel_need(100.0) == 10);             // 204.8 km -> 5 items, x2

    CHECK(warp_fuel_ok(true, 25.0, 0, 4));          // same system: need 4 <= drive 4
    CHECK(!warp_fuel_ok(true, 25.0, 0, 3));
    CHECK(warp_fuel_ok(false, 9999.0, 5, 10));      // other system: 2 * jump_cost <= drive, distance ignored
    CHECK(!warp_fuel_ok(false, 0.0, 5, 9));
}

// ---------- mb_logic.hpp: rules ----------

static void t_cargo_pct()
{
    CHECK(cargo_pct_calc(10, 2, 0) == 0);
    CHECK(cargo_pct_calc(2, 2, 0) == 0);            // no items used: 0 even with no base
    CHECK(cargo_pct_calc(10, 2, 4) == 50);          // 4 / 8
    CHECK(cargo_pct_calc(10, 2, 8) == 100);
    CHECK(cargo_pct_calc(10, 2, 9) == 100);         // clamped
    CHECK(cargo_pct_calc(10, 0, 1) == 10);
    CHECK(cargo_pct_calc(2, 2, 1) == -1);           // no free slot base
    CHECK(cargo_pct_calc(2, 3, 1) == -1);
}

static void t_fuel_low()
{
    FuelCfg f;
    f.threshold = 300;
    CHECK(fuel_low(f, 299));
    CHECK(!fuel_low(f, 300));
    CHECK(fuel_low(f, 0));
    CHECK(!fuel_low(f, -1));                        // unreadable: never low
}

static void t_got_delta()
{
    CHECK(got_delta(10, 4) == 6);
    CHECK(got_delta(4, 10) == 0);
    CHECK(got_delta(5, 5) == 0);
}

static void t_row_done()
{
    CHECK(!row_done(0, 1000));                      // amount 0 = unlimited
    CHECK(!row_done(5, 4));
    CHECK(row_done(5, 5));
    CHECK(row_done(5, 6));
}

static void t_item_next()
{
    bool w = true;
    CHECK(item_next(0, 3, w) == 1 && !w);
    CHECK(item_next(2, 3, w) == 0 && w);            // passed last row: wrap
    CHECK(item_next(0, 1, w) == 0 && w);
}

static void t_collect_verdict()
{
    CHECK(collect_verdict(0, 1, 1) == COL_DONE);
    CHECK(collect_verdict(3, 1, 1) == COL_WAIT);
    CHECK(collect_verdict(3, 30, 0) == COL_GIVEUP);
    CHECK(collect_verdict(3, 5, 8) == COL_GIVEUP);
    CHECK(collect_verdict(0, 99, 99) == COL_DONE);
}

static void t_sweep()
{
    CHECK(sweep_nearest({5, 2, 9}, {0, 0, 0}) == 1);
    CHECK(sweep_nearest({5, 2, 9}, {0, 1, 0}) == 0);
    CHECK(sweep_nearest({1}, {1}) == -1);
    CHECK(sweep_nearest({}, {}) == -1);
    CHECK(sweep_fly_back(true, 4096) == true);
    CHECK(sweep_fly_back(true, 4097) == false);
    CHECK(sweep_fly_back(false, 1) == false);
}

static void t_ore_scan_due()
{
    CHECK(ore_scan_due(true, 1000, 1001, 60000) == true);     // forced
    CHECK(ore_scan_due(false, 0, 5, 60000) == true);          // never run
    CHECK(ore_scan_due(false, 1000, 60999, 60000) == false);  // within the period
    CHECK(ore_scan_due(false, 1000, 61000, 60000) == true);   // period passed
}

static void t_idle_limit()
{
    CHECK(idle_limit(5, true) == 30.0);      // rock alive: at least IDLE_ROCK_MIN_S
    CHECK(idle_limit(45, true) == 45.0);     // longer setting kept
    CHECK(idle_limit(5, false) == 5.0);      // rock gone: idle_seconds as set
    CHECK(idle_limit(0, true) == 0.0);       // 0 = off
}

static void t_hull_dropped()
{
    CHECK(!hull_dropped(-1, 0));                    // no baseline: never a drop
    CHECK(hull_dropped(350, 320));
    CHECK(!hull_dropped(350, 350));
    CHECK(!hull_dropped(350, 351));
    CHECK(!hull_dropped(0, 0));
}

static void t_dead_scan()
{
    CHECK(SHIP_DEAD_SCANS == 2);
    int n = 0;
    n = dead_scan_next(n, true);  CHECK(n == 1 && !dead_confirmed(n));   // one signal: not destroyed yet
    n = dead_scan_next(n, false); CHECK(n == 0 && !dead_confirmed(n));   // transient: reset
    n = dead_scan_next(n, true);  CHECK(!dead_confirmed(n));
    n = dead_scan_next(n, true);  CHECK(n == 2 && dead_confirmed(n));    // two in a row: destroyed
    n = dead_scan_next(n, true);  CHECK(dead_confirmed(n));              // stays destroyed while the signal lasts
}

static void t_dock_limit_giveup()
{
    CHECK(MINE_DOCK_RETRIES == 10);
    CHECK(!dock_limit_giveup(9));
    CHECK(dock_limit_giveup(10));
    CHECK(dock_limit_giveup(11));
}

static void t_loop_guards()
{
    // no-yield skip after 3 visits, rock lost HALT at 5, idle poll HALT at 12, dwait cap at 900 s, fuel floor
    CHECK(!noy_skip_now(2));
    CHECK(noy_skip_now(3));
    CHECK(rock_claim_live(true, false));
    CHECK(!rock_claim_live(true, true));
    CHECK(!rock_claim_live(false, false));
    CHECK(!rock_lost_halt(4));
    CHECK(rock_lost_halt(5));
    CHECK(!idle_poll_halt(11));
    CHECK(idle_poll_halt(12));
    CHECK(MINE_DWAIT_MAX_S == 900.0);
    CHECK(!dwait_halt(900.0));
    CHECK(dwait_halt(900.5));
    // every rock in range skipped: a rock exists ignoring the skip list but none with it
    CHECK(all_rocks_skipped(true, false));
    CHECK(!all_rocks_skipped(true, true));
    CHECK(!all_rocks_skipped(false, false));
    // all skipped: HALT only without a belt (off/none/empty); a belt moves on
    CHECK(noy_all_halt("none"));
    CHECK(noy_all_halt(""));
    CHECK(!noy_all_halt("auto"));
    CHECK(!noy_all_halt("12"));
    CHECK(fuel_amount_floor(100, 300) == 300);
    CHECK(fuel_amount_floor(500, 300) == 500);
    CHECK(fuel_amount_floor(0, 0) == 0);
}

static void t_rock_depleted()
{
    // before the first ore of a rock: never (the no-yield watchdog applies)
    CHECK(!rock_depleted(0, 0.0, true, false));
    CHECK(!rock_depleted(0, 60.0, true, false));
    // after ore: 5 s with no ore = depleted, less = not yet
    CHECK(rock_depleted(1, DEPLETE_S, true, false));
    CHECK(rock_depleted(8500, 9.0, true, false));
    CHECK(!rock_depleted(8500, DEPLETE_S - 0.1, true, false));
    // cargo full: no (the cargo-full rule takes it); laser off: no
    CHECK(!rock_depleted(8500, 30.0, true, true));
    CHECK(!rock_depleted(8500, 30.0, false, false));
    CHECK(DEPLETE_S == 5.0);
}

static void t_setting_clamp()
{
    const SettingBound* idle = find_setting_bound("idle_seconds");
    CHECK(idle != nullptr);
    CHECK(find_setting_bound("no_such_key") == nullptr);
    double o = -1.0;
    CHECK(clamp_setting(*idle, 10, o) && o == 30.0);       // below min -> min
    CHECK(clamp_setting(*idle, 9999, o) && o == 3600.0);   // above max -> max
    CHECK(!clamp_setting(*idle, 0, o) && o == 0.0);        // 0 = off, allowed and kept
    CHECK(!clamp_setting(*idle, 45, o) && o == 45.0);      // in range untouched
    const SettingBound* att = find_setting_bound("max_attempts");   // no off value: 0 -> min 1
    CHECK(att != nullptr);
    CHECK(clamp_setting(*att, 0, o) && o == 1.0);
    CHECK(clamp_setting(*att, 11, o) && o == 10.0);

    // clamp_kv: out of range clamped (one msg each), 0 = off kept, empty and unparsable left alone
    std::map<std::string, std::string> kv = {
        {"idle_seconds", "10"}, {"dock_hold_limit", "0"}, {"radius", "500km"}, {"max_radius", "0"},
        {"mining_distance", "abc"}, {"warp_range", ""}, {"fuel_warp_amount", "9000"}, {"warp_trip_km", "500"}};
    std::vector<std::string> msgs;
    clamp_kv(kv, msgs);
    CHECK(kv["idle_seconds"] == "30");
    CHECK(kv["dock_hold_limit"] == "0");
    CHECK(kv["radius"] == "100000m");
    CHECK(kv["max_radius"] == "0");
    CHECK(kv["mining_distance"] == "abc");
    CHECK(kv["warp_range"] == "");
    CHECK(kv["fuel_warp_amount"] == "5000");
    CHECK(kv["warp_trip_km"] == "100");
    CHECK(msgs.size() == 4);
    // warp_range below its min: 500 m -> 1000 m
    std::map<std::string, std::string> kv2 = {{"warp_range", "500"}};
    std::vector<std::string> m2;
    clamp_kv(kv2, m2);
    CHECK(kv2["warp_range"] == "1000m");
    CHECK(m2.size() == 1);
    CHECK(parse_distance(kv["radius"]) == 100000.0);
}

static void t_unload_chain_more()
{
    CHECK(unload_chain_more(-1, 5));
    CHECK(unload_chain_more(5, 3));
    CHECK(!unload_chain_more(5, 5));
    CHECK(!unload_chain_more(5, 0));
    CHECK(!unload_chain_more(-1, 0));
}

static void t_unload_cand()
{
    // ore 0 cargo+docks, 1 cargo+docks, 2 cargo no docks, 3 docks only, 5 cargo+docks, 6 none
    const std::vector<bool> cargo = {true, true, true, false, false, true, false};
    const std::vector<bool> docks = {true, true, false, true, false, true, false};
    const std::vector<int> got = unload_cand(cargo, docks);
    CHECK(got.size() == 3 && got[0] == 0 && got[1] == 1 && got[2] == 5);
    // silica (5) in cargo, waterice (4) has docks but is not in cargo: waterice is never a candidate
    const std::vector<bool> in1 = {false, false, false, false, false, true, false};
    const std::vector<bool> dk1 = {false, false, false, false, true, true, false};
    const std::vector<int> got1 = unload_cand(in1, dk1);
    CHECK(got1.size() == 1 && got1[0] == 5);
    CHECK(unload_cand(std::vector<bool>(7, false), docks).empty());
}

static void t_unload_misc()
{
    const std::vector<int> got = unload_misc_ores({true, false, true, false, false, false, false}, {false, false, true, false, false, false, false});
    CHECK(got.size() == 1 && got[0] == 0);
    CHECK(unload_misc_ores(std::vector<bool>(7, false), std::vector<bool>(7, false)).empty());
    CHECK(unload_misc_trip(0, 1, 0, true));
    CHECK(unload_misc_trip(0, 0, 2, true));
    CHECK(!unload_misc_trip(1, 1, 0, true));
    CHECK(!unload_misc_trip(0, 1, 0, false));
    CHECK(!unload_misc_trip(0, 0, 0, true));
}

static void t_refuel_stalled()
{
    CHECK(!refuel_stalled(true, 60, 10.0, 10, 60.0));   // all delivered
    CHECK(!refuel_stalled(false, 0, 10.0, 10, 60.0));   // idle off
    CHECK(!refuel_stalled(false, 60, 9.0, 10, 60.0));   // before wait
    CHECK(!refuel_stalled(false, 60, 10.0, 10, 59.0));  // not idle long enough
    CHECK(refuel_stalled(false, 60, 10.0, 10, 60.0));   // all true, boundaries included
}

static void t_mine_radius()
{
    Config c;
    c.belt = "none";   // belt mode caps the radius, see below
    c.radius = "";
    CHECK(mine_radius_m(c) == -1.0);                // unset = unlimited
    c.radius = "5km";
    CHECK(mine_radius_m(c) == 5000.0);
    c.radius = "10km";
    c.max_radius = "2km";
    CHECK(mine_radius_m(c) == 2000.0);              // clamped by max
    c.max_radius = "bad";
    CHECK(mine_radius_m(c) == 10000.0);             // bad max = no clamp
    c.radius = "0";
    CHECK(mine_radius_m(c) == -1.0);
    c.radius = "abc";
    CHECK(mine_radius_m(c) == -1.0);
    c.radius = "nan";
    CHECK(mine_radius_m(c) == -1.0);
    c.belt = "auto";                                // belt mode: rocks beyond 50 km count as none (warp instead)
    c.radius = "";
    CHECK(mine_radius_m(c) == MINE_BELT_NEAR_M);
    c.radius = "5km";
    CHECK(mine_radius_m(c) == 5000.0);
    c.radius = "200km";
    CHECK(mine_radius_m(c) == MINE_BELT_NEAR_M);
    c.warp_range = "20km";
    CHECK(mine_radius_m(c) == 20000.0);              // own warp range caps the radius
    c.radius = "";
    CHECK(mine_radius_m(c) == 20000.0);
    c.warp_range = "bad";
    CHECK(mine_radius_m(c) == MINE_BELT_NEAR_M);    // bad = default 50 km
}

static void t_station()
{
    CHECK(station_destroyable("BP_StationObject_C", "BP_StationObject_C"));
    CHECK(!station_destroyable("BP_AllyStationObject_C", "BP_StationObject_C"));
    CHECK(!station_destroyable("BP_AbandonedStationObject_01_C", "BP_StationObject_C"));
    CHECK(!station_destroyable("BPO_TradingOutpost_01_C", "BP_StationObject_C"));
    CHECK(!station_destroyable("", "BP_StationObject_C"));
    CHECK(!station_destroyable("BP_StationObject_C", ""));
    CHECK(station_row(7, "My|Base\n", "BP_StationObject_C", 3, "BP_StationObject_C") == "7|My Base |BP_StationObject_C|3|1");
    CHECK(station_row(8, "Outpost", "BP_AllyStationObject_C", 2, "BP_StationObject_C") == "8|Outpost|BP_AllyStationObject_C|2|0");
    CHECK(parse_station_id("123") == 123);
    CHECK(parse_station_id("") == 0);
    CHECK(parse_station_id("0") == 0);
    CHECK(parse_station_id("12a") == 0);
    CHECK(parse_station_id("-5") == 0);
    CHECK(parse_station_id("1234567890") == 0);
}

// ---------- dock network (mb_logic.hpp, net_*) ----------

static void t_net_free_go()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    const NetVerdict v = net_pick(d, s, t, 10, P_UNLOAD, NetReq(), {1}, 0, 5.0);
    CHECK(v.k == NetVerdict::GO && v.dock == 1);
    CHECK(d[1].resv == 10 && d[1].why == P_UNLOAD && d[1].since == 5.0);
    CHECK(s[10].ticket == 0 && s[10].waits_dock == 0);
}

static void t_net_busy_wait()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].occ = 7;
    const NetVerdict v = net_pick(d, s, t, 11, P_UNLOAD, NetReq(), {1}, 0, 6.0);
    CHECK(v.k == NetVerdict::WAIT && v.dock == 1 && v.holder == 7 && v.reason == NR_WAIT_BUSY);
    CHECK(s[11].ticket == 1 && s[11].waits_dock == 1 && s[11].blocked_by == 7);
    CHECK(d[1].queue.size() == 1 && d[1].queue[0] == 11);
}

static void t_net_second_dock()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].occ = 7;
    const NetVerdict v = net_pick(d, s, t, 12, P_UNLOAD, NetReq(), {1, 2}, 0, 0);
    CHECK(v.k == NetVerdict::GO && v.dock == 2 && d[2].resv == 12);
    const NetVerdict f = net_pick(d, s, t, 13, P_PICK, NetReq(), {1}, 2, 0);   // fixed dock 2 is busy now
    CHECK(f.k == NetVerdict::WAIT && f.dock == 2 && f.holder == 0 && f.reason == NR_WAIT_QUEUE);   // holder = occupant only; 12 holds a reservation
}

static void t_net_fifo()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].occ = 7;
    net_pick(d, s, t, 11, P_UNLOAD, NetReq(), {1}, 0, 1);
    net_pick(d, s, t, 12, P_UNLOAD, NetReq(), {1}, 0, 2);
    CHECK(s[11].ticket == 1 && s[12].ticket == 2);
    CHECK(d[1].queue.size() == 2 && d[1].queue[0] == 11);
    d[1].occ = 0;                                                    // dock freed
    const NetVerdict nw = net_pick(d, s, t, 13, P_UNLOAD, NetReq(), {1}, 0, 3);  // newcomer must not jump the queue
    CHECK(nw.k == NetVerdict::WAIT && nw.reason == NR_WAIT_QUEUE && s[13].ticket == 3);
    net_grant_tick(d, s, 4);
    CHECK(d[1].resv == 11 && s[11].granted && d[1].queue.size() == 2 && d[1].queue[0] == 12);
    const NetVerdict w12 = net_pick(d, s, t, 12, P_UNLOAD, NetReq(), {1}, 0, 5);
    CHECK(w12.k == NetVerdict::WAIT && w12.reason == NR_WAIT_QUEUE);
    const NetVerdict g11 = net_pick(d, s, t, 11, P_UNLOAD, NetReq(), {1}, 0, 6);
    CHECK(g11.k == NetVerdict::GO && d[1].resv == 11 && !s[11].granted);
    net_release_ship(d, s, 11);                                     // 11 done
    net_grant_tick(d, s, 7);
    CHECK(d[1].resv == 12 && s[12].granted);
}

static void t_net_go_drops_other_grant()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[2].resv = 10;                                                 // granted dock 2 earlier
    const NetVerdict v = net_pick(d, s, t, 10, P_UNLOAD, NetReq(), {1, 2}, 0, 0);
    CHECK(v.k == NetVerdict::GO && v.dock == 1);
    CHECK(d[1].resv == 10 && d[2].resv == 0);
}

static void t_net_self_occupant()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].occ = 5;                                                   // ship 5 is docked at dock 1
    const NetVerdict v = net_pick(d, s, t, 5, P_UNLOAD, NetReq(), {1}, 0, 0);
    CHECK(v.k == NetVerdict::GO && v.dock == 1 && d[1].resv == 5);
    CHECK(d[1].queue.empty() && s[5].waits_dock == 0 && s[5].ticket == 0);

    // full dock the ship sits at: not a candidate, so it waits but never queues for its own dock
    std::map<int, NetDock> d2; std::map<int, NetShip> s2; long t2 = 0;
    d2[1].occ = 7; d2[1].full = true;
    const NetVerdict w = net_pick(d2, s2, t2, 7, P_UNLOAD, NetReq(), {1}, 0, 0);
    CHECK(w.k == NetVerdict::WAIT && w.holder == 0 && w.holder != 7);
    CHECK(d2[1].queue.empty());
}

static void t_net_queue_never_occupant()
{
    // ship 9 occupies dock 1 but is still in its queue (stale): reconcile removes it and clears its wait
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].occ = 9; d[1].queue = {9, 11};
    s[9].sid = 9; s[9].wanted = {1}; s[9].ticket = 1; s[9].waits_dock = 1; s[9].blocked_by = 9;
    s[11].sid = 11; s[11].wanted = {1}; s[11].ticket = 2; s[11].waits_dock = 1; s[11].blocked_by = 9;
    net_reconcile(d, s, std::map<int, int>{{1, 9}}, std::map<int, int>{{9, 1}, {11, 0}}, std::set<int>{1});
    CHECK(d[1].queue.size() == 1 && d[1].queue[0] == 11);
    CHECK(s[9].waits_dock == 0 && s[9].ticket == 0 && s[9].blocked_by == 0);
    CHECK(s[11].blocked_by == 9);                                   // holder of 11's dock is 9, not 11
    CHECK(s[9].blocked_by != 9 && s[11].blocked_by != 11);
}

static void t_net_blocked_by_never_self()
{
    // a stored self-block (stale) is cleared by reconcile
    std::map<int, NetDock> d; std::map<int, NetShip> s;
    d[1].exists = true;
    s[11].sid = 11; s[11].blocked_by = 11; s[11].waits_dock = 0;
    net_reconcile(d, s, std::map<int, int>{}, std::map<int, int>{{11, 0}}, std::set<int>{1});
    CHECK(s[11].blocked_by == 0);
}

static void t_net_not_configured()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    const NetVerdict v = net_pick(d, s, t, 10, P_UNLOAD, NetReq(), {}, 0, 0);
    CHECK(v.k == NetVerdict::ERR && v.reason == NR_NOT_CONFIGURED);
}

static void t_net_dock_gone()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].exists = false;
    const NetVerdict v = net_pick(d, s, t, 10, P_UNLOAD, NetReq(), {1}, 0, 0);
    CHECK(v.k == NetVerdict::ERR && v.reason == NR_DOCK_GONE);
}

static void t_net_full()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].full = true;
    const NetVerdict v = net_pick(d, s, t, 10, P_UNLOAD, NetReq(), {1, 2}, 0, 0);
    CHECK(v.k == NetVerdict::GO && v.dock == 2);

    std::map<int, NetDock> d2; std::map<int, NetShip> s2;
    d2[1].full = true;
    const NetVerdict w = net_pick(d2, s2, t, 10, P_UNLOAD, NetReq(), {1}, 0, 0);
    CHECK(w.k == NetVerdict::WAIT && w.reason == NR_WAIT_FULL);
    CHECK(d2[1].queue.size() == 1);
    d2[1].full = false;                                             // space again
    net_grant_tick(d2, s2, 1);
    CHECK(d2[1].resv == 10 && s2[10].granted);
}

static void t_net_bad_holder()
{
    CHECK(net_bad_holder(HS_PARKED, true, false, 1, 0.0));     // parked: no dwell
    CHECK(!net_bad_holder(HS_PARKED, true, false, 0, 0.0));    // no waiter
    CHECK(net_bad_holder(HS_WORKING, false, false, 1, 25.0));  // empty of this dock's ores, dwell passed
    CHECK(net_bad_holder(HS_WORKING, true, true, 1, 0.0));     // dock full: no dwell
    CHECK(!net_bad_holder(HS_WORKING, true, false, 1, 500.0)); // unloading (ore left): never
    CHECK(!net_bad_holder(HS_WORKING, false, true, 0, 500.0)); // no waiter
    CHECK(!net_bad_holder(HS_WORKING, false, false, 1, 5.0));  // empty, but still in the dwell
    CHECK(!net_bad_holder(HS_WORKING, false, false, 1, NET_DWELL_S - 0.5));
    CHECK(net_bad_holder(HS_PARKED, false, false, 1, 0.0));    // halted and empty
}

static void t_unload_stalled()
{
    CHECK(!unload_stalled(true, 60, 10.0, 10, 60.0));
    CHECK(!unload_stalled(false, 0, 10.0, 10, 60.0));
    CHECK(!unload_stalled(false, 60, 9.0, 10, 60.0));
    CHECK(!unload_stalled(false, 60, 10.0, 10, 59.0));
    CHECK(unload_stalled(false, 60, 10.0, 10, 60.0));
}

static void t_net_reconcile()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s;
    d[1].resv = 5; d[1].queue = {5};
    d[2].resv = 6;
    d[3].resv = 9; d[3].occ = 8;
    s[5].sid = 5; s[5].ticket = 1; s[5].wanted = {1}; s[5].waits_dock = 1;
    s[6].sid = 6; s[6].granted = true;
    s[9].sid = 9;
    const std::map<int, int> occupant = {{3, 8}};
    const std::map<int, int> heading = {{5, 2}, {6, 0}};     // 9 and 8 are dead
    net_reconcile(d, s, occupant, heading, std::set<int>{1, 2, 3});
    CHECK(d[1].resv == 0);                                   // holder 5 heads elsewhere
    CHECK(d[2].resv == 6);                                   // granted ship keeps its grant
    CHECK(d[3].resv == 0 && d[3].occ == 0);                  // dead holder and dead occupant
    CHECK(!s.count(9) && s.count(5) && s.count(6));
    CHECK(d[1].queue.size() == 1);
    net_reconcile(d, s, occupant, heading, std::set<int>{1, 3});   // dock 2 gone
    CHECK(!d[2].exists && d[2].resv == 0);
}

static void t_net_release()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].occ = 7;
    net_pick(d, s, t, 10, P_UNLOAD, NetReq(), {1}, 0, 0);              // waits, ticket 1
    d[2].resv = 10;
    net_release_ship(d, s, 10);
    CHECK(d[1].queue.empty() && d[2].resv == 0);
    CHECK(!s.count(10));
}

static void t_net_text()
{
    CHECK(net_ship_text(NR_WAIT_BUSY, "Unload Silica 1", "Cleaner", 129, 1, 3) == "Waiting for Unload Silica 1 (2:09 occupied by Cleaner) - queue 1/3");
    CHECK(net_ship_text(NR_WAIT_BUSY, "Unload Silica 1", "Cleaner", 9, 1, 1) == "Waiting for Unload Silica 1 (0:09 occupied by Cleaner)");   // one waiter: no queue text
    CHECK(net_ship_text(NR_WAIT_QUEUE, "Unload Silica 1", "", 65, 2, 2) == "Waiting for Unload Silica 1 (1:05) - queue 2/2");
    CHECK(net_ship_text(NR_WAIT_FULL, "X", "", 0, 0, 0) == "Dock full (X), waiting for space");
    CHECK(net_ship_text(NR_WAIT_PARKED, "X", "S", 0, 0, 0) == "Dock X held by idle ship S, no second dock to leave to");
    CHECK(net_ship_text(NR_NOTHING_TO_UNLOAD, "", "", 0, 0, 0) == "Nothing to unload");
    CHECK(net_mmss(0) == "0:00" && net_mmss(59.9) == "0:59" && net_mmss(600) == "10:00" && net_mmss(-3) == "0:00");
    CHECK(net_wait_log(NR_WAIT_BUSY, "Unload Silica 1", "Cleaner", 0, false) == "Waiting for Unload Silica 1 (occupied by Cleaner)");
    CHECK(net_wait_log(NR_WAIT_BUSY, "Unload Silica 1", "Cleaner", 129, true) == "Still waiting for Unload Silica 1 (2:09 occupied by Cleaner)");
    CHECK(net_wait_log(NR_WAIT_QUEUE, "Unload Silica 1", "", 0, false) == "Waiting for Unload Silica 1");
}

static void t_net_log_gate()
{
    LogGate g;
    CHECK(log_gate(g, "a|5|busy", 100.0, 0.0, 60.0) == GATE_NEW);      // first wait line
    CHECK(log_gate(g, "a|5|busy", 101.0, 0.0, 60.0) == GATE_NONE);     // same key, every second: nothing
    CHECK(log_gate(g, "a|5|busy", 159.0, 0.0, 60.0) == GATE_NONE);
    CHECK(log_gate(g, "a|5|busy", 160.0, 0.0, 60.0) == GATE_REMIND);   // reminder once per 60 s
    CHECK(log_gate(g, "a|7|busy", 161.0, 0.0, 60.0) == GATE_NEW);      // holder changed: new line at once
    LogGate q;
    CHECK(log_gate(q, "2/3", 0.0, 10.0, 1e18) == GATE_NEW);
    CHECK(log_gate(q, "1/3", 4.0, 10.0, 1e18) == GATE_NONE);           // queue changed < 10 s after last line: later
    CHECK(log_gate(q, "1/3", 11.0, 10.0, 1e18) == GATE_NEW);           // logged once 10 s passed
    CHECK(log_gate(q, "1/3", 500.0, 10.0, 1e18) == GATE_NONE);         // no reminder for queue
}

static void t_net_score_order()
{
    NetReq full; full.cargo_pct = 98;
    NetReq low; low.cargo_pct = 10;
    NetReq fuel; fuel.cargo_pct = 50; fuel.fuel_low = true;
    NetReq plain; plain.cargo_pct = 50;
    CHECK(net_score(full, P_UNLOAD, 0, false) > net_score(low, P_UNLOAD, 500, false));   // cargo beats waiting time
    CHECK(net_score(fuel, P_UNLOAD, 0, false) > net_score(plain, P_UNLOAD, 0, false));   // fuel low breaks a cargo tie
    CHECK(net_score(plain, P_UNLOAD, 0, false) > net_score(low, P_UNLOAD, 599, false));  // any cargo step beats the longest wait
    CHECK(net_score(plain, P_UNLOAD, 300, false) > net_score(plain, P_UNLOAD, 10, false));
    CHECK(net_score(low, P_UNLOAD, 600, false) > net_score(full, P_UNLOAD, 599, false)); // aged beats everything
    CHECK(net_score(low, P_UNLOAD, 1, true) == net_score(full, P_REFUEL, 2, true));      // aged: same tier, ticket decides
    CHECK(net_prio_why(full, 0, false) == "cargo 98%" && net_prio_why(fuel, 0, false) == "cargo 50%");
    NetReq ff; ff.fuel_low = true;
    CHECK(net_prio_why(ff, 0, false) == "fuel low" && net_prio_why(plain, 0, false) == "cargo 50%");
    CHECK(net_prio_why(NetReq(), 30, false) == "waiting" && net_prio_why(NetReq(), 600, false) == "aged");
}

static void t_net_higher_cargo_wins()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    NetReq full; full.cargo_pct = 98;
    NetReq low; low.cargo_pct = 10;
    d[1].occ = 7;
    CHECK(net_pick(d, s, t, 21, P_UNLOAD, low, {1}, 0, 1).k == NetVerdict::WAIT);   // low ship waits first (ticket 1)
    CHECK(net_pick(d, s, t, 20, P_UNLOAD, full, {1}, 0, 2).k == NetVerdict::WAIT);  // full ship waits later (ticket 2)
    d[1].occ = 0;                                         // dock free, both queued
    CHECK(net_pick(d, s, t, 21, P_UNLOAD, low, {1}, 0, 3).k == NetVerdict::WAIT);   // full ship is ahead: no GO for the low ship
    CHECK(net_pick(d, s, t, 20, P_UNLOAD, full, {1}, 0, 4).k == NetVerdict::GO && d[1].resv == 20);
    net_release_ship(d, s, 20);
    net_grant_tick(d, s, 5);
    CHECK(d[1].resv == 21 && s[21].granted);              // next free dock goes to the waiting low ship
}

static void t_net_non_preempt()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    d[1].occ = 7;
    CHECK(net_pick(d, s, t, 30, P_UNLOAD, NetReq(), {1}, 0, 0).k == NetVerdict::WAIT);
    CHECK(net_pick(d, s, t, 30, P_UNLOAD, NetReq(), {1}, 0, NET_AGE_S + 1).k == NetVerdict::WAIT);   // now aged: prio 9e6
    CHECK(s[30].prio == net_score(NetReq(), P_UNLOAD, 0, true) && s[30].prio_why == "aged");
    net_grant_tick(d, s, NET_AGE_S + 2);
    CHECK(d[1].occ == 7 && d[1].resv == 0 && !s[30].granted);   // the holder is never sent away by a grant
}

static void t_net_flee_no_slot()
{
    std::map<int, NetDock> d; std::map<int, NetShip> s; long t = 0;
    const NetVerdict v = net_pick(d, s, t, 40, P_FLEE, NetReq(), {1}, 0, 0);   // dock 1 is free, but a flee takes no slot
    CHECK(v.k == NetVerdict::WAIT && v.dock == 1 && d[1].resv == 0 && d[1].queue.empty());
    CHECK(s[40].waits_dock == 1 && s[40].ticket == 0);
    net_grant_tick(d, s, 1);
    CHECK(d[1].resv == 0 && !s[40].granted);                                  // never granted
    std::map<int, NetDock> d2; std::map<int, NetShip> s2;
    d2[1].occ = 41;                                                           // a flee ship already docked here keeps its dock
    CHECK(net_pick(d2, s2, t, 41, P_FLEE, NetReq(), {1}, 0, 0).k == NetVerdict::GO && d2[1].resv == 41);
}

static void t_net_slots()
{
    NetDock nd;
    CHECK(net_slot_assign(nd, 10) == 1 && net_slot_assign(nd, 11) == 2);
    CHECK(net_slot_assign(nd, 10) == 1);                    // stable
    net_slot_free(nd, 10);
    CHECK(net_slot_assign(nd, 12) == 1);                    // freed slot reused
    CHECK(net_slot_assign(nd, 13) == 3 && nd.line_slots == 3);
}

static void t_net_slot_pos()
{
    const double east[3] = {0, 1, 0};
    double o[3];
    net_slot_pos(east, 1, o);
    CHECK(std::fabs(o[0]) < 1e-9 && std::fabs(o[1] - 150.0) < 1e-9 && std::fabs(o[2]) < 1e-9);
    net_slot_pos(east, 2, o);
    CHECK(std::fabs(o[1] - 250.0) < 1e-9);
    const double big[3] = {5, 0, 0};                        // length not 1: still metres
    net_slot_pos(big, 1, o);
    CHECK(std::fabs(o[0] - 150.0) < 1e-9);
}

static void t_net_offset_carry()
{
    const int64_t ch0[3] = {5, -2, 0};
    const double loc0[3] = {134217000.0, 100.0, 0.0};   // near the +X chunk border
    const double off[3] = {10.0, -1.0, 0.0};             // metres
    const double rpm = 65536.0;
    int64_t ch[3];
    double loc[3];
    net_offset_pos(ch0, loc0, off, rpm, ch, loc);
    CHECK(ch[0] == 6 && std::fabs(loc[0] - (134217000.0 + 10.0 * rpm - 134217728.0)) < 1e-6);   // carried into chunk x+1
    CHECK(ch[1] == -3 && std::fabs(loc[1] - (100.0 - rpm + 134217728.0)) < 1e-6);          // y goes below 0 raw: carry -1 to chunk y-3
    CHECK(loc[1] >= 0.0 && loc[1] < 134217728.0);                                            // loc always inside the chunk
    CHECK(ch[2] == 0 && std::fabs(loc[2]) < 1e-9);
}

static void t_net_line_spacing()
{
    // 8 docks 80 m apart on one row, 6 slots each: every pair of slot points must stay >= 50 m apart
    std::vector<std::array<double, 3>> pts;
    std::vector<std::array<double, 3>> docks;
    for (int i = 0; i < 8; ++i)
    {
        const std::array<double, 3> dk = {80.0 * i, 0.0, 0.0};
        double dir[3] = {1, 0, 0};
        CHECK(net_line_dir_fix(dk.data(), dir, 6, pts));
        for (int k = 1; k <= 6; ++k)
        {
            double o[3];
            net_slot_pos(dir, k, o);
            pts.push_back({dk[0] + o[0], dk[1] + o[1], dk[2] + o[2]});
        }
        docks.push_back(dk);
    }
    bool ok = true;
    for (size_t a = 0; a < pts.size(); ++a)
        for (size_t b = a + 1; b < pts.size(); ++b)
        {
            const double dx = pts[a][0] - pts[b][0], dy = pts[a][1] - pts[b][1], dz = pts[a][2] - pts[b][2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) < 50.0 - 1e-6) ok = false;
        }
    CHECK(ok);
    // a dock already clear keeps its direction
    const std::array<double, 3> far = {5000.0, 5000.0, 0.0};
    double keep[3] = {0, 1, 0};
    CHECK(net_line_dir_fix(far.data(), keep, 6, pts) && keep[0] == 0 && keep[1] == 1);
}

static void t_net_clear_expired()
{
    NetDock nd;
    nd.resv = 10; nd.since = 0;
    CHECK(!net_clear_expired(nd, 59.0) && nd.resv == 10);
    CHECK(net_clear_expired(nd, 60.0) && nd.resv == 0 && nd.since == 0);
    NetDock here;                                          // ship already docked there: never expired
    here.resv = 10; here.occ = 10; here.since = 0;
    CHECK(!net_clear_expired(here, 500.0) && here.resv == 10);
}

static void t_dock_undock_first()
{
    CHECK(!dock_undock_first(0, 1));          // not docked: no undock
    CHECK(!dock_undock_first(2, 2));          // docked at the target: continue there
    CHECK(dock_undock_first(2, 1));           // docked elsewhere: undock first
    CHECK(!dock_undock_first(1, 0));          // no target: nothing to do
}

static void t_combat_flee()
{
    CHECK(TARGET_FLEE_READS == 2);
    int s = 0;
    s = target_streak_next(s, 1); CHECK(s == 1 && !target_flee_trigger(true, s, false));   // one read: no flee yet
    s = target_streak_next(s, 0); CHECK(s == 0);                                           // a clean read resets
    s = target_streak_next(s, 3); s = target_streak_next(s, 1); CHECK(s == 2 && target_flee_trigger(true, s, false));   // two in a row: flee
    CHECK(!target_flee_trigger(true, 0, false));               // IsShipInCombat alone never flees (streak 0)
    CHECK(!target_flee_trigger(false, 5, false));              // flee_on_target = 0: never
    CHECK(!target_flee_trigger(true, 5, true));                // latched (already fled for this attack): no second flee
    bool l = true;
    CHECK(target_latch_next(l, true));                         // still attacked: stays latched
    CHECK(!target_latch_next(l, false));                       // attack over: released
    CHECK(!target_latch_next(false, true));                    // never set without a flee
}

static void t_hazard_flee()
{
    // hazard = hull drop with no hostile targeter
    CHECK(hazard_flee(0));
    CHECK(!hazard_flee(1));
    // stuck = last 3 speed samples below 1 m/s
    CHECK(speed_stuck({5.0, 0.5, 0.2, 0.1}));
    CHECK(!speed_stuck({0.1, 0.2, 5.0}));
    CHECK(!speed_stuck({0.1, 0.2}));                           // fewer than 3 samples
    CHECK(!speed_stuck({0.0, 0.0, 0.0, 0.0, 0.0, 1.0}));      // 1.0 is not below 1 m/s
    // hull rate per second over 1 s samples
    NEAR(hull_rate_per_s({100, 80, 60}), 20.0, 1e-9);
    NEAR(hull_rate_per_s({214, 174, 134, 94, 54, 14}), 40.0, 1e-9);
    CHECK(hull_rate_per_s({100}) == 0.0);
    CHECK(hull_rate_per_s({50, 60}) == 0.0);                  // a gain is not a loss
    // move-away due: 5 s after the flee, drop in the last 2 s, nothing else moving
    CHECK(!hazard_move_due(4.9, 0.0, false, false));
    CHECK(hazard_move_due(5.0, 0.0, false, false));
    CHECK(!hazard_move_due(5.0, 3.0, false, false));          // no fresh drop
    CHECK(!hazard_move_due(5.0, 0.0, true, false));           // move job running
    CHECK(!hazard_move_due(5.0, 0.0, false, true));           // dock job running
}

static void t_targett_entry()
{
    CHECK(!target_entry_hostile(true, true, false, true));    // our own bench ship
    CHECK(!target_entry_hostile(true, false, true, true));    // same owner or faction as the ship
    CHECK(target_entry_hostile(true, false, false, true));    // readable, other side
    CHECK(target_entry_hostile(false, false, false, true));   // unreadable object entry
    CHECK(!target_entry_hostile(false, false, false, false)); // unreadable, not an object type
    CHECK(clamp_tarray_num(-5) == 0);
    CHECK(clamp_tarray_num(7) == 7);
    CHECK(clamp_tarray_num(250000) == TARRAY_NUM_MAX);
    CHECK(SHIP_DEAD_SCANS == 2 && SHIP_MISSING_SCANS == 3);   // signal A stays at 2, signal B (missing from the list) needs 3
    CHECK(!dead_confirmed(2, SHIP_MISSING_SCANS) && dead_confirmed(3, SHIP_MISSING_SCANS));
    CHECK(dead_confirmed(2));
}

static void t_combat_log()
{
    CHECK(combat_log_trim("abc\n", 10) == "abc\n");            // under cap: unchanged
    const std::string big = "line1\nline2\nline3\nline4\n";     // 24 bytes, cap 10: drop the older half
    const std::string cut = combat_log_trim(big, 10);
    CHECK(cut == "line4\n");                                   // cut after the first newline past the middle (index 12)
    CHECK(combat_log_trim("nonewline_past_cap_xxxxxxxxxxxxxxxxxxxxxx", 10).empty());   // no newline past the middle: nothing kept
    CHECK(attacker_text(1, L"DMSPlayerShipObject", 42, L"Raider", L"Pirates", 1234.7, false) ==
          L"#1 DMSPlayerShipObject id=42 name='Raider' owner='Pirates' dist=1234m");
    CHECK(attacker_text(2, L"Rock", 7, L"", L"n/a", -1.0, true) == L"#2 Rock id=7 name='' owner='n/a' dist=n/a own");
}

static void t_net_unload_first()
{
    CHECK(net_unload_first(true, P_REFUEL));
    CHECK(!net_unload_first(true, P_UNLOAD));
    CHECK(!net_unload_first(false, P_REFUEL));
    CHECK(!net_unload_first(true, P_UNDOCK_TARGET));
}

static void t_trip_progress()
{
    double b = -1.0;
    CHECK(trip_progress(b, 10000.0));
    CHECK(!trip_progress(b, 9700.0));
    CHECK(trip_progress(b, 9500.0));
    CHECK(b == 9500.0);
    CHECK(!trip_progress(b, 0.0));
    CHECK(!trip_progress(b, 9800.0));
    CHECK(trip_warp_wanted(5001.0, 5));
    CHECK(!trip_warp_wanted(5000.0, 5));
    CHECK(!trip_warp_wanted(0.0, 5));
    CHECK(!trip_warp_wanted(9000.0, 0));
    CHECK(!trip_warp_wanted(9000.0, -3));
    CHECK(trip_same_sector(1008, 1008));
    CHECK(!trip_same_sector(1008, 1247));
    CHECK(!trip_same_sector(0, 0));
    CHECK(!trip_same_sector(1008, 0));
}

static void t_ob_dt()
{
    CHECK(ob_dt(0.25, false) == 0.25);
    CHECK(ob_dt(0.25, true) == 0.0);
    double t = 0; for (int i = 0; i < 100; ++i) t += ob_dt(0.1, true);
    CHECK(t == 0.0);
    for (int i = 0; i < 60; ++i) t += ob_dt(0.1, false);
    CHECK(t >= 5.0);
}

// clear-path geometry: segment vs sphere (in front, behind, beyond the end, tangent)
static void t_path_block()
{
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    const double front[3] = {500, 100, 0}, beyond[3] = {1500, 0, 0};
    NEAR(seg_point_dist(a, b, beyond), 500.0, 1e-9);           // clamped: distance to the end point
    NEAR(seg_point_dist(a, b, front), 100.0, 1e-9);
}

// waypoint and stand-off geometry
static void t_path_waypoint()
{
    const double a[3] = {0, 0, 0};
    // stand-off: from the rock centre toward the ship, at the given distance
    const double rc[3] = {1000, 0, 0};
    double so[3] = {0, 0, 0};
    standoff_point(a, rc, 200.0, so);
    NEAR(so[0], 800.0, 1e-9); NEAR(so[1], 0.0, 1e-9); NEAR(so[2], 0.0, 1e-9);
}

// flee hold distance from base size, flee move kind (real warp vs flight), slot offset length
static void t_flee_real_warp_hold()
{
    CHECK(station_extent_m({}, 4.0) < 0.0);
    CHECK(std::fabs(station_extent_m({{3, 4, 0}, {0, 0, 0}}, 4.0) - 20.0) < 1e-9);
    CHECK(std::fabs(station_extent_m({{0, 0, 0}, {300, 400, 0}}, 4.0) - 2000.0) < 1e-9);
    CHECK(flee_hold_dist_m(20.0, {}) == 1000.0);
    CHECK(flee_hold_dist_m(2000.0, {}) == 2300.0);
    CHECK(flee_hold_dist_m(50000.0, {}) == 6000.0);
    CHECK(flee_hold_dist_m(-1.0, {900.0}) == 1200.0);
    CHECK(flee_hold_dist_m(-1.0, {5000.0}) == 1000.0);
    CHECK(flee_move_kind(true, 239000.0, 5, true, false, true, false) == FLEE_REAL);
    CHECK(flee_move_kind(true, 3000.0, 5, true, false, true, false) == FLEE_FLY);
    CHECK(flee_move_kind(true, 239000.0, 5, false, false, true, false) == FLEE_FLY);
    CHECK(flee_move_kind(true, 239000.0, 5, true, true, true, false) == FLEE_FLY);
    CHECK(flee_move_kind(true, 239000.0, 5, true, false, false, false) == FLEE_FLY);
    CHECK(flee_move_kind(true, 239000.0, 5, true, false, true, true) == FLEE_FLY);
    CHECK(flee_move_kind(false, 0.0, 5, true, false, true, false) == FLEE_REAL);
    CHECK(flee_move_kind(true, 239000.0, 0, true, false, true, false) == FLEE_FLY);
    const double dir[3] = {0.34, -0.94, 0.0};
    double off1[3] = {0, 0, 0}, off2[3] = {0, 0, 0};
    net_slot_pos(dir, 1, off1);
    net_slot_pos(dir, 2, off2, 1000.0);
    CHECK(std::fabs(std::sqrt(off1[0] * off1[0] + off1[1] * off1[1] + off1[2] * off1[2]) - 150.0) < 1e-6);
    CHECK(std::fabs(std::sqrt(off2[0] * off2[0] + off2[1] * off2[1] + off2[2] * off2[2]) - 1100.0) < 1e-6);
}

// idle flee: start a move-away only when no holding spot, not holding, retry wait over, no move or dock job
static void t_flee_idle_move()
{
    CHECK(flee_idle_move_due(false, 0, 1.0, false, false));
    CHECK(!flee_idle_move_due(true, 0, 1.0, false, false));
    CHECK(!flee_idle_move_due(false, 2, 5.0, false, false));
    CHECK(!flee_idle_move_due(false, 0, 0.4, false, false));
    CHECK(!flee_idle_move_due(false, 0, 2.0, true, false));
    CHECK(!flee_idle_move_due(false, 0, 2.0, false, true));
}

// attacker distance from chunk + raw loc (65536 raw per m, 2048 m chunks); hit class needs any targeter or combat
static void t_attack_dist_and_flee_class()
{
    const int64_t c0[3] = {0, 240, 0}, c1[3] = {0, 241, 0};
    const double a[3] = {0, 0, 0}, b[3] = {20671200.0, 0, 0};
    const double d = abs_dist_m(c0, a, c0, b, 65536.0, 2048.0);
    CHECK(std::fabs(d - 315.4) < 0.1);
    CHECK(d <= 5000.0);
    CHECK(std::fabs(abs_dist_m(c0, a, c1, a, 65536.0, 2048.0) - 2048.0) < 1e-6);
    CHECK(target_entry_near(d, 5000.0));
    CHECK(!target_entry_near(60000.0, 5000.0));
    CHECK(!hazard_flee_full(1, 0, false));
    CHECK(!hazard_flee_full(0, 3, false));
    CHECK(!hazard_flee_full(0, 0, true));
    CHECK(hazard_flee_full(0, 0, false));
}

static void t_safety_net()
{
    CHECK(safety_allowed({}, 100.0));
    CHECK(safety_allowed({10.0, 20.0}, 100.0));
    CHECK(!safety_allowed({10.0, 20.0, 30.0}, 100.0));
    CHECK(safety_allowed({10.0, 20.0, 30.0}, 400.0));   // all older than 300 s
    CHECK(!safety_restart_due(9.9));
    CHECK(safety_restart_due(10.0));
}

// GetShields double -> whole units; a shield drop from doubles triggers DMG_SHIELD
static void t_flee_guard_shield_units()
{
    CHECK(shield_whole(-1.0) == -1);
    CHECK(shield_whole(0.0) == 0);
    CHECK(shield_whole(59.9) == 59);
    CHECK(shield_whole(60.0) == 60);
    CHECK(shield_whole(1e12) == 2147483647);
    CHECK(shield_whole(std::nan("")) == -1);
    CHECK(!shield_dropped(shield_whole(59.2), shield_whole(59.9)));   // regeneration inside one unit
    CHECK(damage_drop_kind(500, 500, shield_whole(40.0), shield_whole(25.5), false) == DMG_SHIELD);
}

// flee dock list: cargo candidates win; empty cargo falls back to ore docks, then refuel docks, unique
static void t_flee_fallback_docks()
{
    const std::vector<int> r1 = flee_fallback_docks({5}, {{7}, {8}}, {9});
    CHECK(r1 == std::vector<int>({5}));
    const std::vector<int> r2 = flee_fallback_docks({}, {{7, 8}, {8, 6}}, {9, 7});
    CHECK(r2 == std::vector<int>({7, 8, 6, 9}));
    const std::vector<int> r3 = flee_fallback_docks({}, {{}, {}}, {});
    CHECK(r3.empty());
    const std::vector<int> r4 = flee_fallback_docks({}, {{}, {}}, {3});
    CHECK(r4 == std::vector<int>({3}));
}

// shield and hull drop with regeneration: a rise is never a drop; the baseline follows each sample
static void t_shield_drop()
{
    CHECK(!shield_dropped(-1, 50));            // no baseline yet
    CHECK(!shield_dropped(50, -1));            // unreadable shield
    CHECK(!shield_dropped(50, 60));            // regeneration
    CHECK(shield_dropped(60, 55));             // loss measured from the regenerated value
    CHECK(!shield_dropped(60, 60));
    CHECK(damage_drop_kind(350, 320, 40, 0, false) == DMG_HULL);
    CHECK(damage_drop_kind(350, 350, 40, 20, false) == DMG_SHIELD);
    CHECK(damage_drop_kind(350, 350, 40, 20, true) == DMG_NONE);   // docked: ignored
    CHECK(damage_drop_kind(350, 350, -1, -1, false) == DMG_NONE);
    CHECK(damage_drop_kind(350, 360, 40, 60, false) == DMG_NONE);  // both rose
    int drops = 0;
    int32_t base = 50;
    const int32_t seq[] = {60, 70, 55, 55, 80};
    for (int32_t s : seq) { if (shield_dropped(base, s)) ++drops; base = s; }
    CHECK(drops == 1);
}

// fast sample period: a rock within SLOW_ZONE_M (500 m, surface) of the ship
static void t_fast_sample()
{
    CHECK(fast_sample_due(0.0));
    CHECK(fast_sample_due(500.0));
    CHECK(!fast_sample_due(500.5));
    CHECK(!fast_sample_due(1500.0));
    NEAR(FAST_SAMPLE_S, 0.25, 1e-12);
}

// perf helpers: box prefilter, per-check budget, slow-check log/skip timing, snapshot rebuild
static void t_path_perf()
{
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    const double near[3] = {500, 4000, 0}, far[3] = {500, 9000, 0}, past[3] = {-6000, 0, 0};
    CHECK(!path_box_reject(a, b, near, PATH_RMAX_M));   // 4 km off the path: inside the 5 km box
    CHECK(path_box_reject(a, b, far, PATH_RMAX_M));     // 9 km off: rejected
    CHECK(path_box_reject(a, b, past, PATH_RMAX_M));    // 6 km before the start
    // slow check: over 3 ms only
    CHECK(!path_over_budget(3.0)); CHECK(path_over_budget(3.01));
    // slow log: first time always, then every 30 s
    CHECK(path_slow_log_due(0, 1000));
    CHECK(!path_slow_log_due(10000, 39999));
    CHECK(path_slow_log_due(10000, 40000));
    // slow ship skips its periodic checks for 10 s
    const uint64_t until = path_skip_until(50000);
    CHECK(until == 60000);
    CHECK(path_skip_active(until, 59999));
    CHECK(!path_skip_active(until, 60000));
    // periodic check due at 2 s of approach
    CHECK(!path_watch_due(1.99)); CHECK(path_watch_due(2.0));
    NEAR(PATH_SLOW_MS, 3.0, 1e-12);
    CHECK(PATH_CANDIDATES == 8);
}

// approach Throttle (approach_throttle, mb_logic.hpp): full speed beyond APPROACH_RAMP_M, linear to 1 inside, 0 at or past the stand-off point
static void t_approach_throttle()
{
    CHECK(approach_throttle(5000.0, 40.0, 10) == 10);
    CHECK(approach_throttle(500.0, 40.0, 10) == 10);
    CHECK(approach_throttle(250.0, 40.0, 10) == 5);
    CHECK(approach_throttle(250.0, 40.0, 4) == 2);
    CHECK(approach_throttle(1.0, 0.0, 10) == 1);         // ceil: the last metres still move
    CHECK(approach_throttle(0.0, 0.0, 10) == 0);
    CHECK(approach_throttle(-3.0, 0.0, 10) == 0);
    CHECK(approach_throttle(250.0, 40.0, 0) == 0);
    CHECK(approach_throttle(300.0, 99.0, 10) == approach_throttle(300.0, 0.0, 10));   // speed does not change the ramp
    int prev = 0;
    bool mono = true;
    for (double d = 1.0; d <= 600.0; d += 1.0)   // never falls as the ship gets farther, always 0..max
    {
        const int t = approach_throttle(d, 0.0, 10);
        if (t < prev || t < 0 || t > 10) mono = false;
        prev = t;
    }
    CHECK(mono);
}

// bubble geometry (mb_logic.hpp): look-ahead, clearance
static void t_bubble()
{
    CHECK(bubble_look_ahead(0.0) == 100.0);
    CHECK(bubble_look_ahead(20.0) == 100.0);               // 80 m < minimum
    CHECK(bubble_look_ahead(50.0) == 200.0);               // 4 s x 50 m/s
    CHECK(bubble_look_ahead(-5.0) == 100.0);
    CHECK(bubble_clear_m(12.0, 30.0) == 42.0);             // ship radius + 30 m clearance
}

// bubble ladder (bubble_step): brake on a hit, replan after the stop, give up after BUBBLE_MAX_REPLAN replans
static void t_bubble_ladder()
{
    BubbleState s;
    CHECK(bubble_step(s, false, 50.0, 5) == BBA_NONE);
    CHECK(bubble_step(s, true, 50.0, 5) == BBA_BRAKE && s.braking && s.pending == BBA_REPLAN);
    CHECK(bubble_step(s, true, 10.0, 5) == BBA_NONE);      // still braking: no action until stopped
    CHECK(bubble_step(s, false, 2.0, 5) == BBA_REPLAN && !s.braking && s.replans == 1);
    CHECK(bubble_step(s, true, 40.0, 5) == BBA_BRAKE && s.pending == BBA_REPLAN);
    CHECK(bubble_step(s, false, 0.5, 5) == BBA_REPLAN && s.replans == 2);
    CHECK(bubble_step(s, true, 40.0, 5) == BBA_BRAKE && s.pending == BBA_RETARGET);   // 2 replans done: give up
    CHECK(bubble_step(s, false, 1.0, 5) == BBA_RETARGET && s.replans == 0 && !s.braking);
    CHECK(bubble_step(s, true, 40.0, 6) == BBA_BRAKE && s.pending == BBA_REPLAN);      // new target: fresh count
    CHECK(bubble_step(s, true, 40.0, 6) == BBA_NONE);
}

// bubble scope (stage 2b): activation rule, phase per move job, fix per phase
static void t_bubble_scope()
{
    // activation: game autopilot always wins; our thrust job or speed > 3 m/s is active; else idle
    CHECK(bubble_why(true, false, false, true, 50.0) == BW_AUTOPILOT);
    CHECK(bubble_why(false, true, false, true, 50.0) == BW_DOCK_AUTOPILOT);
    CHECK(bubble_why(false, false, true, true, 50.0) == BW_TRAVEL);
    CHECK(bubble_why(false, false, false, true, 0.0) == BW_ACTIVE);      // our approach job, ship still
    CHECK(bubble_why(false, false, false, false, 5.0) == BW_ACTIVE);     // drifting, no job
    CHECK(bubble_why(false, false, false, false, 2.0) == BW_IDLE);       // not moving, no job
    CHECK(bubble_why(false, false, false, false, 3.0) == BW_IDLE);       // at the threshold: not moving
    CHECK(bubble_why_name(BW_AUTOPILOT)[0] != 0 && bubble_why_name(BW_IDLE)[0] != 0);
    // phase from the move job flags
    CHECK(bubble_phase(true, false, false, false) == BPH_SEEK);
    CHECK(bubble_phase(true, true, false, false) == BPH_WAYPOINT);
    CHECK(bubble_phase(true, false, true, false) == BPH_ORE);
    CHECK(bubble_phase(true, true, true, false) == BPH_ORE);             // ore flight wins over point
    CHECK(bubble_phase(false, false, false, true) == BPH_MINE);          // drift
    CHECK(bubble_phase(false, false, false, false) == BPH_NONE);
    CHECK(std::string(bubble_phase_name(BPH_ORE)) == "ore");
    // fix per phase: SEEK replans (skip on retarget); waypoint/ore: waypoint while wp < 2, then skip; mine: nudge when near, else resume
    CHECK(bubble_fix(BPH_SEEK, BBA_REPLAN, 0, false) == BF_REPLAN);
    CHECK(bubble_fix(BPH_SEEK, BBA_RETARGET, 0, false) == BF_SKIP);
    CHECK(bubble_fix(BPH_WAYPOINT, BBA_REPLAN, 0, false) == BF_WAYPOINT);
    CHECK(bubble_fix(BPH_WAYPOINT, BBA_REPLAN, 1, false) == BF_WAYPOINT);
    CHECK(bubble_fix(BPH_WAYPOINT, BBA_REPLAN, 2, false) == BF_SKIP);   // BUBBLE_MAX_WP = 2 used
    CHECK(bubble_fix(BPH_ORE, BBA_REPLAN, 2, true) == BF_SKIP);
    CHECK(bubble_fix(BPH_MINE, BBA_REPLAN, 0, true) == BF_NUDGE);
    CHECK(bubble_fix(BPH_MINE, BBA_REPLAN, 0, false) == BF_RESUME);
    CHECK(std::string(bubble_fix_name(BF_NUDGE)) == "nudge" && std::string(bubble_fix_name(BF_WAYPOINT)) == "waypoint");
    // too near: centre distance below ship radius + rock radius + 10 m
    CHECK(bubble_too_near(29.0, 12.0, 8.0));
    CHECK(!bubble_too_near(30.0, 12.0, 8.0));
    CHECK(BUBBLE_NUDGE_M == 100.0 && BUBBLE_MAX_WP == 2);
    // state keeps the flight count and the resume destination
    BubbleState s;
    s.wp = 1; s.resume = true; s.dst[0] = 5.0;
    CHECK(s.wp == 1 && s.resume && s.dst[0] == 5.0);
}

// path2: replan cooldown and wp gate, rock-lost counting, progress, start grace, strikes, bubble closing
static void t_path2_rules()
{
    // rock-lost counting: only a really gone rock, not our own cancels, not a rock still there
    CHECK(rock_lost_next(2, true, true, false) == 3);
    CHECK(rock_lost_next(2, true, false, false) == 2);              // rock still there: free retry
    CHECK(rock_lost_next(2, true, true, true) == 2);                // own cancel
    CHECK(rock_lost_next(2, false, true, false) == 2);              // not a rock-lost result
    CHECK(!lost_progress(50.0) && lost_progress(50.5));              // progress must be more than 50 m
    // bubble: brake only ahead and closing
    CHECK(!bubble_brake_ok(-0.2, 100.0, 90.0, true, 30.0));         // behind
    CHECK(!bubble_brake_ok(0.0, 100.0, 90.0, true, 30.0));          // beside
    CHECK(bubble_brake_ok(0.9, 100.0, 110.0, true, 30.0));          // ahead and closing (range fell 10 m)
    CHECK(!bubble_brake_ok(0.9, 100.0, 90.0, true, 30.0));          // ahead but opening
    CHECK(bubble_brake_ok(0.9, 100.0, 0.0, false, 30.0));           // first sight, moving
    CHECK(!bubble_brake_ok(0.9, 100.0, 0.0, false, 0.0));           // first sight, not moving
}

// sweep return arrival (sweep_arrive) and flee target range (target_entry_near)
static void t_sweep_return_flee()
{
    CHECK(sweep_arrive(1.0, 500.0, true) == SA_WAIT);      // inside 1000 m but before the 2 s settle minimum
    CHECK(sweep_arrive(3.0, 500.0, true) == SA_ARRIVED);
    CHECK(sweep_arrive(2.0, 1000.0, true) == SA_ARRIVED);  // boundary: 1000 m counts
    CHECK(sweep_arrive(3.0, 1000.5, true) == SA_WAIT);     // 1000.5 m: not yet
    CHECK(sweep_arrive(3.0, 500.0, false) == SA_WAIT);     // jumping: system 0, still travelling
    CHECK(sweep_arrive(5.0, -1.0, true) == SA_WAIT);       // position unreadable while jumping
    CHECK(sweep_arrive(29.9, 5000.0, false) == SA_WAIT);
    CHECK(sweep_arrive(30.0, 5000.0, false) == SA_GIVE_UP); // 30 s cap, not arrived
    CHECK(sweep_arrive(30.0, -1.0, true) == SA_GIVE_UP);
    // flee range: a targeter within the range (or unreadable distance) counts, a farther one does not
    CHECK(target_entry_near(4999.0, 5000.0));
    CHECK(target_entry_near(5000.0, 5000.0));
    CHECK(!target_entry_near(5001.0, 5000.0));
    CHECK(!target_entry_near(70000.0, 5000.0));            // 70 km: far
    CHECK(target_entry_near(-1.0, 5000.0));                // unreadable distance counts as near
    CHECK(target_entry_near(-1.0, 1000.0));
    CHECK(!target_entry_near(1500.0, 1000.0));              // 1 km range: 1.5 km is far
}

// far-trip warp for our flights: decision order, landing candidates, sweep reachability
static void t_flight_warp()
{
    // 10 km, warp_trip_km 5, same system and sector, 100 warp items in the drive: warp
    CHECK(flight_warp_why(10000.0, 5, true, 3, 3, false, false, false, false, 100, 4.88) == FW_WARP);
    CHECK(flight_warp_why(4000.0, 5, true, 3, 3, false, false, false, false, 100, 1.95) == FW_NEAR);   // inside the trip distance
    CHECK(flight_warp_why(10000.0, 0, true, 3, 3, false, false, false, false, 100, 4.88) == FW_OFF);   // km 0: warp off
    CHECK(flight_warp_why(10000.0, 5, true, 3, 3, false, false, true, false, 100, 4.88) == FW_AUTOPILOT);   // game autopilot (or docking, passed as one)
    CHECK(flight_warp_why(10000.0, 5, true, 3, 3, false, true, false, false, 100, 4.88) == FW_TRAVEL);
    CHECK(flight_warp_why(10000.0, 5, true, 3, 3, true, false, false, false, 100, 4.88) == FW_DOCKED);
    CHECK(flight_warp_why(10000.0, 5, true, 3, 3, false, false, false, true, 100, 4.88) == FW_PILOTED);
    CHECK(flight_warp_why(10000.0, 5, false, 3, 3, false, false, false, false, 100, 4.88) == FW_OTHER_SYSTEM);
    CHECK(flight_warp_why(10000.0, 5, true, 0, 3, false, false, false, false, 100, 4.88) == FW_WARP);   // one side unknown: allowed
    CHECK(flight_warp_why(10000.0, 5, true, 3, 0, false, false, false, false, 100, 4.88) == FW_WARP);
    CHECK(flight_warp_why(10000.0, 5, true, 0, 0, false, false, false, false, 100, 4.88) == FW_WARP);   // both unknown: allowed
    CHECK(flight_warp_why(10000.0, 5, true, 3, 3, false, false, false, false, 100, 4.88) == FW_WARP);   // same known sector
    CHECK(!sectors_conflict(0, 3) && !sectors_conflict(3, 0) && !sectors_conflict(0, 0) && !sectors_conflict(3, 3));
    CHECK(sectors_conflict(3, 4));   // both known, different
    CHECK(flight_warp_why(10000.0, 5, true, 3, 4, false, false, false, false, 100, 4.88) == FW_OTHER_SECTOR);
    CHECK(flight_warp_why(10000.0, 5, true, 3, 3, false, false, false, false, 0, 4.88) == FW_NO_FUEL);
    CHECK(flight_unreachable(FW_OTHER_SECTOR) && flight_unreachable(FW_OTHER_SYSTEM));
    CHECK(!flight_unreachable(FW_NO_FUEL) && !flight_unreachable(FW_WARP));
    // landing candidates toward T = (5000,0,0) from the origin: base 300 m short, 600 m short, 200 m sideways both ways
    const double T[3] = {5000, 0, 0}, dir[3] = {1, 0, 0};
    double L[3];
    landing_candidate(0, T, dir, L); CHECK(std::fabs(L[0] - 4700) < 1e-9 && std::fabs(L[1]) < 1e-9);
    landing_candidate(1, T, dir, L); CHECK(std::fabs(L[0] - 4400) < 1e-9);
    landing_candidate(2, T, dir, L); CHECK(std::fabs(L[0] - 4700) < 1e-9 && std::fabs(L[1] + 200) < 1e-9);
    landing_candidate(3, T, dir, L); CHECK(std::fabs(L[0] - 4700) < 1e-9 && std::fabs(L[1] - 200) < 1e-9);
    CHECK(!point_blocked(20.0, 8.0, 8.0));    // 20 m from an 8 m rock with 8 m clearance: block radius 14.4
    CHECK(point_blocked(10.0, 8.0, 8.0));
    CHECK(point_blocked(30.0, 35.0, 8.0));    // big rock: block radius 36
}

// tuning (fix/0.2.3-tune): block radius, leg clearance, waypoint candidates and pick, retarget score, 60 s approach cap
static void t_tune_geom()
{
    // retarget score: a blocked candidate loses to a clear one unless it is 1500 m nearer
    CHECK(std::fabs(pick_score(100.0, false) - 100.0) < 1e-9);
    CHECK(pick_score(500.0, true) > pick_score(1000.0, false));
    CHECK(pick_score(2000.0, true) > pick_score(1000.0, false));
    // 60 s cap from the first path event
    CHECK(!appr_capped(0, 999999));                                  // no path event yet
    CHECK(!appr_capped(10000, 69999));
    CHECK(appr_capped(10000, 70000));
    CHECK(!appr_capped(70000, 80000) && appr_capped(70000, 130000));
}

static void t_reg_abs_m()
{
    // chunk (1, 0, -1) + loc raw (65536, 0, 131072) at 65536 raw per m = (2048 + 1, 0, -2048 + 2) m
    const int64_t ch[3] = {1, 0, -1};
    const double loc[3] = {65536.0, 0.0, 131072.0};
    double c[3];
    reg_abs_m(ch, loc, 65536.0, c);
    NEAR(c[0], 2049.0, 1e-9);
    NEAR(c[1], 0.0, 1e-9);
    NEAR(c[2], -2046.0, 1e-9);
    RegRock r;   // radius not read: conservative default; read: the radius (never negative)
    NEAR(reg_eff_rad(r), REG_RAD_UNKNOWN_M, 1e-12);
    r.rad_known = true; r.rad_m = 12.5;
    NEAR(reg_eff_rad(r), 12.5, 1e-12);
    r.rad_m = -3.0;
    NEAR(reg_eff_rad(r), 0.0, 1e-12);
}

// ---------- box probe and reg_check parsers (mb_logic.hpp, mb_registry_logic.hpp) ----------
static void t_box_probe_parse()
{
    int sid = 0, mode = -1;
    double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    CHECK(probe_parse_box("probe_collision_box:41529:-50:-50:-50:50:50:50", sid, lo, hi, mode));
    CHECK(sid == 41529 && mode == PROBE_BOX_LOCAL && lo[0] == -50.0 && hi[2] == 50.0);
    CHECK(probe_parse_box("probe_collision_box:7:0:0:0:1:2:3:world", sid, lo, hi, mode) && mode == PROBE_BOX_WORLD && hi[1] == 2.0);
    CHECK(probe_parse_box("probe_collision_box:7:0:0:0:1:2:3:raw", sid, lo, hi, mode) && mode == PROBE_BOX_RAW);
    CHECK(probe_parse_box("probe_collision_box:7:0:0:0:1:2:3:local", sid, lo, hi, mode) && mode == PROBE_BOX_LOCAL);
    CHECK(!probe_parse_box("probe_collision_box:7:0:0:0:1:2:3:bogus", sid, lo, hi, mode));
    CHECK(!probe_parse_box("probe_collision_box:7:0:0:0:1:2", sid, lo, hi, mode));        // 6 numbers
    CHECK(!probe_parse_box("probe_collision_box:0:0:0:0:1:2:3", sid, lo, hi, mode));      // ship id
    CHECK(!probe_parse_box("probe_collision_box:x:0:0:0:1:2:3", sid, lo, hi, mode));
    CHECK(!probe_parse_box("probe_collision_box:7:5:0:0:1:2:3", sid, lo, hi, mode));      // min > max on x
    CHECK(!probe_parse_box("probe_collision_box:7:0:0:0:nan:2:3", sid, lo, hi, mode));
    CHECK(!probe_parse_box("probe_collision_box:7::0:0:1:2:3", sid, lo, hi, mode));       // empty number
    CHECK(!probe_parse_box("probe_collision_boxwatch:7:5", sid, lo, hi, mode));           // other command
    double secs = 0.0;
    CHECK(probe_parse_boxwatch("probe_collision_boxwatch:41529:30", sid, secs, mode) && sid == 41529 && secs == 30.0 && mode == PROBE_BOX_LOCAL);
    CHECK(probe_parse_boxwatch("probe_collision_boxwatch:7:20:world", sid, secs, mode) && mode == PROBE_BOX_WORLD && secs == 20.0);
    CHECK(probe_parse_boxwatch("probe_collision_boxwatch:7:500", sid, secs, mode) && secs == PROBE_WATCH_MAX_S);   // clamped
    CHECK(!probe_parse_boxwatch("probe_collision_boxwatch:7:20:raw", sid, secs, mode));   // raw refused
    CHECK(!probe_parse_boxwatch("probe_collision_boxwatch:7", sid, secs, mode));
    CHECK(!probe_parse_boxwatch("probe_collision_boxwatch:7:0", sid, secs, mode));
    CHECK(!probe_parse_boxwatch("probe_collision_boxwatch:7:x", sid, secs, mode));
    CHECK(probe_split("a::b").size() == 3 && probe_split("").size() == 1);
}

static void t_box_probe_uu_fwd()
{
    const double m_lo[3] = {1, 2, 3}, m_hi[3] = {4, 5, 6}, ship[3] = {1000, 0, -200};
    double lo[3], hi[3];
    probe_box_uu(PROBE_BOX_LOCAL, m_lo, m_hi, ship, lo, hi);
    NEAR(lo[0], 100.0, 1e-9); NEAR(lo[1], 200.0, 1e-9); NEAR(lo[2], 300.0, 1e-9); NEAR(hi[2], 600.0, 1e-9);
    probe_box_uu(PROBE_BOX_WORLD, m_lo, m_hi, ship, lo, hi);
    NEAR(lo[0], 1100.0, 1e-9); NEAR(lo[2], 100.0, 1e-9); NEAR(hi[0], 1400.0, 1e-9);
    probe_box_uu(PROBE_BOX_RAW, m_lo, m_hi, ship, lo, hi);
    NEAR(lo[0], 1.0, 1e-9); NEAR(hi[2], 6.0, 1e-9);   // typed numbers as they are
    NEAR(probe_box_fwd_m(0), 25.0, 1e-9);
    NEAR(probe_box_fwd_m(3), 25.0, 1e-9);             // 15 m, floored to 25
    NEAR(probe_box_fwd_m(10), 50.0, 1e-9);
    NEAR(probe_box_fwd_m(20), 100.0, 1e-9);
    double al[3], ah[3], fl[3], fh[3];
    probe_watch_boxes(10, al, ah, fl, fh);
    NEAR(al[0], -50.0, 1e-9); NEAR(ah[2], 50.0, 1e-9);
    NEAR(fl[0], 0.0, 1e-9); NEAR(fh[0], 50.0, 1e-9); NEAR(fl[1], -15.0, 1e-9); NEAR(fh[2], 15.0, 1e-9);
}

static void t_reg_check_summary()
{
    int sid = 0;
    size_t n = 0;
    CHECK(reg_check_parse("reg_check:207537", sid, n) && sid == 207537 && n == REG_CHECK_DEF_N);
    CHECK(reg_check_parse("reg_check:207537:5", sid, n) && n == 5);
    CHECK(reg_check_parse("reg_check:207537:500", sid, n) && n == REG_CHECK_MAX_N);   // capped
    CHECK(!reg_check_parse("reg_check:0", sid, n));
    CHECK(!reg_check_parse("reg_check:x", sid, n));
    CHECK(!reg_check_parse("reg_check:5:0", sid, n));
    CHECK(!reg_check_parse("reg_check:5:-1", sid, n));
    CHECK(!reg_check_parse("reg_check:5:3:4", sid, n));
    CHECK(!reg_check_parse("reg_check:", sid, n));
}

// ---------- runner ----------

struct Case { const char* name; void (*fn)(); };

static void t_watchdog()
{
    CHECK(!wd_is_stalled(9.9));
    CHECK(wd_is_stalled(10.0));
    CHECK(wd_is_stalled(120.0));
    CHECK(wd_line_due(0, true, 0.0));          // first line of an episode is due at once
    CHECK(!wd_line_due(1, false, 29.9));       // repeat waits 30 s
    CHECK(wd_line_due(1, false, 30.0));
    CHECK(!wd_line_due(WD_MAX_LINES, true, 999.0)); // hard cap
    CHECK(!wd_line_due(WD_MAX_LINES, false, 999.0));
    CHECK(wd_line_due(WD_MAX_LINES - 1, false, 30.0));
}

static void t_probe()
{
    int sid = 0;
    double secs = 0.0;
    CHECK(probe_parse_watch("probe_avoid_watch:41529:30", sid, secs) && sid == 41529 && secs == 30.0);
    CHECK(probe_parse_watch("probe_avoid_watch:7:500", sid, secs) && sid == 7 && secs == 120.0);   // clamped
    CHECK(!probe_parse_watch("probe_avoid_watch:7", sid, secs));
    CHECK(!probe_parse_watch("probe_avoid_watch:x:5", sid, secs));
    CHECK(!probe_parse_watch("probe_avoid_watch:7:0", sid, secs));
    CHECK(!probe_parse_watch("probe_avoid_watch:7:-3", sid, secs));
    CHECK(!probe_parse_watch("probe_avoid", sid, secs));
    CHECK(probe_parse_sid("probe_avoid:41529", sid) && sid == 41529);
    CHECK(!probe_parse_sid("probe_avoid", sid));
    CHECK(!probe_parse_sid("probe_avoid:", sid));
    CHECK(probe_watch_more(0, 0.0, 30.0));
    CHECK(!probe_watch_more(PROBE_WATCH_MAX_LINES, 0.0, 30.0));   // line cap
    CHECK(!probe_watch_more(PROBE_WATCH_MAX_LINES - 1, 0.0, 30.0)); // last line kept for the end line
    CHECK(probe_watch_more(PROBE_WATCH_MAX_LINES - 2, 0.0, 30.0));
    CHECK(!probe_watch_more(10, 30.0, 30.0));                     // time asked for
    CHECK(probe_name_match(L"bAvoidanceEnabled"));
    CHECK(probe_name_match(L"WorldImpulse"));
    CHECK(probe_name_match(L"bAutopilot"));
    CHECK(!probe_name_match(L"Throttle"));
}

static void t_probe_collision()
{
    CHECK(probe_m_to_q16(1.0) == 65536);
    CHECK(probe_m_to_q16(-0.5) == -32768);
    CHECK(probe_m_to_q16(2.5) == 163840);
    CHECK(probe_m_to_q16(1.0 / 65536.0) == 1);
    CHECK(probe_m_to_q16(0.0) == 0);
    NEAR(probe_m_to_uu(1.25), 125.0, 1e-9);
    CHECK(probe_fwd_m(0) == 5.0);
    CHECK(probe_fwd_m(-3) == 5.0);
    CHECK(probe_fwd_m(1) == 5.0);
    CHECK(probe_fwd_m(3) == 15.0);
    CHECK(probe_fwd_m(10) == 50.0);
    const uint8_t bytes[3] = {0x00, 0xff, 0x0a};
    CHECK(probe_hex(bytes, 3) == L"00 ff 0a");
    CHECK(probe_hex(bytes, 0).empty());
    int sid = 0;
    double v[3] = {0.0, 0.0, 0.0};
    CHECK(probe_parse_call("probe_collision_call:41529:1.5:-2:20", sid, v) && sid == 41529 && v[0] == 1.5 && v[1] == -2.0 && v[2] == 20.0);
    CHECK(probe_parse_call("probe_collision_call:41529:1e2:0:0", sid, v) && v[0] == 100.0);
    CHECK(!probe_parse_call("probe_collision_call:41529:1:2", sid, v));
    CHECK(!probe_parse_call("probe_collision_call:41529:1:2:3:4", sid, v));
    CHECK(!probe_parse_call("probe_collision_call:0:1:2:3", sid, v));
    CHECK(!probe_parse_call("probe_collision_call:41529:x:2:3", sid, v));
    CHECK(!probe_parse_call("probe_collision_call:41529::2:3", sid, v));
    CHECK(!probe_parse_call("probe_collision_call:41529:1:2:nan", sid, v));
    CHECK(!probe_parse_call("probe_avoid:41529:1:2:3", sid, v));
    double secs = 0.0;
    CHECK(probe_parse_cwatch("probe_collision_watch:41529:5", sid, secs) && sid == 41529 && secs == 5.0);
    CHECK(probe_parse_cwatch("probe_collision_watch:7:500", sid, secs) && secs == 120.0);
    CHECK(!probe_parse_cwatch("probe_avoid_watch:7:5", sid, secs));
    CHECK(probe_parse_sid_pfx("probe_collision:41529", "probe_collision:", sid) && sid == 41529);
    CHECK(!probe_parse_sid_pfx("probe_collision:", "probe_collision:", sid));
}

// ---------- chunk of a metre position (mb_registry_logic.hpp) ----------

static void t_reg_chunk_of_m()
{
    // 2048 m per chunk, floor for negatives
    const double pm[3] = {2047.9, -0.5, 4096.0};
    int64_t cm[3] = {0, 0, 0};
    reg_chunk_of_m(pm, cm);
    CHECK(cm[0] == 0 && cm[1] == -1 && cm[2] == 2);
}

static void t_mining_world_ready()
{
    CHECK(mining_world_ready(true, false));
    CHECK(!mining_world_ready(false, false));
    CHECK(!mining_world_ready(true, true));
    CHECK(!mining_world_ready(false, true));
}

// low power: response grace, dock loop guard, approach timeout and Throttle cap, power name matchers, low_power_return bound
static void t_lowpower()
{
    CHECK(!lowpower_due(9.9));
    CHECK(lowpower_due(10.0));
    CHECK(lowpower_due(12.5));
    CHECK(!lowpower_halt_at_dock(false, true));   // no low-power trip: the dock stage is not stopped
    CHECK(!lowpower_halt_at_dock(true, false));   // power restored at the dock: go on
    CHECK(lowpower_halt_at_dock(true, true));     // still low power after the dock stage of a low-power trip: HALT
    NEAR(approach_timeout_s(300.0, true, 0.333), 300.0 / 0.333, 1e-9);
    CHECK(approach_timeout_s(300.0, false, 0.333) == 300.0);
    CHECK(approach_timeout_s(300.0, true, 0.0) == 300.0);   // unreadable fraction: base
    CHECK(approach_timeout_s(300.0, true, 1.0) == 300.0);   // full speed in low power: base
    CHECK(lowpower_throttle_cap(10, true, 5) == 5);
    CHECK(lowpower_throttle_cap(3, true, 5) == 3);
    CHECK(lowpower_throttle_cap(10, false, 5) == 10);
    CHECK(lowpower_throttle_cap(10, true, 0) == 10);
    CHECK(power_name_match(std::wstring(L"PowerGenRate")));
    CHECK(power_name_match(std::wstring(L"ThrusterPowerDraw")));
    CHECK(power_name_match(std::wstring(L"bInLowPowerMode")));
    CHECK(!power_name_match(std::wstring(L"Health")));
    CHECK(power_obj_match(std::wstring(L"BP_ThrusterComponent_C")));
    CHECK(!power_obj_match(std::wstring(L"DMSPlayerShipObject")));
    const SettingBound* b = find_setting_bound("low_power_return");
    CHECK(b != nullptr);
    double out = -1.0;
    CHECK(b && clamp_setting(*b, 5.0, out));
    CHECK(out == 1.0);
    CHECK(b && !clamp_setting(*b, 0.0, out));
    CHECK(out == 0.0);
}

// probe_power: highlight and buffer matchers, 1 s watch cadence, watch line text
static void t_power_probe_fmt()
{
    CHECK(power_highlight_match(std::wstring(L"bAutoPullFuel")));
    CHECK(power_highlight_match(std::wstring(L"FeedMode")));
    CHECK(power_highlight_match(std::wstring(L"bEnabled")));
    CHECK(!power_highlight_match(std::wstring(L"Health")));
    CHECK(power_buffer_match(std::wstring(L"StoredFuel")));
    CHECK(!power_buffer_match(std::wstring(L"Health")));
    CHECK(power_obj_match(std::wstring(L"BP_FuelCell_C")));
    double acc = 0.0;
    CHECK(!power_watch_due(acc, 0.4));
    CHECK(power_watch_due(acc, 0.7));   // 1.1 s total: due, 0.1 s left over
    CHECK(!power_watch_due(acc, 0.5));
    CHECK(power_watch_due(acc, 0.5));
    const std::wstring line = power_watch_line(3.0, true, true, L"5", L"2", L"fuel=10", L"hydrogen=7");
    CHECK(line.find(L"lp=LOW") != std::wstring::npos);
    CHECK(line.find(L"PowerGenRate=5") != std::wstring::npos);
    CHECK(line.find(L"cargo:hydrogen=7") != std::wstring::npos);
    CHECK(power_watch_line(0.0, false, false, L"-", L"-", L"-", L"-").find(L"lp=unknown") != std::wstring::npos);
}

// power grid rules, brown-out log change, reactor auto input (due / rate / attempt cap), refuel unload rule
static void t_power_grid_auto()
{
    CHECK(power_no_power(40.0, 0.0, 0.0));        // Nickel Miner 1: consumes 40, makes nothing
    CHECK(!power_no_power(0.0, 0.0, 0.0));        // idle, nothing consumed
    CHECK(!power_no_power(200.0, 80.0, 0.4));     // Big Miner 1: brown-out, not dead
    CHECK(power_brownout(200.0, 80.0, 0.4));
    CHECK(power_brownout(280.0, 80.0, 0.29));     // Cleaner 2
    CHECK(!power_brownout(0.0, 80.0, 1.0));       // idle ship
    CHECK(!power_brownout(40.0, 0.0, 0.0));       // dead is not brown-out
    CHECK(power_bo_event(false, true) == 1);
    CHECK(power_bo_event(true, false) == 2);
    CHECK(power_bo_event(true, true) == 0);       // no line while the state holds
    CHECK(power_bo_event(false, false) == 0);
    // grace: no power for 10 s counts as the response, not before
    CHECK(!lowpower_due(9.0));
    CHECK(lowpower_due(10.0));
    double acc = 0.0;
    CHECK(!period_due(acc, 59.0, REACTOR_AUTO_PERIOD_S));
    CHECK(period_due(acc, 1.0, REACTOR_AUTO_PERIOD_S));  // 60 s: due
    CHECK(!period_due(acc, 30.0, REACTOR_AUTO_PERIOD_S));
    // auto input: off -> write once, then rate limit 30 s, then give-up after 3 writes
    CHECK(reactor_auto_action(true, 0, false, 0, 1000) == RA_NONE);             // already on
    CHECK(reactor_auto_action(false, 0, false, 0, 1000) == RA_WRITE);           // first write
    CHECK(reactor_auto_action(false, 1, false, 1000, 20000) == RA_NONE);        // 19 s after the last: rate limited
    CHECK(reactor_auto_action(false, 1, false, 1000, 31000) == RA_WRITE);       // 30 s: next write
    CHECK(reactor_auto_action(false, REACTOR_AUTO_MAX, false, 1000, 99999) == RA_GIVEUP);
    CHECK(reactor_auto_action(false, REACTOR_AUTO_MAX, true, 1000, 99999) == RA_NONE);   // give-up line written once
    CHECK(reactor_auto_action(true, REACTOR_AUTO_MAX, false, 1000, 99999) == RA_NONE);   // on now: nothing
    // refuel: unload first only when full (>= 90 % or no space)
    CHECK(!refuel_needs_unload(27, false));
    CHECK(!refuel_needs_unload(89, false));
    CHECK(refuel_needs_unload(90, false));
    CHECK(refuel_needs_unload(10, true));
    CHECK(!refuel_needs_unload(-1, false));       // unknown cargo: refuel directly
}

static void t_ship_tags()
{
    reset_globals();
    using V = std::vector<std::pair<int, int>>;
    CHECK((ship_tags_parse("[TITANIUM]") == V{{0, 0}}));
    CHECK((ship_tags_parse("[TITANIUM:200]") == V{{0, 200}}));
    CHECK((ship_tags_parse("Miner [Titanium : 200]") == V{{0, 200}}));
    CHECK((ship_tags_parse("[NICKEL:100] [TITANIUM:50]") == V{{1, 100}, {0, 50}}));
    CHECK((ship_tags_parse("[Water Ice:7]") == V{{4, 7}}));
    CHECK((ship_tags_parse("Medium Miner 1 [ICE]") == V{{4, 0}}));
    CHECK((ship_tags_parse("[ice:30]") == V{{4, 30}}));
    CHECK((ship_tags_parse("[FOO] [MISC] [TITANIUM]") == V{{0, 0}}));
    CHECK((ship_tags_parse("[TITANIUM:x]") == V{{0, 0}}));
    CHECK((ship_tags_parse("[TITANIUM:-5]") == V{{0, 0}}));
    CHECK((ship_tags_parse("[TITANIUM:]") == V{{0, 0}}));
    CHECK((ship_tags_parse("[TITANIUM:5] [TITANIUM:9]") == V{{0, 5}}));
    CHECK(ship_tags_parse("[TITANIUM").empty());
    CHECK(ship_tags_parse("no tags").empty());
    CHECK(ship_tags_items(ship_tags_parse("[NICKEL:100] [TITANIUM:50]")) == "ITEM_NickelOre:100,ITEM_TitaniumOre:50");
    reset_globals();
    g_ship_kv[7]["items"] = "ITEM_Silica:9";
    g_ship_names[7] = "[NICKEL:100] [TITANIUM:50]";
    Config c = eff_cfg(7);
    CHECK(c.items.size() == 2);
    CHECK(c.resource == ORE_ITEM[1]);
    CHECK(c.amount == 100);
    g_item_idx[7] = 1;
    c = eff_cfg(7);
    CHECK(c.resource == ORE_ITEM[0]);
    CHECK(c.amount == 50);
    g_item_idx.clear();
    g_ship_names[7] = "plain";
    c = eff_cfg(7);
    CHECK(c.resource == "ITEM_Silica");
    c = eff_cfg(8);
    CHECK(c.resource != ORE_ITEM[1]);
    CHECK(g_ship_kv[7]["items"] == "ITEM_Silica:9");
}

static void t_dock_tags()
{
    reset_globals();
    DockTag a = dock_tag_parse(7, "Main [TITANIUM] [auto-filter]");
    CHECK((a.ores == std::vector<int>{0}) && a.autof && !a.misc);
    CHECK((dock_tag_parse(1, "Ice [Water Ice]").ores == std::vector<int>{4}));
    CHECK((dock_tag_parse(1, "[ICE]").ores == std::vector<int>{4}));
    CHECK(dock_tag_parse(1, "[MISC]").misc);
    CHECK(dock_tag_parse(1, "Titanium plates").ores.empty());
    CHECK(dock_tag_parse(1, "[NICKEL").ores.empty());
    CHECK(dock_tag_parse(1, "[FOO] [x]").ores.empty());
    CHECK((dock_tag_parse(1, "[titanium][NICKEL]").ores == std::vector<int>{0, 1}));
    DockTag bad = dock_tag_parse(30, "[AUTO-FILTER] [NICKEL] [COBALT]"); CHECK(dock_tag_invalidate(bad)); CHECK(bad.ores.empty() && !bad.autof);
    DockTag ok1 = dock_tag_parse(31, "[AUTO-FILTER] [NICKEL]"); CHECK(!dock_tag_invalidate(ok1)); CHECK(ok1.autof && ok1.ores.size() == 1);
    DockTag ms = dock_tag_parse(32, "[MISC] [NICKEL] [COBALT]"); CHECK(dock_tag_invalidate(ms) && ms.misc);
    g_dock_tags = {dock_tag_parse(10, "[TITANIUM]"), dock_tag_parse(11, "[MISC]")};
    g_cfg.ore_docks[0] = {5, 10}; g_cfg.ore_docks[2] = {9};
    Config c = eff_cfg(1);
    CHECK((c.ore_docks[0] == std::vector<int>{10, 5}));
    CHECK((c.ore_docks[1] == std::vector<int>{11}));
    CHECK((c.ore_docks[2] == std::vector<int>{9}));
    CHECK((g_cfg.ore_docks[0] == std::vector<int>{5, 10}));
    DockTag p1 = dock_tag_parse(20, "[AUTO-FILTER] [NICKEL]"), p2 = dock_tag_parse(21, "[AUTO-FILTER]"), p3 = dock_tag_parse(22, "[AUTO-FILTER] [MISC] [TITANIUM]"), p4 = dock_tag_parse(23, "[COBALT]");
    Config m; m.ore_docks[3] = {21};
    auto pr = autofilter_pairs({p1, p2, p3, p4}, {m});
    CHECK((pr == std::vector<std::pair<int, int>>{{20, 1}, {21, 3}}));
    g_cfg.misc_docks = {40, 11};
    c = eff_cfg(1);
    CHECK((c.misc_docks == std::vector<int>{11, 40}));
    CHECK((c.ore_docks[1] == std::vector<int>{11, 40}));
    CHECK((c.ore_docks[2] == std::vector<int>{9}));
    g_dock_tags.clear();
    c = eff_cfg(1);
    CHECK((c.ore_docks[1] == std::vector<int>{40, 11}));
    std::map<std::string, std::string> kvm{{"unload_docks_misc", "7,8,x"}};
    Config kc; apply_kv(kc, kvm, true);
    CHECK((kc.misc_docks == std::vector<int>{7, 8}));
    Config m2; m2.misc_docks = {21};
    CHECK(autofilter_pairs({p2}, {m, m2}).empty());
    DockTag rf = dock_tag_parse(1, "Fuel [REFUEL] [hydrogen] [Warp]");
    CHECK(rf.refuel && rf.fuel == 5 && !rf.misc);
    CHECK(dock_tag_parse(1, "[REFUEL]").refuel && dock_tag_parse(1, "[REFUEL]").fuel == 0);
    CHECK(dock_tag_parse(1, "[DEUTERIUM]").fuel == 2 && !dock_tag_parse(1, "[DEUTERIUM]").refuel);
    g_dock_tags = {dock_tag_parse(50, "[REFUEL] [HYDROGEN]"), dock_tag_parse(51, "[REFUEL]"), dock_tag_parse(60, "[PARKING] [REFUEL]"), dock_tag_parse(11, "[MISC]")};
    g_cfg.refuel_docks = {9, 51}; g_cfg.misc_docks = {50}; g_cfg.ore_docks[0] = {50, 5};
    c = eff_cfg(1);
    CHECK((c.refuel_docks == std::vector<int>{50, 51, 9}));
    CHECK((c.park_docks == std::vector<int>{60}));
    CHECK((c.misc_docks == std::vector<int>{11}));
    CHECK((c.ore_docks[0] == std::vector<int>{5}));
    CHECK(autofilter_pairs({dock_tag_parse(70, "[REFUEL] [AUTO-FILTER] [NICKEL]")}, {Config()}).empty());
    CHECK((refuel_by_fuel({50, 51, 9}, g_dock_tags, 2) == std::vector<int>{51, 9}));
    CHECK((refuel_by_fuel({50, 51, 9}, g_dock_tags, 1) == std::vector<int>{50, 51, 9}));
    CHECK((refuel_by_fuel({50}, g_dock_tags, 2) == std::vector<int>{50}));
    CHECK((refuel_by_fuel({50, 51, 9}, g_dock_tags, 0) == std::vector<int>{50, 51, 9}));
    Config fc; fc.fuel[0] = {1, 300, 300}; fc.fuel[1] = {0, 300, 300}; fc.fuel[2] = {1, 300, 300};
    int64_t h1[N_FUEL] = {500, 0, 100}, h2[N_FUEL] = {500, 0, 500};
    CHECK(fuel_need_mask(fc, h1) == 2 && fuel_need_mask(fc, h2) == 3);
    reset_globals();
}

static void t_parking()
{
    reset_globals();
    CHECK(dock_tag_parse(1, "[PARKING]").park);
    DockTag pb = dock_tag_parse(2, "Bay [parking] [titanium]"); CHECK(pb.park && (pb.ores == std::vector<int>{0}));
    CHECK(!dock_tag_parse(1, "[PARK]").park);
    g_dock_tags = {dock_tag_parse(9, "[PARKING]")};
    g_cfg.misc_docks = {9}; g_cfg.ore_docks[0] = {9, 5}; g_cfg.refuel_docks = {9};
    Config c = eff_cfg(1);
    CHECK(c.misc_docks.empty() && c.refuel_docks.empty());
    CHECK((c.ore_docks[0] == std::vector<int>{5}));
    CHECK(c.ore_docks[1].empty());
    DockTag pa = dock_tag_parse(8, "[PARKING] [AUTO-FILTER] [NICKEL]");
    Config m; CHECK(autofilter_pairs({pa}, {m}).empty());
    g_dock_tags.clear();
    std::vector<DockTag> mix = {dock_tag_parse(3, "[PARKING]"), dock_tag_parse(5, "[MISC]"), dock_tag_parse(9, "[PARKING]")};
    CHECK((park_ids(mix) == std::vector<int>{3, 9}));
    CHECK((park_order({1, 2, 3}, {500, -1, 100}) == std::vector<int>{3, 1, 2}));
    CHECK(!park_wanted({}, 0, false, false));
    CHECK(!park_wanted({3}, 0, true, false));
    CHECK(!park_wanted({3}, 0, false, true));
    CHECK(!park_wanted({3}, 3, false, false));
    CHECK(park_wanted({3}, 0, false, false));
    // park fallback (no dock, button trip): only with a parking dock, no flee, no Dock-at pick, not already parking
    CHECK(park_fallback_wanted(false, false, false, true, true));
    CHECK(!park_fallback_wanted(true, false, false, true, true));
    CHECK(!park_fallback_wanted(false, true, false, true, true));
    CHECK(!park_fallback_wanted(false, false, true, true, true));
    CHECK(!park_fallback_wanted(false, false, false, false, true));
    CHECK(!park_fallback_wanted(false, false, false, true, false));
    // manual parking_docks merged with tags; stripped from other lists
    reset_globals();
    g_dock_tags = {dock_tag_parse(9, "[PARKING]")};
    g_cfg.park_docks = {7, 9}; g_cfg.ore_docks[1] = {7, 3}; g_cfg.misc_docks = {7}; g_cfg.refuel_docks = {7};
    Config e = eff_cfg(1);
    CHECK((e.park_docks == std::vector<int>{9, 7}));
    CHECK((e.ore_docks[1] == std::vector<int>{3}));
    CHECK(e.misc_docks.empty() && e.refuel_docks.empty());
    { Config k; apply_kv(k, {{"parking_docks", "4,5,x"}}, false); CHECK((k.park_docks == std::vector<int>{4, 5})); }
    g_ship_kv[1] = {{"parking_docks", "8"}};
    CHECK((eff_cfg(1).park_docks == std::vector<int>{9, 8}));
    { Config pc; pc.park_docks = {7}; CHECK(autofilter_pairs({dock_tag_parse(7, "[AUTO-FILTER] [NICKEL]")}, {pc}).empty()); }
    reset_globals();
}

// ship no-go sphere (mining ships as blockers): zone test, yield timer, tiebreak, path through a ship sphere, leaving-sphere ignore
static void t_ship_zone()
{
    CHECK(zone_hold_for(false, 0, 1, 5, 2));
    CHECK(zone_hold_for(true, 10, 1, 20, 2));
    CHECK(!zone_hold_for(true, 20, 1, 10, 2));
    CHECK(zone_hold_for(true, 10, 1, 10, 2));
    CHECK(!zone_hold_for(true, 10, 2, 10, 1));
    CHECK(!beam_yield_due(14.9));
    CHECK(beam_yield_due(15.0));
    const double o[3] = {0, 0, 0}, p299[3] = {299, 0, 0}, p301[3] = {301, 0, 0};
    CHECK(in_ship_zone(p299, o));
    CHECK(!in_ship_zone(p301, o));
    PathRock sr;
    sr.c[0] = sr.c[1] = sr.c[2] = 0.0;
    sr.r = block_r_m(SHIP_ZONE_RAD_M, 20.0);
    const double a1[3] = {-1000, 100, 0}, b1[3] = {1000, 100, 0}, a2[3] = {-1000, 400, 0}, b2[3] = {1000, 400, 0};
    double dd = 0.0;
    CHECK(path_first_blocker(a1, b1, &sr, 1, dd) == 0);
    CHECK(path_first_blocker(a2, b2, &sr, 1, dd) == -1);
    const double ia[3] = {50, 0, 0}, ib[3] = {200, 0, 0}, ib2[3] = {-200, 0, 0};
    CHECK(ship_blk_ignore(ia, ib, o, 320.0));
    CHECK(!ship_blk_ignore(ia, ib2, o, 320.0));
}

static void t_shield_from_raw()
{
    // raw PNum Value is Q16: 65536 = 1 unit; negative = unreadable (-1); whole units floor
    CHECK(shield_from_raw(-65536) == -1);
    CHECK(shield_from_raw(0) == 0);
    CHECK(shield_from_raw(65536LL * 60) == 60);
    CHECK(shield_from_raw(65536LL * 60 + 100) == 60);
}

static void t_mining_read_from_caps()
{
    // MSCore_ship_mining_caps -> presence read: unknown/off = -1, bit 1 (MiningLaser) = 1, else 0
    CHECK(mining_read_from_caps(-1) == -1);
    CHECK(mining_read_from_caps(-2) == -1);
    CHECK(mining_read_from_caps(0) == 0);
    CHECK(mining_read_from_caps(1) == 1);
    CHECK(mining_read_from_caps(2) == 0);
    CHECK(mining_read_from_caps(3) == 1);
}

static void t_station_zone()
{
    StZone a; a.id = 1; a.sys = 5;
    // boundary inclusive, just outside false
    const double c0[3] = {0, 0, 0}, in[3] = {500, 0, 0}, out[3] = {500.5, 0, 0};
    CHECK(in_zone(in, c0, 500));
    CHECK(!in_zone(out, c0, 500));
    // other system ignored, any zone of the same system counts
    StZone b; b.id = 2; b.sys = 5; b.c[0] = 10000;
    std::vector<StZone> zs = {a, b};
    const double p[3] = {10050, 0, 0};
    CHECK(in_station_zone(5, p, zs, 100));
    CHECK(station_zone_hit(5, p, zs, 100) == 2);
    CHECK(!in_station_zone(6, p, zs, 100));
    CHECK(!in_station_zone(5, out, std::vector<StZone>{b}, 100));
    CHECK(!in_station_zone(5, p, zs, 0));    // 0 = bubble off
    CHECK(Config{}.station_zone_radius_m == 2000);
    // log once per (ship, station)
    std::set<std::pair<int, int>> seen;
    CHECK(zone_log_once(seen, 7, 1));
    CHECK(!zone_log_once(seen, 7, 1));
    CHECK(zone_log_once(seen, 7, 2));
    CHECK(zone_log_once(seen, 8, 1));
}

static void t_laser_gate()
{
    // laser gate mode: no export or module off = bench-only fallback; unknown read = hold; 0..3 = check
    CHECK(laser_gate_mode(false, 1) == LASER_FALLBACK_BENCH_ONLY);
    CHECK(laser_gate_mode(true, -2) == LASER_FALLBACK_BENCH_ONLY);
    CHECK(laser_gate_mode(true, -1) == LASER_HOLD);
    CHECK(laser_gate_mode(true, 0) == LASER_CHECK);
    CHECK(laser_gate_mode(true, 3) == LASER_CHECK);
    // presence read: no bench block = absent, else the caps mapping
    CHECK(bench_read(false, 3) == 0);
    CHECK(bench_read(true, -1) == -1);
    CHECK(bench_read(true, 0) == 0);
    CHECK(bench_read(true, 1) == 1);
    CHECK(bench_read(true, 2) == 0);
    // gate_accept(bench, mode, listed, presence_accepted): FALLBACK = bench; HOLD = bench (unknown accepted, listed or not); CHECK = presence
    // bench, mode, listed, pres -> has
    CHECK(gate_accept(false, LASER_FALLBACK_BENCH_ONLY, false, false) == false);
    CHECK(gate_accept(false, LASER_FALLBACK_BENCH_ONLY, false, true) == false);
    CHECK(gate_accept(false, LASER_FALLBACK_BENCH_ONLY, true, false) == false);
    CHECK(gate_accept(false, LASER_FALLBACK_BENCH_ONLY, true, true) == false);
    CHECK(gate_accept(true, LASER_FALLBACK_BENCH_ONLY, false, false) == true);
    CHECK(gate_accept(true, LASER_FALLBACK_BENCH_ONLY, false, true) == true);
    CHECK(gate_accept(true, LASER_FALLBACK_BENCH_ONLY, true, false) == true);
    CHECK(gate_accept(true, LASER_FALLBACK_BENCH_ONLY, true, true) == true);
    CHECK(gate_accept(false, LASER_HOLD, false, false) == false);
    CHECK(gate_accept(false, LASER_HOLD, false, true) == false);
    CHECK(gate_accept(false, LASER_HOLD, true, false) == false);
    CHECK(gate_accept(false, LASER_HOLD, true, true) == false);
    CHECK(gate_accept(true, LASER_HOLD, false, false) == true);    // never listed, unknown read: accepted as bench-only
    CHECK(gate_accept(true, LASER_HOLD, false, true) == true);
    CHECK(gate_accept(true, LASER_HOLD, true, false) == true);     // listed, unknown read: stays listed
    CHECK(gate_accept(true, LASER_HOLD, true, true) == true);
    CHECK(gate_accept(false, LASER_CHECK, false, false) == false);  // presence decides (debounce)
    CHECK(gate_accept(false, LASER_CHECK, false, true) == true);    // listed, bench absent, still inside the debounce
    CHECK(gate_accept(false, LASER_CHECK, true, false) == false);
    CHECK(gate_accept(false, LASER_CHECK, true, true) == true);
    CHECK(gate_accept(true, LASER_CHECK, false, false) == false);   // bench, no laser (read 0), never listed: rejected
    CHECK(gate_accept(true, LASER_CHECK, false, true) == true);     // bench + laser (read 1)
    CHECK(gate_accept(true, LASER_CHECK, true, false) == false);    // listed, no laser for 3 reads: dropped
    CHECK(gate_accept(true, LASER_CHECK, true, true) == true);
}

int main()
{
    const Case cases[] = {
        {"qmul_identity", t_qmul_identity}, {"qconj", t_qconj}, {"cross_dot_norm_dist", t_cross_dot_norm_dist},
        {"quat_angle", t_quat_angle}, {"rotvec_zero", t_rotvec_zero},
        {"trim", t_trim}, {"to_int", t_to_int}, {"parse_distance", t_parse_distance}, {"parse_items", t_parse_items},
        {"ok_res", t_ok_res},
        {"shield_from_raw", t_shield_from_raw},
        {"apply_kv_empty_keeps_old", t_apply_kv_empty_keeps_old}, {"apply_kv_dock_per_ship", t_apply_kv_dock_per_ship},
        {"apply_kv_items_empty_clears", t_apply_kv_items_empty_clears}, {"dock_lists", t_dock_lists}, {"eff_cfg_items_row", t_eff_cfg_items_row},
        {"eff_cfg_does_not_touch_global", t_eff_cfg_does_not_touch_global}, {"trip_progress", t_trip_progress}, {"ob_dt", t_ob_dt},
        {"parse_cmd_line", t_parse_cmd_line}, {"cmd_seq_is_new", t_cmd_seq_is_new},
        {"belt", t_belt}, {"csq", t_csq}, {"sector_contains", t_sector_contains}, {"warp_fuel", t_warp_fuel},
        {"cargo_pct", t_cargo_pct}, {"fuel_low", t_fuel_low}, {"got_delta", t_got_delta}, {"row_done", t_row_done},
        {"item_next", t_item_next}, {"collect_verdict", t_collect_verdict}, {"sweep", t_sweep}, {"ore_scan_due", t_ore_scan_due}, {"idle_limit", t_idle_limit}, {"hull_dropped", t_hull_dropped}, {"dead_scan", t_dead_scan},
        {"dock_limit_giveup", t_dock_limit_giveup}, {"loop_guards", t_loop_guards}, {"setting_clamp", t_setting_clamp},{"unload_chain_more", t_unload_chain_more}, {"unload_cand", t_unload_cand}, {"unload_misc", t_unload_misc},{"refuel_stalled", t_refuel_stalled}, {"mine_radius", t_mine_radius},
        {"station", t_station}, {"mining_world_ready", t_mining_world_ready},
        {"net_free_go", t_net_free_go}, {"net_busy_wait", t_net_busy_wait}, {"net_second_dock", t_net_second_dock},
        {"net_fifo", t_net_fifo}, {"net_go_drops_other_grant", t_net_go_drops_other_grant}, {"net_not_configured", t_net_not_configured}, {"net_dock_gone", t_net_dock_gone},
        {"net_full", t_net_full}, {"net_bad_holder", t_net_bad_holder}, {"unload_stalled", t_unload_stalled},
        {"net_log_gate", t_net_log_gate}, {"net_self_occupant", t_net_self_occupant}, {"net_queue_never_occupant", t_net_queue_never_occupant}, {"net_blocked_by_never_self", t_net_blocked_by_never_self},
        {"net_reconcile", t_net_reconcile}, {"net_release", t_net_release}, {"net_text", t_net_text},
        {"watchdog", t_watchdog}, {"probe", t_probe}, {"probe_collision", t_probe_collision},
        {"net_score_order", t_net_score_order}, {"net_higher_cargo_wins", t_net_higher_cargo_wins}, {"net_non_preempt", t_net_non_preempt},
        {"net_flee_no_slot", t_net_flee_no_slot}, {"net_slots", t_net_slots}, {"net_slot_pos", t_net_slot_pos}, {"net_offset_carry", t_net_offset_carry}, {"net_line_spacing", t_net_line_spacing},
        {"net_clear_expired", t_net_clear_expired}, {"net_unload_first", t_net_unload_first}, {"dock_undock_first", t_dock_undock_first},
        {"combat_flee", t_combat_flee}, {"combat_log", t_combat_log},
        {"hazard_flee", t_hazard_flee}, {"targett_entry", t_targett_entry},
        {"path_block", t_path_block}, {"path_perf", t_path_perf}, {"path_waypoint", t_path_waypoint}, {"shield_drop", t_shield_drop}, {"safety_net", t_safety_net}, {"flee_guard_shield_units", t_flee_guard_shield_units}, {"flee_fallback_docks", t_flee_fallback_docks}, {"attack_dist_and_flee_class", t_attack_dist_and_flee_class}, {"flee_idle_move", t_flee_idle_move}, {"flee_real_warp_hold", t_flee_real_warp_hold},{"fast_sample", t_fast_sample},
        {"dock_tags", t_dock_tags},
        {"ship_tags", t_ship_tags},
        {"parking", t_parking},
        {"rock_depleted", t_rock_depleted},
        {"ship_zone", t_ship_zone}, {"station_zone", t_station_zone},
        {"mining_read_from_caps", t_mining_read_from_caps}, {"laser_gate", t_laser_gate},
        {"approach_throttle", t_approach_throttle}, {"bubble", t_bubble}, {"bubble_ladder", t_bubble_ladder}, {"bubble_scope", t_bubble_scope}, {"tune_geom", t_tune_geom}, {"flight_warp", t_flight_warp}, {"sweep_return_flee", t_sweep_return_flee}, {"path2_rules", t_path2_rules},
        {"reg_abs_m", t_reg_abs_m}, {"reg_chunk_of_m", t_reg_chunk_of_m},
        {"box_probe_parse", t_box_probe_parse}, {"box_probe_uu_fwd", t_box_probe_uu_fwd}, {"reg_check_summary", t_reg_check_summary},
        {"lowpower", t_lowpower}, {"power_probe_fmt", t_power_probe_fmt}, {"power_grid_auto", t_power_grid_auto},
    };
    for (const Case& c : cases)
    {
        g_cur = c.name;
        c.fn();
    }
    std::printf("test_logic: %d passed, %d failed (%d checks)\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
