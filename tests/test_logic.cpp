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

static void t_dock_backoff()
{
    CHECK(dock_backoff_s(0) == 2);
    CHECK(dock_backoff_s(1) == 5);
    CHECK(dock_backoff_s(2) == 10);
    CHECK(dock_backoff_s(3) == 20);
    CHECK(dock_backoff_s(4) == 40);
    CHECK(dock_backoff_s(5) == 60);
    CHECK(dock_backoff_s(6) == 60);
    CHECK(dock_backoff_s(100) == 60);
}

static void t_hull_dropped()
{
    CHECK(!hull_dropped(-1, 0));                    // no baseline: never a drop
    CHECK(hull_dropped(350, 320));
    CHECK(!hull_dropped(350, 350));
    CHECK(!hull_dropped(350, 351));
    CHECK(!hull_dropped(0, 0));
}

static void t_dock_limit_giveup()
{
    CHECK(MINE_DOCK_RETRIES == 10);
    CHECK(!dock_limit_giveup(9));
    CHECK(dock_limit_giveup(10));
    CHECK(dock_limit_giveup(11));
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

// ---------- runner ----------

struct Case { const char* name; void (*fn)(); };

int main()
{
    const Case cases[] = {
        {"qmul_identity", t_qmul_identity}, {"qconj", t_qconj}, {"cross_dot_norm_dist", t_cross_dot_norm_dist},
        {"quat_angle", t_quat_angle}, {"rotvec_zero", t_rotvec_zero},
        {"trim", t_trim}, {"to_int", t_to_int}, {"parse_distance", t_parse_distance}, {"parse_items", t_parse_items},
        {"ok_res", t_ok_res},
        {"apply_kv_empty_keeps_old", t_apply_kv_empty_keeps_old}, {"apply_kv_dock_per_ship", t_apply_kv_dock_per_ship},
        {"apply_kv_items_empty_clears", t_apply_kv_items_empty_clears}, {"dock_lists", t_dock_lists}, {"eff_cfg_items_row", t_eff_cfg_items_row},
        {"eff_cfg_does_not_touch_global", t_eff_cfg_does_not_touch_global},
        {"parse_cmd_line", t_parse_cmd_line}, {"cmd_seq_is_new", t_cmd_seq_is_new},
        {"belt", t_belt}, {"csq", t_csq}, {"sector_contains", t_sector_contains}, {"warp_fuel", t_warp_fuel},
        {"cargo_pct", t_cargo_pct}, {"fuel_low", t_fuel_low}, {"got_delta", t_got_delta}, {"row_done", t_row_done},
        {"item_next", t_item_next}, {"collect_verdict", t_collect_verdict}, {"dock_backoff", t_dock_backoff}, {"hull_dropped", t_hull_dropped},
        {"dock_limit_giveup", t_dock_limit_giveup}, {"refuel_stalled", t_refuel_stalled}, {"mine_radius", t_mine_radius},
        {"station", t_station},
    };
    for (const Case& c : cases)
    {
        g_cur = c.name;
        c.fn();
    }
    std::printf("test_logic: %d passed, %d failed (%d checks)\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
