// FILE: tests/test_logic.cpp
// WHAT: unit checks for mb_logic.hpp (parsing, command line, belt, fuel, cargo, dock and hull rules) and mb_math.hpp (quaternion, vector, angle). No game needed.
// WHY: these functions decide what the mining loop does; the checks keep their rules fixed.
// RUN: tests\run.ps1 (builds with cl, runs, exit code 0 = all PASS). Or by hand: see run.ps1.
// DEPENDS ON: mods/MiningBenchMod/mb_logic.hpp, mods/MiningBenchMod/mb_math.hpp (mb_math needs <cmath>, included first).
// NOTE: CHECK counts failures and keeps going; the summary line prints the pass and fail counts.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "mb_logic.hpp"
#include "mb_math.hpp"
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
    CHECK(station_destroyable("BP_StationObject_C"));
    CHECK(!station_destroyable("BP_AllyStationObject_C"));
    CHECK(!station_destroyable("BP_AbandonedStationObject_01_C"));
    CHECK(!station_destroyable("BPO_TradingOutpost_01_C"));
    CHECK(!station_destroyable(""));
    CHECK(station_row(7, "My|Base\n", "BP_StationObject_C", 3) == "7|My Base |BP_StationObject_C|3|1");
    CHECK(station_row(8, "Outpost", "BP_AllyStationObject_C", 2) == "8|Outpost|BP_AllyStationObject_C|2|0");
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
    // circle directions: base turned 90 deg per step, z kept
    const double base[3] = {1.0, 0.0, 0.5};
    double d[3];
    hazard_dir(base, 0, d); CHECK(d[0] == 1.0 && d[1] == 0.0 && d[2] == 0.5);
    hazard_dir(base, 1, d); NEAR(d[0], 0.0, 1e-9); NEAR(d[1], 1.0, 1e-9); CHECK(d[2] == 0.5);
    hazard_dir(base, 2, d); NEAR(d[0], -1.0, 1e-9); NEAR(d[1], 0.0, 1e-9);
    hazard_dir(base, 4, d); NEAR(d[0], 1.0, 1e-9); NEAR(d[1], 0.0, 1e-9);
    hazard_dir(base, -1, d); NEAR(d[0], 0.0, 1e-9); NEAR(d[1], -1.0, 1e-9);
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

static void t_warp_settle()
{
    CHECK(!warp_settle_done(1.0, 100.0));          // under the 2 s minimum: wait even when arrived
    CHECK(warp_settle_done(2.0, 300.0));           // arrived at the dock
    CHECK(!warp_settle_done(5.0, 1500.0));         // still far
    CHECK(!warp_settle_done(5.0, -1.0));           // position unknown: wait
    CHECK(warp_settle_done(30.0, -1.0));           // cap: go on after 30 s
    CHECK(warp_settle_done(5.0, 1000.0));          // exactly the arrival distance counts
}

// clear-path geometry: segment vs sphere (in front, behind, beyond the end, tangent)
static void t_path_block()
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
    NEAR(seg_point_dist(a, b, beyond), 500.0, 1e-9);           // clamped: distance to the end point
    NEAR(seg_point_dist(a, b, front), 100.0, 1e-9);
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

// waypoint and stand-off geometry
static void t_path_waypoint()
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
    // stand-off: from the rock centre toward the ship, at the given distance
    const double rc[3] = {1000, 0, 0};
    double so[3] = {0, 0, 0};
    standoff_point(a, rc, 200.0, so);
    NEAR(so[0], 800.0, 1e-9); NEAR(so[1], 0.0, 1e-9); NEAR(so[2], 0.0, 1e-9);
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
    // budget: takes one unit per call until empty
    int bud = 2;
    CHECK(budget_take(bud)); CHECK(budget_take(bud)); CHECK(!budget_take(bud)); CHECK(bud == 0);
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
    // snapshot rebuild: list changed, or 5 s old, and never while building
    CHECK(snap_rebuild_due(false, 7, 6, 1000, 0));
    CHECK(!snap_rebuild_due(false, 7, 7, 4000, 0));
    CHECK(snap_rebuild_due(false, 7, 7, 5000, 0));
    CHECK(!snap_rebuild_due(true, 8, 7, 9000, 0));
    NEAR(PATH_SLOW_MS, 3.0, 1e-12);
    CHECK(PATH_CANDIDATES == 8);
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

int main()
{
    const Case cases[] = {
        {"qmul_identity", t_qmul_identity}, {"qconj", t_qconj}, {"cross_dot_norm_dist", t_cross_dot_norm_dist},
        {"quat_angle", t_quat_angle}, {"rotvec_zero", t_rotvec_zero},
        {"trim", t_trim}, {"to_int", t_to_int}, {"parse_distance", t_parse_distance}, {"parse_items", t_parse_items},
        {"ok_res", t_ok_res},
        {"apply_kv_empty_keeps_old", t_apply_kv_empty_keeps_old}, {"apply_kv_dock_per_ship", t_apply_kv_dock_per_ship},
        {"apply_kv_items_empty_clears", t_apply_kv_items_empty_clears}, {"dock_lists", t_dock_lists}, {"eff_cfg_items_row", t_eff_cfg_items_row},
        {"eff_cfg_does_not_touch_global", t_eff_cfg_does_not_touch_global}, {"trip_progress", t_trip_progress}, {"warp_settle", t_warp_settle},
        {"parse_cmd_line", t_parse_cmd_line}, {"cmd_seq_is_new", t_cmd_seq_is_new},
        {"belt", t_belt}, {"csq", t_csq}, {"sector_contains", t_sector_contains}, {"warp_fuel", t_warp_fuel},
        {"cargo_pct", t_cargo_pct}, {"fuel_low", t_fuel_low}, {"got_delta", t_got_delta}, {"row_done", t_row_done},
        {"item_next", t_item_next}, {"collect_verdict", t_collect_verdict}, {"sweep", t_sweep}, {"ore_scan_due", t_ore_scan_due}, {"idle_limit", t_idle_limit}, {"hull_dropped", t_hull_dropped}, {"dead_scan", t_dead_scan},
        {"dock_limit_giveup", t_dock_limit_giveup}, {"loop_guards", t_loop_guards}, {"setting_clamp", t_setting_clamp},{"unload_chain_more", t_unload_chain_more}, {"unload_cand", t_unload_cand}, {"refuel_stalled", t_refuel_stalled}, {"mine_radius", t_mine_radius},
        {"station", t_station},
        {"net_free_go", t_net_free_go}, {"net_busy_wait", t_net_busy_wait}, {"net_second_dock", t_net_second_dock},
        {"net_fifo", t_net_fifo}, {"net_go_drops_other_grant", t_net_go_drops_other_grant}, {"net_not_configured", t_net_not_configured}, {"net_dock_gone", t_net_dock_gone},
        {"net_full", t_net_full}, {"net_bad_holder", t_net_bad_holder}, {"unload_stalled", t_unload_stalled},
        {"net_log_gate", t_net_log_gate}, {"net_self_occupant", t_net_self_occupant}, {"net_queue_never_occupant", t_net_queue_never_occupant}, {"net_blocked_by_never_self", t_net_blocked_by_never_self},
        {"net_reconcile", t_net_reconcile}, {"net_release", t_net_release}, {"net_text", t_net_text},
        {"watchdog", t_watchdog},
        {"net_score_order", t_net_score_order}, {"net_higher_cargo_wins", t_net_higher_cargo_wins}, {"net_non_preempt", t_net_non_preempt},
        {"net_flee_no_slot", t_net_flee_no_slot}, {"net_slots", t_net_slots}, {"net_slot_pos", t_net_slot_pos}, {"net_offset_carry", t_net_offset_carry}, {"net_line_spacing", t_net_line_spacing},
        {"net_clear_expired", t_net_clear_expired}, {"net_unload_first", t_net_unload_first}, {"dock_undock_first", t_dock_undock_first},
        {"combat_flee", t_combat_flee}, {"combat_log", t_combat_log},
        {"hazard_flee", t_hazard_flee}, {"targett_entry", t_targett_entry},
        {"path_block", t_path_block}, {"path_perf", t_path_perf}, {"path_waypoint", t_path_waypoint}, {"shield_drop", t_shield_drop}, {"fast_sample", t_fast_sample},
        {"rock_depleted", t_rock_depleted},
    };
    for (const Case& c : cases)
    {
        g_cur = c.name;
        c.fn();
    }
    std::printf("test_logic: %d passed, %d failed (%d checks)\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
