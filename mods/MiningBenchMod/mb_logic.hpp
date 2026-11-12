// FILE: mods/MiningBenchMod/mb_logic.hpp
// WHAT: pure logic: config structs and INI value parsing (Config, apply_kv, eff_cfg, parse_distance, parse_items), command line
//       parsing, and the small rule functions the mining loop, belt and fuel code use (no game objects).
// WHY separate: these have no UE or Windows calls, so the unit tests (tests/test_logic.cpp) can compile and run them alone.
// DEPENDS ON (exact): std headers only (algorithm, array, cctype, cmath, cstdint, cstdlib, iterator, map, set, string, utility, vector). No UE4SS, no Windows.h.
// USED BY: mb_common.hpp (includes this first), mb_config.hpp, mb_cmdstate.hpp, mb_belt.hpp, mb_mining.hpp, mb_dockjob.hpp, mb_testcmd.hpp, tests/test_logic.cpp
// RULE: keep this file free of UE/Windows headers and STR(); use L"" literals.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // WHAT: the fuels and ores the Operation tab can refuel / unload, one index per item.
    // WHY: the INI keys, the item object names and the Lua UI share the same order.
    constexpr int N_FUEL = 3;
    constexpr int N_ORE = 7;
    constexpr const char* FUEL_KEY[N_FUEL] = {"hydrogen", "warp", "deuterium"};
    constexpr const char* FUEL_ITEM[N_FUEL] = {"ITEM_HydrogenFuel", "ITEM_WarpFuel", "ITEM_EnrichedFuel"};
    constexpr const char* ORE_KEY[N_ORE] = {"titanium", "nickel", "cobalt", "iridium", "waterice", "silica", "graphite"};
    constexpr const char* ORE_ITEM[N_ORE] = {"ITEM_TitaniumOre", "ITEM_NickelOre", "ITEM_CobaltOre", "ITEM_IridiumOre", "ITEM_WaterIce", "ITEM_Silica", "ITEM_Graphite"};

    // WHAT: one refuel rule: on (1/0), threshold (refuel when fuel is below it), amount (wait-for-items and filter amount).
    // WHY: per fuel, so each fuel can be switched and sized on its own.
    struct FuelCfg
    {
        int on = 0;
        int threshold = 300;
        int amount = 300;
    };
    // WHAT: every mining setting. g_cfg holds the global values; eff_cfg(id) returns a copy with the [ship.<id>] overrides applied. Units are in each field comment.
    // WHY: one struct for both, so the same parser (apply_kv) and the same INI keys work everywhere.
    struct Config
    {
        // 1 = mod active for this ship, 0 = off. Per-ship overridable. (INI: enabled)
        int enabled = 1;
        // text: item to mine, by object name, display name or numeric ID; empty = none. (INI: resource)
        std::string resource = "";
        // items: ore amount to mine (ore gained); 0 = unlimited (mine until cargo full). (INI: amount)
        int amount = 0;
        // ordered queue of (item, amount) rows from INI items=; has_items = key present even if empty. Replaces resource/amount (eff_cfg copies the current row into them).
        std::vector<std::pair<std::string, int>> items;
        bool has_items = false;
        // save object ID of the dock (numeric); 0 = unset. (INI: dock_id)
        int dock_id = 0;
        std::string dock_name_filter = "";   // in-game dock name; overrides dock_id when non-empty
        std::string dock_id_name = "";       // dock_id written as a name (resolved to id by in-game name)
        // text: belt warp target. "none"/"" = never warp, "auto" = closest sector with the needed ore (default), digits = sector id. Per-ship. (INI: belt_sector)
        std::string belt = "auto";
        // raw PNum passed to MiningBeacon::SetRange once per dock; 0 = leave unchanged. Scale unknown (see the range log lines). (INI: range_raw)
        int range_raw = 0;
        // text distance, e.g. 10km or 8000m: clamp for radius. Empty or 0 = no clamp. Raw scale UNCALIBRATED. (INI: max_radius)
        std::string max_radius = "50km";
        // text distance, e.g. 5km: mining radius, replaces range_raw when set. Older key: Update 18 added mining_distance as the per-ship distance; both are still read. (INI: radius)
        std::string radius = "50km";
        // s: idle limit for the mining loop: go home after this long with nothing in range or no ore gained; after wait_seconds, halt a refuel whose fuel cargo has not changed for this long ("refuel stalled"). 0 = off. (INI: idle_seconds)
        int idle_seconds = 0;
        // s: max time at the dock for unload/refuel with items left; then undock, wait 30 s, dock again (10 rounds, then undock + idle). Not an error. 0 = no limit. (INI: dock_hold_limit)
        int dock_hold_limit = 300;
        // km: a dock trip longer than this warps when the ship has warp fuel. (INI: warp_trip_km)
        int warp_trip_km = 5;
        // s: how often tick_work runs (ship scan, lists). Minimum 1 (clamped on load). (INI: log_interval_seconds)
        int log_interval_seconds = 10;
        // s: minimum time between order attempts per ship. No order is issued in M3a, so this is inactive. (INI: order_retry_seconds)
        int order_retry_seconds = 30;
        // count: max order attempts per ship per game session. (INI: max_attempts)
        int max_attempts = 3;
        // 1 = after unloading, undock and mine again; 0 = stop and wait. Not acted on yet (M3a). Per-ship overridable. (INI: repeat)
        int repeat = 0;
        // s: seconds between trace log lines for bench ships. Minimum 1 (clamped on load). (INI: trace_interval_seconds)
        int trace_interval_seconds = 5;
        int record = 0;                      // no effect since 0.0.1 (recorder removed); kept so old INI files still load
        int record_interval_seconds = 2;     // global only
        bool kill_switch = false;            // 1 = the mod stops touching the game completely (hot reload). Global only. (INI: kill_switch)
        bool watchdog = true;                // 1 = freeze watchdog on: MiningFreeze.txt lines when the game thread stalls 10 s. Global only. (INI: watchdog)
        int flee_on_target = 1;              // 1 = a ship targeted by a hostile (or in combat) flees to a dock like after a hull hit. Global only. (INI: flee_on_target)
        int path_clearance_m = 4;            // clear-path check: extra clearance (m) beyond the rock block radius. Global only. (INI: path_clearance_m)
        int bubble_clearance_m = 6;         // bubble reflex: extra clearance (m) beyond ship radius + rock block radius. Global only. (INI: bubble_clearance_m)
        int flee_target_range_km = 5;        // a hostile targeter farther than this (km) does not trigger the flee. Global only. (INI: flee_target_range_km)
        int low_power_return = 1;            // 1 = a ship in low power for LOWPOWER_GRACE_S while flying/mining goes to dock like fuel low. Global only. (INI: low_power_return)
        int reactor_auto_input = 1;          // 1 = reactors of active bench ships get bAutoInputFuel on (checked at task start and every 60 s). Global only. (INI: reactor_auto_input)
        // text distance, e.g. 50km: belt mode only. Rocks farther than this from the ship count as none (the ship warps instead of flying there), and radius is capped by it. Empty/0/bad = 50 km. Per-ship. (INI: warp_range)
        std::string warp_range = "50km";
        std::string mining_distance = "50m";    // per-ship: mining distance from the rock, e.g. 200m / 0.2km (empty = unset)
        int fuel_margin = 0;                    // per-ship: fuel margin (int), 0 = unset
        int fuel_amount = 0;                    // per-ship: fuel amount (int), 0 = unset
        int raw_per_meter = 65536;              // global: raw PNum per meter (calibration)
        int steer_probe_speed_k = 60;           // global: speed limit = MaxSpeed * k (raw per second)
        int refuel_dock_id = 0;               // numeric dock id, 0 = none
        std::string refuel_dock_name = "";      // refuel_dock_id written as an in-game name
        // per ore: unload docks in list order (INI: unload_docks_<orekey>=id,id). Empty = this ore is not unloaded. Per-ship overridable.
        std::vector<int> ore_docks[N_ORE];
        // refuel docks in list order (INI: refuel_docks=id,id). refuel_dock_id is tried first. Per-ship overridable.
        std::vector<int> refuel_docks;
        // s: wait at the dock after the order list (global only). (INI: wait_seconds)
        int wait_seconds = 10;
        // per fuel: on / refuel threshold / amount (global only). (INI: fuel_<key>_on / _threshold / _amount)
        FuelCfg fuel[N_FUEL] = {{1, 300, 300}, {1, 300, 300}, {0, 300, 300}};
    };
    // WHAT: the active global config, read by tick_work and the feature code. Replaced as a whole by reload_config; a failed parse keeps the old value.
    Config g_cfg;
    // WHAT: raw key/value text of each [ship.<id>] INI section, keyed by save ship ID.
    // WHY: kept as text, so eff_cfg() re-applies it to the current g_cfg on every lookup; a reload replaces it.
    std::map<int, std::map<std::string, std::string>> g_ship_kv;   // [ship.<id>] sections
    // WHAT: copy of s without leading and trailing spaces, tabs, CR and LF (empty if all blank).
    // WHY: INI lines arrive with CRLF endings and stray spaces.
    std::string trim(const std::string& s)
    {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }
    // WHAT: string to int; def when the text is not a number.
    // WHY: INI values are typed by hand, and a typo must fall back to the default instead of throwing inside the tick.
    int to_int(const std::string& s, int def)
    {
        try { return std::stoi(s); } catch (...) { return def; }
    }
    // WHAT: text distance to meters; -1 when empty or invalid. Accepts 5km, 2500m, or a plain number (meters).
    // WHY: the Lua UI stores typed text as-is (log 2026-10-27), so the mod must parse these forms itself.
    double parse_distance(const std::string& t0)
    {
        std::string t = trim(t0);
        for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (t.empty()) return -1.0;
        double mul = 1.0;
        if (t.size() >= 2 && t.compare(t.size() - 2, 2, "km") == 0) { mul = 1000.0; t = trim(t.substr(0, t.size() - 2)); }
        else if (t.back() == 'm') t = trim(t.substr(0, t.size() - 1));
        if (t.empty()) return -1.0;
        char* end = nullptr;
        double v = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0' || v < 0.0) return -1.0;
        const double r = v * mul;
        if (!std::isfinite(r)) return -1.0;   // inf, nan, or km overflow
        return r;
    }
    // WHAT: "item:amount,item:amount" to ordered rows. A segment without ':' gets amount 0; empty segments are skipped.
    // WHY: the ini queue format; one row per segment, order kept.
    std::vector<std::pair<std::string, int>> parse_items(const std::string& t)
    {
        std::vector<std::pair<std::string, int>> out;
        size_t p = 0;
        while (p <= t.size())
        {
            size_t e = t.find(',', p);
            if (e == std::string::npos) e = t.size();
            const std::string seg = trim(t.substr(p, e - p));
            if (!seg.empty())
            {
                const size_t c = seg.rfind(':');
                out.push_back({c == std::string::npos ? seg : trim(seg.substr(0, c)), c == std::string::npos ? 0 : to_int(trim(seg.substr(c + 1)), 0)});
            }
            p = e + 1;
        }
        return out;
    }
    // WHAT: "a,b,c" to trimmed tokens; empty tokens dropped.
    // WHY: dock lists in the INI are comma separated.
    std::vector<std::string> split_csv(const std::string& t)
    {
        std::vector<std::string> out;
        size_t p = 0;
        while (p <= t.size())
        {
            size_t e = t.find(',', p);
            if (e == std::string::npos) e = t.size();
            const std::string seg = trim(t.substr(p, e - p));
            if (!seg.empty()) out.push_back(seg);
            p = e + 1;
        }
        return out;
    }
    // WHAT: one dock id from a list token; 0 when the token is not digits (names are not allowed in lists).
    int dock_token(const std::string& t)
    {
        if (t.empty() || t.find_first_not_of("0123456789") != std::string::npos) return 0;
        return to_int(t, 0);
    }
    // WHAT: "id,id" to dock ids in list order; bad and 0 tokens dropped.
    std::vector<int> dock_list(const std::string& t)
    {
        std::vector<int> out;
        for (const auto& tok : split_csv(t))
            if (int d = dock_token(tok)) out.push_back(d);
        return out;
    }
    // WHAT: current queue row per ship id (runtime only; 0 = first row). Advanced by item_advance, reset by mine_cmd start.
    std::map<int, int> g_item_idx;
    // WHAT: copies the keys present in kv into c. An empty value counts as missing and keeps c's value.
    // WHY: the same parser serves the global section and the [ship.<id>] sections. per_ship: a per-ship dock_id must not be overridden by an inherited dock_name_filter (the filter wins over dock_id), so the filter is cleared.
    void apply_kv(Config& c, const std::map<std::string, std::string>& kv, bool per_ship)
    {
        auto get = [&](const char* k) -> const std::string* {
            auto it = kv.find(k);
            return (it == kv.end() || it->second.empty()) ? nullptr : &it->second;
        };
        if (auto v = get("enabled")) c.enabled = to_int(*v, 1);
        if (auto v = get("repeat")) c.repeat = to_int(*v, 0);
        if (auto v = get("resource")) c.resource = *v;
        if (auto v = get("amount")) c.amount = to_int(*v, 0);
        // outside get(): an empty items= still counts as present (clears the queue)
        { auto it = kv.find("items"); if (it != kv.end()) { c.items = parse_items(it->second); c.has_items = true; } }
        if (auto v = get("dock_id"))
        {
            c.dock_id = 0;
            c.dock_id_name.clear();
            // a non-digit value is a dock NAME (resolved to an id later); digits are a save ID.
            if (v->find_first_not_of("0123456789") != std::string::npos) c.dock_id_name = *v;
            else c.dock_id = to_int(*v, 0);
            if (per_ship && !get("dock_name_filter")) c.dock_name_filter.clear();
        }
        if (auto v = get("dock_name_filter")) c.dock_name_filter = *v;
        if (auto v = get("belt_sector")) c.belt = *v;
        // an empty distance value means "default" (the struct default stays); "0" is how radius/max_radius are switched off
        if (auto v = get("radius")) { if (!trim(*v).empty()) c.radius = *v; }
        if (auto v = get("idle_seconds")) c.idle_seconds = to_int(*v, 0);
        if (auto v = get("dock_hold_limit")) c.dock_hold_limit = to_int(*v, 300);
        if (auto v = get("warp_trip_km")) c.warp_trip_km = to_int(*v, 5);
        if (auto v = get("mining_distance")) { if (!trim(*v).empty()) c.mining_distance = *v; }
        if (auto v = get("warp_range")) { if (!trim(*v).empty()) c.warp_range = *v; }
        if (auto v = get("fuel_margin")) c.fuel_margin = to_int(*v, 0);
        if (auto v = get("fuel_amount")) c.fuel_amount = to_int(*v, 0);
        if (auto v = get("refuel_dock_id"))
        {
            c.refuel_dock_id = 0;
            c.refuel_dock_name.clear();
            if (v->find_first_not_of("0123456789") != std::string::npos) c.refuel_dock_name = *v;
            else c.refuel_dock_id = to_int(*v, 0);
        }
        // list keys use kv.find, not get(): a present key with an empty value clears the list
        for (int i = 0; i < N_ORE; ++i)
        {
            auto it = kv.find(std::string("unload_docks_") + ORE_KEY[i]);
            if (it != kv.end()) c.ore_docks[i] = dock_list(it->second);
        }
        { auto it = kv.find("refuel_docks"); if (it != kv.end()) c.refuel_docks = dock_list(it->second); }
    }
    // WHAT: effective config for ship sid: g_cfg plus its [ship.<sid>] overrides.
    // WHY: every per-ship decision goes through this copy, so g_cfg is never changed for one ship. A copy is returned, so callers cannot write back.
    Config eff_cfg(int sid)
    {
        Config c = g_cfg;
        auto it = g_ship_kv.find(sid);
        if (it != g_ship_kv.end()) apply_kv(c, it->second, true);
        if (c.has_items)
        {
            if (c.items.empty()) { c.resource.clear(); c.amount = 0; }
            else
            {
                auto ix = g_item_idx.find(sid);
                int i = ix == g_item_idx.end() ? 0 : ix->second;
                if (i < 0 || i >= (int)c.items.size()) i = 0;
                c.resource = c.items[i].first;
                c.amount = c.items[i].second;
            }
        }
        return c;
    }
    // WHAT: parses "<seq>|<ship>|<cmd>". false when any part is malformed: seq and ship must be digits, cmd must not be empty.
    // WHY: Lua writes a millisecond-epoch seq (13 digits), which overflows int. So only the last 9 digits are kept (log: every command was "malformed" before this fix). The last 9 digits wrap about every 11.5 days; cmd_poll only checks seq != last, so a wrap does not block commands.
    bool parse_cmd_line(const std::string& line, int& seq, int& sid, std::string& cmd)
    {
        const size_t a = line.find('|');
        const size_t b = a == std::string::npos ? std::string::npos : line.find('|', a + 1);
        if (b == std::string::npos) return false;
        std::string s0 = line.substr(0, a);
        const std::string s1 = line.substr(a + 1, b - a - 1);
        cmd = trim(line.substr(b + 1));
        if (s0.empty() || s0.find_first_not_of("0123456789") != std::string::npos) return false;
        if (s1.empty() || s1.find_first_not_of("0123456789") != std::string::npos) return false;
        // Lua writes ms-epoch seq (13 digits) -> keep last 9 so it fits int
        if (s0.size() > 9) s0 = s0.substr(s0.size() - 9);
        seq = to_int(s0, -1);
        sid = to_int(s1, -1);
        return seq >= 0 && sid >= 0 && !cmd.empty();
    }
    // search radius (m) around the anchor from the existing settings: radius, clamped by max_radius. <= 0 = unlimited (radius unset).
    constexpr double MINE_BELT_NEAR_M = 50000.0;   // default warp_range (m): belt mode, rocks beyond it count as none -> warp
    double mine_radius_m(const Config& c)
    {
        const bool belt = !(c.belt.empty() || c.belt == "none");
        double near_m = parse_distance(c.warp_range);
        if (near_m <= 0.0) near_m = MINE_BELT_NEAR_M;
        double r = parse_distance(c.radius);
        const double mx = parse_distance(c.max_radius);
        if (r <= 0.0) return belt ? near_m : -1.0;
        if (mx > 0.0 && r > mx) r = mx;
        if (belt && r > near_m) r = near_m;   // a rock farther than this is not worth the flight, warp instead
        return r;
    }
    // WHAT: true when the belt setting means "never warp" (empty or "none").
    bool belt_off(const std::string& b) { return b.empty() || b == "none"; }
    // WHAT: squared distance between two chunk locations.
    double csq(const int64_t a[3], const int64_t b[3])
    {
        double s = 0;
        for (int i = 0; i < 3; ++i) { const double d = static_cast<double>(a[i] - b[i]); s += d * d; }
        return s;
    }

    // WHAT: result text is a success ("ok" or "ok: ...").
    // WHY: job, move and undock results are all text; one test keeps the checks the same everywhere.
    bool ok_res(const std::wstring& r) { return r.rfind(L"ok", 0) == 0; }

    // WHAT: belt setting as a wanted sector id: digits -> that number, anything else -> 0 (no sector wanted).
    int belt_want(const std::string& belt)
    {
        const bool digits = !belt.empty() && belt.find_first_not_of("0123456789") == std::string::npos;
        return digits ? std::atoi(belt.c_str()) : 0;
    }

    // WHAT: true when chunk ch lies inside the sector box (centre sec_ch, half extents he) on all three axes.
    bool sector_contains(const int64_t sec_ch[3], const int64_t he[3], const int64_t ch[3])
    {
        for (int i = 0; i < 3; ++i) if (std::llabs(ch[i] - sec_ch[i]) > he[i]) return false;
        return true;
    }

    // WHAT: belt target sort score, lower = better: the wanted sector -2, the current sector -1, any other sector its squared chunk distance.
    double belt_score(int id, int want, int cur, double dist_sq)
    {
        return id == want ? -2.0 : (id == cur ? -1.0 : dist_sq);
    }

    // WHAT: warp fuel items for a round trip over dist_chunks chunks. 1 chunk = 2.048 km, 1 warp item = 50 km, there and back (2x).
    long long warp_fuel_need(double dist_chunks)
    {
        return 2LL * static_cast<long long>(std::ceil(dist_chunks * 2.048 / 50.0));
    }

    // WHAT: round-trip fuel rule. Same system: warp_fuel_need(dist) <= drive. Other system: 2 * jump_cost <= drive.
    bool warp_fuel_ok(bool same_system, double dist_chunks, long long jump_cost, long long drive)
    {
        return same_system ? warp_fuel_need(dist_chunks) <= drive : 2LL * jump_cost <= drive;
    }

    // WHAT: dock trip warps when it is longer than km kilometres. km <= 0 turns the trip warp off; dist_m <= 0 never warps (no divide by zero in the standoff).
    inline bool trip_warp_wanted(double dist_m, int km) { return km > 0 && dist_m > 0.0 && dist_m > km * 1000.0; }

    // WHAT: far trip warp only inside one sector: both sector ids known (> 0) and equal.
    // WHY: a warp across sectors is not wanted; an unknown sector (0) on either side skips the warp (the ship flies as before).
    inline bool trip_same_sector(int ship_sec, int dock_sec) { return ship_sec > 0 && ship_sec == dock_sec; }
    // WHAT: the two sectors conflict only when both are known and different. An unknown sector (0, open space outside the named sectors) never blocks a warp.
    // WHY: the rule is warp when the flight is longer than warp_trip_km; open space has no sector, so it must not refuse the warp.
    inline bool sectors_conflict(int a, int b) { return a > 0 && b > 0 && a != b; }

    // WHAT: dock-job timeout clock reset. best_m = distance at last reset (<0 = none yet). now_m <= 0 = unreadable, ignored.
    // Returns true (and sets best_m) on the first reading or when the ship got >= step_m closer since the last reset.
    inline bool trip_progress(double& best_m, double now_m, double step_m = 500.0)
    {
        if (now_m <= 0.0) return false;
        if (best_m < 0.0 || best_m - now_m >= step_m) { best_m = now_m; return true; }
        return false;
    }

    constexpr double WARP_SETTLE_MIN_S = 2.0, WARP_SETTLE_MAX_S = 30.0, WARP_ARRIVE_M = 1000.0;
    // WHAT: far-trip warp settled (dock job stage 4): at least WARP_SETTLE_MIN_S, then the ship reads within WARP_ARRIVE_M of the dock (dock_dist_m >= 0) or WARP_SETTLE_MAX_S has passed.
    // WHY: the dock order is issued only after the warp has landed (no order runs during the teleport).
    inline bool warp_settle_done(double t, double dock_dist_m)
    {
        if (t < WARP_SETTLE_MIN_S) return false;
        return (dock_dist_m >= 0.0 && dock_dist_m <= WARP_ARRIVE_M) || t >= WARP_SETTLE_MAX_S;
    }

    // ---------- far-trip warp for our flights (WARPFLIGHT) ----------
    constexpr double WARP_LAND_M = 300.0;            // a flight warp lands this far short of its destination (m)
    constexpr double WARP_LAND_SIDE_M = 200.0;       // landing candidates 2 and 3: this far sideways from the base landing point (m)
    // WHAT: why a flight of ours warps or flies. Order: off (km 0), near (inside the trip distance), autopilot, travelling, docked, piloted (player flies),
    //       other system, other sector (both known and different), no warp fuel, else warp. Autopilot and travel never warp. Unknown sectors do not block.
    enum FlightWhy { FW_WARP = 0, FW_NEAR = 1, FW_OFF = 2, FW_AUTOPILOT = 3, FW_DOCKED = 4, FW_TRAVEL = 5, FW_PILOTED = 6,
                     FW_OTHER_SYSTEM = 7, FW_OTHER_SECTOR = 8, FW_NO_FUEL = 9, FW_LANDING = 10 };
    inline FlightWhy flight_warp_why(double dist_m, int km, bool same_sys, int ship_sec, int dest_sec, bool docked, bool travelling,
                                     bool autopilot, bool piloted, long long drive, double dist_chunks)
    {
        if (km <= 0) return FW_OFF;
        if (!trip_warp_wanted(dist_m, km)) return FW_NEAR;
        if (autopilot) return FW_AUTOPILOT;
        if (travelling) return FW_TRAVEL;
        if (docked) return FW_DOCKED;
        if (piloted) return FW_PILOTED;
        if (!same_sys) return FW_OTHER_SYSTEM;
        if (sectors_conflict(ship_sec, dest_sec)) return FW_OTHER_SECTOR;
        if (!warp_fuel_ok(true, dist_chunks, 0, drive)) return FW_NO_FUEL;
        return FW_WARP;
    }
    // WHAT: the destination cannot be reached by our flight at all here (other system or other sector): a sweep ore like that is skipped.
    inline bool flight_unreachable(FlightWhy w) { return w == FW_OTHER_SYSTEM || w == FW_OTHER_SECTOR; }
    inline const char* flight_warp_name(FlightWhy w)
    {
        switch (w)
        {
        case FW_WARP: return "warp";
        case FW_NEAR: return "near";
        case FW_OFF: return "warp off";
        case FW_AUTOPILOT: return "game autopilot";
        case FW_DOCKED: return "docked";
        case FW_TRAVEL: return "travelling";
        case FW_PILOTED: return "player flies";
        case FW_OTHER_SYSTEM: return "other system";
        case FW_OTHER_SECTOR: return "other sector";
        case FW_NO_FUEL: return "no warp fuel";
        default: return "landing blocked";
        }
    }
    // WHAT: mining stops (leftover collect, then unload) when the cargo fill reaches this % (was 100). Also the CHECK rule, so the ship does not undock again at once.
    constexpr int CARGO_STOP_PCT = 90;

    // WHAT: cargo fill % from slot counts: other / (isz - fuel) * 100, clamped 0..100. 0 when no non-fuel slot is used, -1 when the cargo has no free slot base.
    int cargo_pct_calc(int isz, int fuel_s, int other_s)
    {
        if (other_s == 0) return 0;
        const int64_t den = static_cast<int64_t>(isz) - fuel_s;
        if (den <= 0) return -1;
        return static_cast<int>(std::min<int64_t>(100, static_cast<int64_t>(other_s) * 100 / den));
    }

    // WHAT: fuel in cargo is below the refuel threshold. have < 0 = unreadable, never low.
    bool fuel_low(const FuelCfg& f, int64_t have)
    {
        return have >= 0 && have < f.threshold;
    }

    // WHAT: ore gained since the last sample: inv - last when it went up, else 0.
    int64_t got_delta(int64_t inv, int64_t last)
    {
        return inv > last ? inv - last : 0;
    }

    // WHAT: queue row target reached: amount > 0 and got >= amount (amount 0 = unlimited, never done).
    bool row_done(int amount, int64_t got)
    {
        return amount > 0 && got >= amount;
    }

    // WHAT: leftover-ore collect (MS_COLLECT) caps: total seconds and seconds with no ore count drop.
    // WHY: the tractor pulls floating ore in; a stuck or unreachable piece must not hold the ship forever.
    constexpr double COLLECT_MAX_S = 30.0;      // collect timeout (s)
    constexpr double COLLECT_STALL_S = 8.0;     // seconds with no drop in the nearby ore count -> give up (s)
    constexpr double COLLECT_SCAN_S = 2.0;      // ore count scan period (s): the scan is slow, see ore_near
    constexpr double COLLECT_RANGE_M = 300.0;   // floating ore within this range counts as leftover (m)
    enum CollectVerdict { COL_WAIT = 0, COL_DONE, COL_GIVEUP };

    // WHAT: collect step verdict from the floating ore count n, elapsed seconds and stall seconds. n <= 0 = done; timeout or stall = give up; else wait.
    CollectVerdict collect_verdict(int n, double elapsed, double stall)
    {
        if (n <= 0) return COL_DONE;
        if (elapsed >= COLLECT_MAX_S || stall >= COLLECT_STALL_S) return COL_GIVEUP;
        return COL_WAIT;
    }

    // WHAT: idle watchdog limit (s) for MS_MINE. idle_s 0 = off (returned as is). While the rock is alive: at least IDLE_ROCK_MIN_S.
    // WHY: ore comes in bursts, a 5 s gap is not a spent rock; only a rock that is gone or a long silence ends the mining.
    constexpr double IDLE_ROCK_MIN_S = 30.0;
    inline double idle_limit(double idle_s, bool rock_alive)
    {
        return (idle_s > 0.0 && rock_alive) ? std::max(idle_s, IDLE_ROCK_MIN_S) : idle_s;
    }

    // WHAT: sweep (MS_SWEEP) limits: pick timeout (s), rescans before giving up, fly-back max distance (m), fly timeout (s), warp wait (s).
    constexpr double SWEEP_PICK_S = 10.0;
    constexpr int SWEEP_RESCANS = 2;
    constexpr double SWEEP_FLY_MAX_M = 4096.0;
    constexpr double SWEEP_FLY_TIMEOUT_S = 120.0;
    constexpr double SWEEP_RESCAN_S = 2.0;   // wait before each rescan of the ore list (s)

    // WHAT: fly back by tractor flight only when same system and within SWEEP_FLY_MAX_M; else warp.
    // WHAT: return warp decision (SW_WARP). Arrived: same system, within WARP_ARRIVE_M of the saved point, after WARP_SETTLE_MIN_S. Give up: no arrival
    //       after WARP_SETTLE_MAX_S. Else wait. dist_m < 0 = position unreadable while jumping; same_sys false = jumping (system 0): still travelling.
    // WHY: the 15 s wait was too short for a jump and a jump reads system 0, so the warp was called a failure (live 2026-11-04).
    enum SweepArrive { SA_WAIT = 0, SA_ARRIVED = 1, SA_GIVE_UP = 2 };
    inline int sweep_arrive(double t, double dist_m, bool same_sys)
    {
        const bool near = same_sys && dist_m >= 0.0 && dist_m <= WARP_ARRIVE_M;
        if (near && t >= WARP_SETTLE_MIN_S) return SA_ARRIVED;
        return t >= WARP_SETTLE_MAX_S ? SA_GIVE_UP : SA_WAIT;
    }

    inline bool sweep_fly_back(bool same_sys, double dist_m)
    {
        return same_sys && dist_m <= SWEEP_FLY_MAX_M;
    }

    // WHAT: index of the smallest d[i] with done[i] == 0, or -1 when none.
    inline int sweep_nearest(const std::vector<double>& d, const std::vector<char>& done)
    {
        int best = -1;
        for (size_t i = 0; i < d.size(); ++i)
            if (!done[i] && (best < 0 || d[i] < d[(size_t)best])) best = (int)i;
        return best;
    }

    // WHAT: true when the shared ore scan must run: forced, never run (last_ms 0), or the last run is period_ms old or older.
    inline bool ore_scan_due(bool force, uint64_t last_ms, uint64_t now_ms, uint64_t period_ms)
    {
        return force || last_ms == 0 || now_ms - last_ms >= period_ms;
    }

    // WHAT: next queue row index after idx for n rows. wrapped = true when it passed the last row (result 0).
    int item_next(int idx, int n, bool& wrapped)
    {
        const int i = idx + 1;
        wrapped = i >= n;
        return wrapped ? 0 : i;
    }

    // WHAT: hull dropped below the baseline. baseline < 0 = no baseline yet, never a drop.
    bool hull_dropped(int32_t baseline, int32_t h)
    {
        return baseline >= 0 && h < baseline;
    }

    // WHAT: destroyed-ship debounce. A scan with the destroyed signal adds 1 to the ship's count, a scan without it sets 0.
    // WHY: one scan can read a transient value (hull 0, ship missing from the list), so a ship counts as destroyed only after SHIP_DEAD_SCANS scans in a row.
    // signal A (hull Health 0) = SHIP_DEAD_SCANS; signal B (missing from FindAllOf) needs SHIP_MISSING_SCANS: a list read can miss a ship for a scan or two.
    constexpr int SHIP_DEAD_SCANS = 2;
    constexpr int SHIP_MISSING_SCANS = 3;
    inline int dead_scan_next(int prev, bool signal)
    {
        return signal ? prev + 1 : 0;
    }
    inline bool dead_confirmed(int count, int need = SHIP_DEAD_SCANS)
    {
        return count >= need;
    }

    // WHAT: TArray Num read from game memory, clamped to 0..100000 before it sizes a buffer.
    // WHY: a stale or wrong array count must not allocate or index past the array.
    constexpr int32_t TARRAY_NUM_MAX = 100000;
    inline int32_t clamp_tarray_num(int32_t n) { return std::clamp(n, static_cast<int32_t>(0), TARRAY_NUM_MAX); }

    // WHAT: one TargettedBy entry is hostile. Our own bench ship: never. A readable entry: hostile unless it shares the ship's owner or faction.
    //       An unreadable entry: hostile only when the array element type is a known object type (no owner can be read from it).
    // WHY: an entry that shares our owner/faction is not an attacker; an unreadable non-object element is not counted either.
    inline bool target_entry_hostile(bool live, bool own, bool same_side, bool object_type)
    {
        if (own) return false;
        return live ? !same_side : object_type;
    }

    // WHAT: a TargettedBy entry counts for the flee only when it is within range_m (m) of the ship. dist_m < 0 = unreadable: counts as near (conservative).
    inline bool target_entry_near(double dist_m, double range_m) { return dist_m < 0.0 || dist_m <= range_m; }

    // ---------- hazard flee: hull drop with no hostile targeter ----------
    // WHAT: the flee is a hazard flee when the hull drop had no hostile targeter at the sample (the damage has no attacker).
    inline bool hazard_flee(int hostile_n) { return hostile_n <= 0; }
    // WHAT: distance in metres between two objects from chunk (int64 x3) and raw local loc (x3, raw units); raw_per_m = g_raw_per_m, chunk_m = DOCK_CHUNK_M.
    inline double abs_dist_m(const int64_t ca[3], const double la[3], const int64_t cb[3], const double lb[3], double raw_per_m, double chunk_m)
    {
        double s = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            const double d = (static_cast<double>(ca[i]) - static_cast<double>(cb[i])) * chunk_m + (la[i] - lb[i]) / raw_per_m;
            s += d * d;
        }
        return std::sqrt(s);
    }
    // WHAT: hit with no attacker at all: no near hostile, no far hostile, IsShipInCombat false. Anything targeting the ship makes the hit an attack.
    inline bool hazard_flee_full(int hostile_near, int hostile_far, bool combat) { return hostile_near <= 0 && hostile_far <= 0 && !combat; }
    constexpr double HAZARD_STUCK_MPS = 1.0;     // speed below this = not moving (m/s)
    constexpr size_t HAZARD_STUCK_N = 3;         // speed samples (1 s apart) below HAZARD_STUCK_MPS in a row = stuck
    // WHAT: last HAZARD_STUCK_N speed samples (oldest first) all below HAZARD_STUCK_MPS.
    inline bool speed_stuck(const std::vector<double>& hist)
    {
        if (hist.size() < HAZARD_STUCK_N) return false;
        for (size_t i = hist.size() - HAZARD_STUCK_N; i < hist.size(); ++i)
            if (hist[i] >= HAZARD_STUCK_MPS) return false;
        return true;
    }
    // WHAT: hull lost per second over the samples (1 s apart, oldest first): (first - last) / (n - 1). 0 with fewer than 2 samples or no loss.
    inline double hull_rate_per_s(const std::vector<int32_t>& hist)
    {
        if (hist.size() < 2) return 0.0;
        const double d = static_cast<double>(hist.front() - hist.back()) / static_cast<double>(hist.size() - 1);
        return d > 0.0 ? d : 0.0;
    }
    constexpr double HAZARD_MOVE_AFTER_S = 5.0;  // hull still dropping this long after the flee began: move away (no hold)
    constexpr double HAZARD_DROP_FRESH_S = 2.0;  // a hull drop this recent counts as "still dropping"
    constexpr double HAZARD_RADIUS_M = 5000.0;   // move-away point: this far from the damage start (m); circling keeps the ship >= 2 km away
    constexpr double HAZARD_RETRY_S = 1.0;       // a refused or finished move-away is re-issued after this long (s)
    // WHAT: a fleeing ship with no holding spot yet (slot_ok false), not yet holding (hold_ph != 2), no move/dock job, and the retry wait over must start a move-away.
    inline bool flee_idle_move_due(bool slot_ok, int hold_ph, double hz_t, bool move_busy, bool dock_busy)
    {
        return !slot_ok && hold_ph != 2 && hz_t >= HAZARD_RETRY_S && !move_busy && !dock_busy;
    }
    // WHAT: the move-away is due: 5 s or more since the flee began, a hull drop in the last 2 s, and no move or dock job runs.
    inline bool hazard_move_due(double flee_t, double drop_age, bool move_busy, bool dock_busy)
    {
        return flee_t >= HAZARD_MOVE_AFTER_S && drop_age < HAZARD_DROP_FRESH_S && !move_busy && !dock_busy;
    }
    // WHAT: direction k of the move-away circle: base turned k * 90 degrees about Z (XY plane; z kept). k = 0 is base.
    inline void hazard_dir(const double base[3], int k, double out[3])
    {
        const int q = ((k % 4) + 4) % 4;
        double x = base[0], y = base[1];
        for (int i = 0; i < q; ++i) { const double nx = -y; y = x; x = nx; }   // +90 degrees per step
        out[0] = x; out[1] = y; out[2] = base[2];
    }

    // ---------- shields: drop rule shared with hull ----------
    // WHAT: shield lost since the last sample. baseline < 0 or s < 0 (unreadable) = never a drop. A rise (regeneration) is never a drop.
    // WHY: the baseline is the last sample, so regeneration just raises it and the next loss is measured from there.
    inline bool shield_dropped(int32_t baseline, int32_t s) { return s >= 0 && hull_dropped(baseline, s); }
    // WHAT: whole shield units from the GetShields double. Negative / NaN = -1 (unreadable). Floors, so regeneration never looks like a drop; clamps to INT32_MAX.
    inline int32_t shield_whole(double v) { if (!(v >= 0.0)) return -1; if (v > 2147483647.0) return 2147483647; return static_cast<int32_t>(v); }
    // WHAT: flee dock candidates. With cargo the cargo candidates stay; with empty cargo (no ore lists match) every ore dock list, then refuel docks, in order, no duplicates.
    inline std::vector<int> flee_fallback_docks(std::vector<int> cargo_cands,
            const std::vector<std::vector<int>>& ore_docks, const std::vector<int>& refuel)
    {
        if (!cargo_cands.empty()) return cargo_cands;
        std::vector<int> out;
        auto add = [&](int d) { if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d); };
        for (const auto& v : ore_docks) for (int d : v) add(d);
        for (int d : refuel) add(d);
        return out;
    }
    enum DmgKind { DMG_NONE = 0, DMG_HULL = 1, DMG_SHIELD = 2 };
    // WHAT: which damage a sample shows: hull first, then shield. A docked ship never counts (the drop is not a hit in space).
    inline DmgKind damage_drop_kind(int32_t hull_base, int32_t h, int32_t sh_base, int32_t s, bool docked)
    {
        if (docked) return DMG_NONE;
        if (hull_dropped(hull_base, h)) return DMG_HULL;
        if (shield_dropped(sh_base, s)) return DMG_SHIELD;
        return DMG_NONE;
    }

    // ---------- clear-path check for approaches (segment ship -> stand-off point against rock spheres) ----------
    // Clearance of the path check is the config key path_clearance_m (Config, default 8 m). A rock blocks when its centre is closer than
    // block_r_m(rock radius, clearance) to the segment. The rock radius is scaled by ROCK_BLOCK_SCALE (rocks are not perfect spheres).
    constexpr double ROCK_BLOCK_SCALE = 0.8;         // blocking radius = rock radius x this + clearance (registry data unchanged)
    constexpr double HAZARD_CLEAR_EXTRA_M = 100.0;   // hazard rocks: extra clearance on top of the path clearance (m)
    constexpr double PATH_BLOCK_PENALTY_M = 1500.0;  // retarget scoring: a candidate whose straight path is blocked is this many m farther away
    constexpr double SLOW_ZONE_M = 500.0;            // fast-sample range: a rock this close (surface, m) to a ship gets the fast hull/shield watch
    constexpr double HAZARD_BLOCK_M = 1500.0;        // hazard hit: rocks this close (surface, m) are marked; the back-out distance is HAZARD_BACKOUT_M
    constexpr double HAZARD_BACKOUT_M = 1500.0;
    constexpr int PATH_WP_MAX = 1;                   // waypoints per approach before the target is skipped for this visit (one plan, 4 candidates)
    constexpr double APPR_CAP_S = 60.0;              // a target not mined within this many s of its first path event is skipped (s)
    constexpr double PATH_CHECK_S = 1.0;             // clear-path re-check period while an approach runs (s)
    constexpr double FAST_SAMPLE_S = 0.25;           // hull/shield sample period near rock (s); elsewhere 1 s
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

    // WHAT: distance from point c to the SEGMENT a->b (clamped to the ends). Used by the bubble and path checks of the registry (the ship end counts).
    inline double seg_point_dist(const double a[3], const double b[3], const double c[3])
    {
        const double d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const double w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const double t = std::clamp(l2 < 1e-12 ? 0.0 : (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / l2, 0.0, 1.0);
        const double p[3] = {a[0] + d[0] * t, a[1] + d[1] * t, a[2] + d[2] * t};
        return std::sqrt((c[0] - p[0]) * (c[0] - p[0]) + (c[1] - p[1]) * (c[1] - p[1]) + (c[2] - p[2]) * (c[2] - p[2]));
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

    // ---------- path check cost limits (perf) ----------
    constexpr double PATH_RMAX_M = 5000.0;        // rocks whose centre is farther than this from the path are never tested (prefilter)
    constexpr size_t PATH_CANDIDATES = 8;         // retarget: nearest candidates of the ore tried
    constexpr double PATH_PICK_M = 10000.0;       // retarget: candidates are rocks of the ore within this distance of the ship (m)
    constexpr double REG_RAD_UNKNOWN_M = 35.0;    // blocking radius of a registry rock whose radius is not read yet (conservative, m)
    constexpr double PATH_WATCH_S = 2.0;          // per-ship clear-path re-check period while an approach runs (s)
    constexpr double PATH_SLOW_MS = 3.0;          // one check over this many ms is slow
    constexpr double PATH_SLOW_LOG_S = 30.0;      // at most one 'PATH slow' line per this many seconds
    constexpr double PATH_SLOW_OFF_S = 10.0;      // a ship whose check was slow skips its periodic checks this long

    // WHAT: true when centre c lies outside the box of segment a->b grown by rmax on every side (cannot be within rmax of the segment).
    // WHY: cheap first test; the distance test only runs on the rocks that pass it.
    inline bool path_box_reject(const double a[3], const double b[3], const double c[3], double rmax)
    {
        for (int i = 0; i < 3; ++i)
        {
            const double lo = std::min(a[i], b[i]) - rmax, hi = std::max(a[i], b[i]) + rmax;
            if (c[i] < lo || c[i] > hi) return true;
        }
        return false;
    }
    // WHAT: a check that took ms milliseconds is over budget (PATH_SLOW_MS).
    inline bool path_over_budget(double ms) { return ms > PATH_SLOW_MS; }
    // WHAT: the 'PATH slow' line may be written: never written (last 0) or PATH_SLOW_LOG_S or more since the last one (ms, GetTickCount64 clock).
    inline bool path_slow_log_due(uint64_t last_ms, uint64_t now_ms) { return last_ms == 0 || now_ms - last_ms >= static_cast<uint64_t>(PATH_SLOW_LOG_S * 1000.0); }
    // WHAT: a slow check pauses the ship's periodic checks until this time (ms).
    inline uint64_t path_skip_until(uint64_t now_ms) { return now_ms + static_cast<uint64_t>(PATH_SLOW_OFF_S * 1000.0); }
    // WHAT: the periodic checks of a ship are paused at now_ms (until_ms = path_skip_until of its last slow check).
    inline bool path_skip_active(uint64_t until_ms, uint64_t now_ms) { return now_ms < until_ms; }
    // WHAT: a ship's periodic clear-path check is due after PATH_WATCH_S of approach time.
    inline bool path_watch_due(double t) { return t >= PATH_WATCH_S; }

    // ---------- bubble: reflex brake before a rock on the nose (mb_bubble.hpp) ----------
    constexpr double BUBBLE_LOOK_MIN_M = 100.0;      // look-ahead never shorter than this (m)
    constexpr double BUBBLE_LOOK_S = 4.0;            // look-ahead = speed x this (s), along the nose
    constexpr double BUBBLE_BRAKE_V = 3.0;           // braking ends below this speed (m/s)
    constexpr int BUBBLE_MAX_REPLAN = 2;             // replans per target before the target is given up
    constexpr double BUBBLE_SHIP_R_M = 12.0;         // ship CollisionRadius when the game read fails (m)
    constexpr double BUBBLE_SLOW_MS = 2.0;          // one ship's bubble pass over this many ms is slow ('BUBBLE slow')

    // WHAT: look-ahead length (m) for speed_mps (m/s): speed x BUBBLE_LOOK_S, at least BUBBLE_LOOK_MIN_M.
    inline double bubble_look_ahead(double speed_mps) { return std::max(BUBBLE_LOOK_MIN_M, std::max(0.0, speed_mps) * BUBBLE_LOOK_S); }
    // WHAT: clearance (m) for a ship of collision radius ship_r (m) and bubble clearance clr (m, config bubble_clearance_m). The rock block radius is added by block_r_m.
    inline double bubble_clear_m(double ship_r, double clr) { return ship_r + clr; }

    // ---------- blocking geometry and waypoint plan (stage tune) ----------
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
    // WHAT: retarget score of a candidate rock: its distance (m) plus PATH_BLOCK_PENALTY_M when its straight path is blocked. Lower is better.
    inline double pick_score(double dist_m, bool blocked) { return dist_m + (blocked ? PATH_BLOCK_PENALTY_M : 0.0); }
    // WHAT: landing point candidate k (0..3) of a warp toward T (m) along unit dir (ship -> T): 0 = WARP_LAND_M short of T, 1 = 2 x WARP_LAND_M short,
    //       2 / 3 = the base point sideways by WARP_LAND_SIDE_M on either side.
    inline void landing_candidate(int k, const double T[3], const double dir[3], double out[3])
    {
        double base[3];
        for (int i = 0; i < 3; ++i) base[i] = T[i] - dir[i] * WARP_LAND_M;
        if (k == 0) { for (int i = 0; i < 3; ++i) out[i] = base[i]; }
        else if (k == 1) { for (int i = 0; i < 3; ++i) out[i] = T[i] - dir[i] * 2.0 * WARP_LAND_M; }
        else side_point(base, dir, WARP_LAND_SIDE_M, k == 2 ? 1 : -1, out);
    }
    // WHAT: a point at dist (m) from a rock centre is inside the rock block radius (block_r_m(rad, clear)).
    inline bool point_blocked(double dist_m, double rad_m, double clear_m) { return dist_m < block_r_m(rad_m, clear_m); }

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
    // WHAT: the approach of a target has run APPR_CAP_S since its first path event (first_ms, 0 = no event yet). now_ms in GetTickCount64 ms.
    inline bool appr_capped(uint64_t first_ms, uint64_t now_ms) { return first_ms != 0 && now_ms - first_ms >= static_cast<uint64_t>(APPR_CAP_S * 1000.0); }

    // WHAT: bubble ladder action. NONE = nothing; BRAKE = hit seen, throttle cut (the log line names the next action); REPLAN = after braking,
    //       path_guard against the target with the blocker skipped; RETARGET = after BUBBLE_MAX_REPLAN replans, the target is given up.
    enum BubbleAct { BBA_NONE = 0, BBA_BRAKE = 1, BBA_REPLAN = 2, BBA_RETARGET = 3 };
    // WHAT: ladder state of one ship. braking = throttle held at 0 until speed < BUBBLE_BRAKE_V. pending = action after the stop.
    //       replans = replans done for rock. rock = the target the count belongs to (a new target starts at 0).
    //       blk / blk_d = last blocker id and its distance to the nose line (m), kept for the replan and the log.
    //       phase = flight phase of the last hit. wp = waypoints used in this flight. resume = a waypoint is flying; dst = the flight destination (m)
    //       to fly again when the waypoint ends (WAYPOINT and ORE phases). why = last activation reason (status line only).
    struct BubbleState { bool braking = false; int replans = 0; int pending = BBA_NONE; int rock = 0; int blk = 0; double blk_d = 0.0;
                         int prev_id = 0; double prev_d = 0.0; int phase = 0; int wp = 0; bool resume = false; double dst[3] = {0, 0, 0}; int why = 4; };   // phase: BubblePhase, why: BubbleWhy (4 = BW_IDLE)
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

    // ---------- path2: replan gate, rock-lost counting, start grace, strikes, bubble closing ----------
    constexpr uint64_t REPLAN_COOLDOWN_MS = 20000;   // one waypoint plan per target per this many ms
    constexpr uint64_t REPLAN_LOG_MS = 30000;        // 'PATH replan suppressed' at most this often
    constexpr double LOST_PROGRESS_M = 50.0;         // approach progress (m) that starts the rock-lost count again
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
    // WHAT: rock-lost count after an approach result. Only a result 'rock lost' with the rock actor really gone counts; our own cancels
    //       (own_cancel) and a rock still there never count (they are free retries).
    inline int rock_lost_next(int n, bool result_rock_lost, bool rock_gone, bool own_cancel)
    {
        return (result_rock_lost && rock_gone && !own_cancel) ? n + 1 : n;
    }
    // WHAT: approach progress since the reference point is enough to start the rock-lost count again (moved_m > LOST_PROGRESS_M).
    inline bool lost_progress(double moved_m) { return moved_m > LOST_PROGRESS_M; }
    // WHAT: start grace. A blocker whose centre is within ship radius + rock radius + START_GRACE_M of the ship start is ignored when the
    //       ship moves away from it (away) or when the first 60 m of the segment are inside its block radius (first60_inside).
    inline bool grace_ignore(double start_d, double ship_r, double rock_r, bool away, bool first60_inside)
    {
        return start_d < ship_r + rock_r + START_GRACE_M && (away || first60_inside);
    }
    // WHAT: the same blocker blocked the same target this many times: skip the target.
    inline bool strikes_reached(int n) { return n >= STRIKE_MAX; }
    // WHAT: bubble brake only for a blocker ahead (cos_ahead > 0) and closing. Closing: range to it fell since the last pass (same blocker),
    //       or first sight while moving (speed > 0). Beside, behind or opening: no brake.
    inline bool bubble_brake_ok(double cos_ahead, double dist_now, double dist_prev, bool same_prev, double speed_mps)
    {
        if (cos_ahead <= 0.0) return false;
        if (!same_prev) return speed_mps > 0.0;
        return dist_now < dist_prev - 0.01;
    }

    // ---------- bubble scope: which ships, which phase, which fix (stage 2b) ----------
    constexpr int BUBBLE_MAX_WP = 2;                 // waypoints around a blocker per flight (waypoint and ore phases)
    constexpr double BUBBLE_NUDGE_M = 100.0;         // mining drift nudge: move this far away from the rock (m)
    constexpr double BUBBLE_NEAR_MARGIN_M = 10.0;    // drift nudge when centre distance < ship radius + rock radius + this (m)

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
    // WHAT: a drifting ship is too near a rock: centre distance (m) below ship radius + rock radius + BUBBLE_NEAR_MARGIN_M.
    inline bool bubble_too_near(double centre_m, double ship_r, double rock_r) { return centre_m < ship_r + rock_r + BUBBLE_NEAR_MARGIN_M; }

    // WHAT: stand-off point on the line from the rock centre c toward the ship a, at distance dist (m) from c. Fallback direction +X when a == c.
    inline void standoff_point(const double a[3], const double c[3], double dist, double out[3])
    {
        double d[3] = {a[0] - c[0], a[1] - c[1], a[2] - c[2]};
        const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n < 1e-9) { d[0] = 1.0; d[1] = 0.0; d[2] = 0.0; }
        else for (double& x : d) x /= n;
        for (int i = 0; i < 3; ++i) out[i] = c[i] + d[i] * dist;
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

    // WHAT: a rock is sampled at the fast period (FAST_SAMPLE_S) when its surface distance to the ship is within SLOW_ZONE_M.
    inline bool fast_sample_due(double surface_m) { return surface_m <= SLOW_ZONE_M; }

    // ---------- combat: target flee trigger and combat log text (mb_mining.hpp samples the game, mb_common.hpp writes the file) ----------

    // WHAT: consecutive target samples with a hostile targeter (a sample with none sets 0).
    // WHY: one read can catch a stale entry; the flee needs TARGET_FLEE_READS reads in a row (about 1 s apart, the sample cadence).
    constexpr int TARGET_FLEE_READS = 2;
    inline int target_streak_next(int prev, int hostile_n)
    {
        return hostile_n > 0 ? prev + 1 : 0;
    }
    // WHAT: flee trigger from a target sample: flee_on_target on, not latched by an earlier flee of this attack, and the hostile-targeter streak reached TARGET_FLEE_READS.
    // WHY: IsShipInCombat() is not a trigger (our own lasers vs rocks may set it); it is logged only. The latch means one flee per attack.
    inline bool target_flee_trigger(bool enabled, int streak, bool latched)
    {
        return enabled && !latched && streak >= TARGET_FLEE_READS;
    }
    // WHAT: the latch stays set only while the ship is still attacked (targeted or in combat); a sample with no attack clears it.
    inline bool target_latch_next(bool latched, bool attacked)
    {
        return attacked && latched;
    }
    // WHAT: text of a log file after the size cap: unchanged up to cap bytes, past it the older half is dropped (cut after the first newline past the middle).
    // WHY: MiningCombat.txt must not grow without limit in a long session.
    inline std::string combat_log_trim(const std::string& txt, size_t cap)
    {
        if (txt.size() <= cap) return txt;
        const size_t nl = txt.find('\n', txt.size() / 2);
        return nl == std::string::npos ? std::string() : txt.substr(nl + 1);
    }
    // WHAT: one attacker entry of a combat line: "#idx class id=.. name='..' owner='..' dist=..m" (+ " own" for our bench ships). dist_m < 0 = n/a.
    inline std::wstring attacker_text(int idx, const std::wstring& cls, int id, const std::wstring& name, const std::wstring& owner, double dist_m, bool own)
    {
        std::wstring s = L"#" + std::to_wstring(idx) + L" " + cls + L" id=" + std::to_wstring(id) + L" name='" + name + L"' owner='" + owner + L"' dist=";
        s += dist_m < 0.0 ? std::wstring(L"n/a") : std::to_wstring(static_cast<long long>(dist_m)) + L"m";
        return own ? s + L" own" : s;
    }

    // WHAT: dock time limit rounds used (n): from MINE_DOCK_RETRIES on, the next limit hit undocks and goes idle.
    constexpr int MINE_DOCK_RETRIES = 10;   // dock time limit: undock/wait/redock rounds before the ship undocks and goes idle
    bool dock_limit_giveup(int n)
    {
        return n >= MINE_DOCK_RETRIES;
    }

    // WHAT: unload chain goes on when cargo of docked ores is left (now > 0) and it shrank since the last hop (prev < 0 = first hop).
    inline bool unload_chain_more(int64_t prev, int64_t now)
    {
        return now > 0 && (prev < 0 || now < prev);
    }

    // WHAT: unload candidates for a dock trip: ORE indices that are in cargo AND have an unload dock list (ore order).
    // WHY: a trip only picks a dock for an ore it carries. An ore not in cargo must never pick the dock: the ship docks, unloads nothing, the cargo check fails.
    std::vector<int> unload_cand(const std::vector<bool>& in_cargo, const std::vector<bool>& has_docks)
    {
        std::vector<int> out;
        for (size_t i = 0; i < in_cargo.size() && i < has_docks.size(); ++i)
            if (in_cargo[i] && has_docks[i]) out.push_back(static_cast<int>(i));
        return out;
    }

    // WHAT: refuel stalled: items not all delivered, t >= wait_s, and the fuel cargo has not grown for idle_s (idle_s 0 = off).
    bool refuel_stalled(bool done, int idle_s, double t, int wait_s, double hold_idle)
    {
        return !done && idle_s > 0 && t >= wait_s && hold_idle >= idle_s;
    }

    // WHAT: a command seq is new when it differs from the last one run. Wrap-around of the 9-digit seq is fine.
    bool cmd_seq_is_new(int seq, int last)
    {
        return seq != last;
    }

    // WHAT: Dev Menu Stations tab: a station is destroyable only if its class is the plain player class. INFERRED from legacy assets (NPC stations are BP_AllyStationObject_C, BP_AbandonedStationObject_01_C, BPO_TradingOutpost_01_C).
    bool station_destroyable(const std::string& cls) { return cls == "BP_StationObject_C"; }

    // WHAT: one DevStations.txt row: id|name|class|platforms|destroyable(1/0). '|', CR and LF in name/class become spaces.
    std::string station_row(int id, std::string name, std::string cls, int platforms)
    {
        for (char& c : name) if (c == '|' || c == '\r' || c == '\n') c = ' ';
        for (char& c : cls) if (c == '|' || c == '\r' || c == '\n') c = ' ';
        return std::to_string(id) + "|" + name + "|" + cls + "|" + std::to_string(platforms) + "|" + (station_destroyable(cls) ? "1" : "0");
    }

    // WHAT: station id from the text after "dev_destroy_station:". Digits only, 1..9 chars, > 0. Else 0.
    int parse_station_id(const std::string& arg)
    {
        if (arg.empty() || arg.size() > 9 || arg.find_first_not_of("0123456789") != std::string::npos) return 0;
        return std::atoi(arg.c_str());
    }

    // ---------- dock network: pure tables and rules (mb_net.hpp keeps the tables filled from the game) ----------
    // WHAT: a ship's wait reason, why a ship is at a dock, and the holder state of a dock user.
    // WHY: one shared queue per dock, FIFO by global ticket; no game objects here so the tests can run it.
    enum NetReason { NR_OK, NR_WAIT_BUSY, NR_WAIT_QUEUE, NR_WAIT_PARKED, NR_WAIT_FULL, NR_WAIT_UNMANAGED, NR_NOT_CONFIGURED, NR_DOCK_GONE, NR_NOTHING_TO_UNLOAD };
    enum Purpose { P_NONE, P_UNLOAD, P_REFUEL, P_UNDOCK_TARGET, P_PICK, P_FLEE };
    enum HolderState { HS_WORKING, HS_PARKED };

    constexpr double NET_DWELL_S = 20.0;   // a working holder gets this long to unload before a waiter can send it away (s)
    constexpr double NET_AGE_S = 600.0;    // a ship that waited this long goes first (aged tier, FIFO by ticket)
    constexpr double NET_CLEAR_S = 60.0;   // a dock reservation not used within this time is given up (s)
    constexpr double NET_SLOT_FIRST_M = 150.0;   // holding slot 1 is this far from the dock (m)
    constexpr double NET_SLOT_STEP_M = 100.0;    // next slot is this much further along the line (m)
    constexpr double NET_SLOT_MIN_GAP_M = 50.0;  // two slot points closer than this clash (m)
    constexpr double FLEE_SLOT_MIN_M = 1000.0;      // a flee holds at least this far from the dock (clear of the station, m)
    constexpr double FLEE_SLOT_MARGIN_M = 300.0;    // added to the farthest other dock of the same station (m)
    constexpr double FLEE_STATION_MAX_M = 3000.0;   // docks farther than this belong to another station (m)

    // what a waiting ship needs: cargo fill, fuel state, hull state, ore for this dock in cargo, distance to the dock (-1 unknown)
    struct NetReq { int cargo_pct = 0; bool fuel_low = false; bool has_dock_ore = false; double dist_m = -1; };
    // dock position learned from a docked ship (mb_net.hpp fills it): system, chunk, local loc (raw units), line direction
    struct DockPos { bool known = false; int sys = 0; int64_t ch[3] = {0, 0, 0}; double loc[3] = {0, 0, 0}; double dir[3] = {1, 0, 0}; bool dir_known = false; bool from_object = false; };
    // slot_sid[k-1] = ship in holding slot k (0 free). line_slots = slot_sid.size(). Dock reservation = resv (no separate cleared field).
    struct NetDock { int id = 0; bool exists = true; int occ = 0; int resv = 0; Purpose why = P_NONE; double since = 0; bool full = false; double full_since = 0; double occ_since = 0; std::vector<int> queue; std::vector<int> slot_sid; int line_slots = 0; };
    struct NetShip { int sid = 0; Purpose intent = P_NONE; std::vector<int> wanted; int waits_dock = 0; int blocked_by = 0; long ticket = 0; double wait_since = 0; NetReason reason = NR_OK; bool granted = false;
                     NetReq req; int slot = 0; double prio = 0; std::string prio_why; int phase = 0; };   // phase: 0 none, 1 flying to slot, 2 holding, 3 cleared
    struct NetVerdict { enum K { GO, WAIT, ERR } k = GO; int dock = 0; int holder = 0; NetReason reason = NR_OK; };

    // WHAT: take ship sid out of every dock queue (ticket stays on the ship until drop_wait).
    inline void net_unqueue(std::map<int, NetDock>& docks, int sid)
    {
        for (auto& kv : docks) { auto& q = kv.second.queue; q.erase(std::remove(q.begin(), q.end(), sid), q.end()); }
    }

    // WHAT: ship stops waiting: out of all queues, ticket and wait fields cleared.
    inline void net_drop_wait(std::map<int, NetDock>& docks, NetShip& s)
    {
        net_unqueue(docks, s.sid);
        s.ticket = 0; s.waits_dock = 0; s.blocked_by = 0; s.wait_since = 0; s.reason = NR_OK;
    }

    // WHAT: add ship to dock d's queue, queue kept sorted by ticket (FIFO).
    inline void net_queue_add(std::map<int, NetDock>& docks, std::map<int, NetShip>& ships, int d, int sid)
    {
        std::vector<int>& q = docks[d].queue;
        if (std::find(q.begin(), q.end(), sid) == q.end()) q.push_back(sid);
        std::stable_sort(q.begin(), q.end(), [&](int a, int b) { return ships[a].ticket < ships[b].ticket; });
    }

    // WHAT: true when a queued ship wants this dock before me: higher prio, or same prio with a lower ticket (a new ship with no ticket is behind equal ones).
    inline bool net_queue_blocks(const NetDock& nd, const std::map<int, NetShip>& ships, const NetShip& me)
    {
        for (int q : nd.queue)
        {
            if (q == me.sid) continue;
            const auto it = ships.find(q);
            if (it == ships.end()) continue;
            const NetShip& o = it->second;
            if (o.prio > me.prio || (o.prio == me.prio && (me.ticket == 0 || o.ticket < me.ticket))) return true;
        }
        return false;
    }

    // WHAT: queue score: aged (wait >= NET_AGE_S, or aged flag) = tier 9 only (ties -> ticket FIFO); else tier 1 + cargo% * 1000 + 500 if fuel low + wait (max 599 s).
    // WHY: fullest cargo first, then low fuel, then the longest waiting; aged ships first, so nobody starves.
    inline double net_score(const NetReq& r, Purpose purpose, double wait_s, bool aged)
    {
        (void)purpose;   // every purpose is the same tier today
        if (aged || wait_s >= NET_AGE_S) return 9.0 * 1000000.0;
        return 1000000.0 + r.cargo_pct * 1000.0 + (r.fuel_low ? 500.0 : 0.0) + std::min(wait_s, NET_AGE_S - 1.0);
    }

    // WHAT: short reason shown for the queue priority: "aged", "cargo 98%", "fuel low", "waiting".
    inline std::string net_prio_why(const NetReq& r, double wait_s, bool aged)
    {
        if (aged || wait_s >= NET_AGE_S) return "aged";
        if (r.cargo_pct > 0) return "cargo " + std::to_string(r.cargo_pct) + "%";
        if (r.fuel_low) return "fuel low";
        return "waiting";
    }

    // WHAT: ship sid wants a dock for purpose. req = what the ship carries and needs (queue score). cands = dock ids in config order; fixed_dock > 0 = use only that dock (dock picker).
    // WHAT (result): GO = dock reserved for sid now; WAIT = queued (dock = the one shown, holder = who uses it); ERR = real error (NOT_CONFIGURED / DOCK_GONE).
    // WHY: free dock + no earlier waiter for it = GO. Full docks are skipped. Busy/reserved/queued = WAIT, never an error.
    inline NetVerdict net_pick(std::map<int, NetDock>& docks, std::map<int, NetShip>& ships, long& ticket, int sid, Purpose purpose,
                               const NetReq& req, std::vector<int> cands, int fixed_dock, double now)
    {
        NetVerdict v;
        if (fixed_dock > 0) cands = {fixed_dock};
        if (cands.empty()) { v.k = NetVerdict::ERR; v.reason = NR_NOT_CONFIGURED; return v; }
        std::vector<int> live;
        for (int d : cands) { docks[d].id = d; if (docks[d].exists) live.push_back(d); }
        if (live.empty()) { v.k = NetVerdict::ERR; v.reason = NR_DOCK_GONE; return v; }
        NetShip& s = ships[sid];
        s.sid = sid; s.intent = purpose; s.wanted = live;
        const double waited = s.ticket != 0 ? now - s.wait_since : 0;   // time already queued (the score ages with it)
        s.req = req; s.prio = net_score(req, purpose, waited, false); s.prio_why = net_prio_why(req, waited, false);
        // a fleeing ship takes no dock slot: never queued, never granted. It waits near the dock (holding slot) unless it already sits at one.
        bool at_dock = false;
        for (int d : live) at_dock = at_dock || docks[d].occ == sid;
        if (purpose == P_FLEE && !at_dock)
        {
            net_unqueue(docks, sid);
            for (auto& kv : docks) if (kv.second.resv == sid) { kv.second.resv = 0; kv.second.why = P_NONE; kv.second.since = 0; }
            if (s.wait_since == 0) s.wait_since = now;
            s.granted = false; s.ticket = 0;
            s.waits_dock = live.front(); s.blocked_by = docks[live.front()].occ; s.reason = NR_WAIT_QUEUE;
            v.k = NetVerdict::WAIT; v.dock = live.front(); v.holder = s.blocked_by; v.reason = NR_WAIT_QUEUE;
            return v;
        }
        std::vector<int> open;
        for (int d : live) if (!docks[d].full) open.push_back(d);
        for (int d : open)
        {
            NetDock& nd = docks[d];
            const bool mine = nd.occ == sid;   // already docked here: continue at this dock, never queue for it
            if (!mine && (nd.occ != 0 || (nd.resv != 0 && nd.resv != sid))) continue;
            if (!mine && nd.resv != sid && net_queue_blocks(nd, ships, s)) continue;
            nd.resv = sid; nd.why = purpose; nd.since = now;
            for (auto& kv : docks) if (kv.first != d && kv.second.resv == sid) { kv.second.resv = 0; kv.second.why = P_NONE; kv.second.since = 0; }   // a grant on another dock is given up
            net_drop_wait(docks, s);
            s.granted = false;
            v.k = NetVerdict::GO; v.dock = d;
            return v;
        }
        // WAIT: first ticket is kept; queue on every dock the ship could use (all full docks if none is open).
        if (s.ticket == 0) { s.ticket = ++ticket; s.wait_since = now; }
        s.granted = false;   // a grant is used up by this pick; the next grant sets it again
        const std::vector<int>& tg = open.empty() ? live : open;
        net_unqueue(docks, sid);
        for (int d : tg) if (docks[d].occ != sid && docks[d].resv != sid) net_queue_add(docks, ships, d, sid);   // a dock the ship sits at or holds is never a queue
        const NetDock& w = docks[tg[0]];
        const int holder = w.occ == sid ? 0 : w.occ;   // the ship that sits at this dock; never the waiter itself
        v.reason = open.empty() ? NR_WAIT_FULL : (w.occ != 0 ? NR_WAIT_BUSY : NR_WAIT_QUEUE);
        s.waits_dock = tg[0]; s.blocked_by = holder; s.reason = v.reason;
        v.k = NetVerdict::WAIT; v.dock = tg[0]; v.holder = holder;
        return v;
    }

    // WHAT: waiters in order (prio high first, then lowest ticket); each takes the first of its wanted docks that is free now. Taken ship gets granted=true.
    // WHY: a waiting ship holds no dock; the grant is the only way a free dock goes to a waiter. Greedy: a free dock never stays idle while someone wants it.
    inline void net_grant_tick(std::map<int, NetDock>& docks, std::map<int, NetShip>& ships, double now)
    {
        std::vector<int> waiters;
        for (const auto& kv : docks)
            for (int q : kv.second.queue)
                if (std::find(waiters.begin(), waiters.end(), q) == waiters.end()) waiters.push_back(q);
        std::stable_sort(waiters.begin(), waiters.end(), [&](int a, int b)
        {
            const NetShip& x = ships[a];
            const NetShip& y = ships[b];
            if (x.prio != y.prio) return x.prio > y.prio;
            return x.ticket < y.ticket;
        });
        for (int sid : waiters)
        {
            NetShip& s = ships[sid];
            for (int d : s.wanted)
            {
                const auto it = docks.find(d);
                if (it == docks.end()) continue;
                NetDock& nd = it->second;
                if (!nd.exists || nd.full || nd.occ != 0 || nd.resv != 0) continue;
                net_drop_wait(docks, s);
                s.granted = true;
                nd.resv = sid; nd.why = s.intent; nd.since = now;
                break;
            }
        }
    }

    // WHAT: ship sid is gone (stop all, halt, release): drop its reservation, queue entries, ticket and the ship row.
    inline void net_release_ship(std::map<int, NetDock>& docks, std::map<int, NetShip>& ships, int sid)
    {
        for (auto& kv : docks)
        {
            NetDock& nd = kv.second;
            if (nd.resv == sid) { nd.resv = 0; nd.why = P_NONE; nd.since = 0; }
        }
        net_unqueue(docks, sid);
        ships.erase(sid);
    }

    // WHAT: offset (m) from the dock to holding slot k (1-based) along dir: 150 m for slot 1, +100 m per slot. dir need not be unit length.
    inline void net_slot_pos(const double dir[3], int k, double out[3], double first_m = NET_SLOT_FIRST_M)
    {
        const double len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        const double m = first_m + (k - 1) * NET_SLOT_STEP_M;
        for (int i = 0; i < 3; ++i) out[i] = len > 0 ? dir[i] / len * m : 0.0;
    }
    // WHAT: first holding distance (m) of a fleeing ship: clear of the station. Farthest other dock within FLEE_STATION_MAX_M (distances in m) + margin, at least FLEE_SLOT_MIN_M.
    inline double flee_slot_first_m(const std::vector<double>& other_dock_dist_m)
    {
        double far = 0.0;
        for (double d : other_dock_dist_m) if (d >= 0.0 && d <= FLEE_STATION_MAX_M && d > far) far = d;
        return std::max(FLEE_SLOT_MIN_M, far + FLEE_SLOT_MARGIN_M);
    }

    // WHAT: chunk + raw loc moved by off_m metres (raw_per_m = raw per metre). A chunk is 134217728 raw (2048 m): the carry goes to the chunk, loc stays in 0..134217728.
    inline void net_offset_pos(const int64_t ch0[3], const double loc0[3], const double off_m[3], double raw_per_m, int64_t ch[3], double loc[3])
    {
        const double chunk_raw = 134217728.0;
        for (int i = 0; i < 3; ++i)
        {
            const double raw = loc0[i] + off_m[i] * raw_per_m;
            const double carry = std::floor(raw / chunk_raw);
            ch[i] = ch0[i] + static_cast<int64_t>(carry);
            loc[i] = raw - carry * chunk_raw;
        }
    }

    // WHAT: holding slot for ship sid at this dock: the slot it already has, else the lowest free one (1-based). line_slots = slots in the line.
    inline int net_slot_assign(NetDock& nd, int sid)
    {
        size_t free_at = nd.slot_sid.size();
        for (size_t i = 0; i < nd.slot_sid.size(); ++i)
        {
            if (nd.slot_sid[i] == sid) return static_cast<int>(i) + 1;
            if (nd.slot_sid[i] == 0 && free_at == nd.slot_sid.size()) free_at = i;
        }
        if (free_at == nd.slot_sid.size()) nd.slot_sid.push_back(sid);
        else nd.slot_sid[free_at] = sid;
        nd.line_slots = static_cast<int>(nd.slot_sid.size());
        return static_cast<int>(free_at) + 1;
    }

    // WHAT: frees every slot of ship sid at this dock.
    inline void net_slot_free(NetDock& nd, int sid)
    {
        for (int& s : nd.slot_sid) if (s == sid) s = 0;
        nd.line_slots = static_cast<int>(nd.slot_sid.size());
    }

    // WHAT: turns the line direction (dir, xy plane, 30 deg steps) until the dock's first nslots slot points are >= 50 m from every point in others (absolute m).
    // WHY: two docks close together must not put holding slots on top of each other. Returns false (dir unchanged) if no step is clear.
    inline bool net_line_dir_fix(const double dock[3], double dir[3], int nslots, const std::vector<std::array<double, 3>>& others)
    {
        const double keep[3] = {dir[0], dir[1], dir[2]};
        const double a = 30.0 * 3.14159265358979323846 / 180.0;
        for (int step = 0; step < 12; ++step)
        {
            bool clear = true;
            for (int k = 1; k <= nslots && clear; ++k)
            {
                double o[3];
                net_slot_pos(dir, k, o);
                for (const auto& p : others)
                {
                    const double dx = dock[0] + o[0] - p[0], dy = dock[1] + o[1] - p[1], dz = dock[2] + o[2] - p[2];
                    if (std::sqrt(dx * dx + dy * dy + dz * dz) < NET_SLOT_MIN_GAP_M) { clear = false; break; }
                }
            }
            if (clear) return true;
            const double x = dir[0], y = dir[1];
            dir[0] = x * std::cos(a) - y * std::sin(a);
            dir[1] = x * std::sin(a) + y * std::cos(a);
        }
        dir[0] = keep[0]; dir[1] = keep[1]; dir[2] = keep[2];
        return false;
    }

    // WHAT: a dock job must undock first when the ship sits at another dock than its target (docked_at 0 = not docked, target 0 = none).
    // WHY: a dock order from a dock does not move the ship (stuck retries, live log 16:03): undock, then dock.
    inline bool dock_undock_first(int docked_at, int target) { return docked_at > 0 && target > 0 && docked_at != target; }

    // WHAT: ship must unload before it refuels: only a refuel trip with ore of this dock in cargo.
    inline bool net_unload_first(bool has_dock_ore, Purpose purpose) { return has_dock_ore && purpose == P_REFUEL; }

    // WHAT: a refuel trip unloads first only when the cargo is full: fill >= CARGO_STOP_PCT, or HasSpaceForItems false (full). Below that it refuels directly.
    // WHY: user rule (live 27 % cargo was sent to unload before refuel). cargo_pct -1 (unknown) is not >= 90, so it refuels directly.
    inline bool refuel_needs_unload(int cargo_pct, bool full) { return full || cargo_pct >= CARGO_STOP_PCT; }

    // WHAT: reservation a ship did not use within NET_CLEAR_S (since = time reserved) is given up, unless that ship already sits at the dock.
    // WHY: a cleared ship that never starts its job must not block the dock; its next net_pick queues it again.
    inline bool net_clear_expired(NetDock& nd, double now)
    {
        if (nd.resv == 0 || nd.occ == nd.resv || now - nd.since < NET_CLEAR_S) return false;
        nd.resv = 0; nd.why = P_NONE; nd.since = 0;
        return true;
    }

    // WHAT: rebuild the tables from what the game shows. occupant: dock -> docked ship. heading: every live ship -> dock its job heads to (0 none). existing: dock ids that exist.
    // WHY: drops reservations and queue entries of dead ships or ships that no longer want that dock, and clears tickets of ships not waiting.
    //       now >= 0: also expire reservations older than NET_CLEAR_S (net_clear_expired); now < 0 = no expiry.
    inline void net_reconcile(std::map<int, NetDock>& docks, std::map<int, NetShip>& ships,
                              const std::map<int, int>& occupant, const std::map<int, int>& heading, const std::set<int>& existing, double now = -1)
    {
        for (auto it = ships.begin(); it != ships.end();) it = heading.count(it->first) ? std::next(it) : ships.erase(it);
        for (auto& kv : docks)
        {
            const int d = kv.first;
            NetDock& nd = kv.second;
            nd.exists = existing.count(d) != 0;
            const auto o = occupant.find(d);
            nd.occ = (o != occupant.end() && heading.count(o->second)) ? o->second : 0;
            if (nd.resv != 0 && now >= 0) net_clear_expired(nd, now);
            if (nd.resv != 0)
            {
                const auto h = heading.find(nd.resv);
                const auto sf = ships.find(nd.resv);
                const bool granted = sf != ships.end() && sf->second.granted;
                const bool keep = nd.exists && h != heading.end() && (h->second == d || granted || nd.occ == nd.resv);
                if (!keep) { nd.resv = 0; nd.why = P_NONE; nd.since = 0; }
            }
            // the occupant of a dock is never in its queue: it is docked there, its wait is over
            if (nd.occ != 0)
            {
                const auto os = ships.find(nd.occ);
                if (os != ships.end() && os->second.waits_dock == d)
                {
                    NetShip& s = os->second;
                    s.ticket = 0; s.waits_dock = 0; s.blocked_by = 0; s.wait_since = 0; s.reason = NR_OK;
                }
            }
            nd.queue.erase(std::remove_if(nd.queue.begin(), nd.queue.end(), [&](int q)
            {
                const auto f = ships.find(q);
                return q == nd.occ || f == ships.end() || f->second.ticket == 0 || std::find(f->second.wanted.begin(), f->second.wanted.end(), d) == f->second.wanted.end();
            }), nd.queue.end());
        }
        for (auto& kv : ships)
        {
            if (kv.second.waits_dock == 0) kv.second.ticket = 0;
            if (kv.second.blocked_by == kv.first) kv.second.blocked_by = 0;   // a ship is never blocked by itself
        }
    }

    // WHAT: holder of a dock is bad (send it away) only when a ship waits for it and: the dock is FULL, or the holder is parked (HALT/OFF),
    //       or after the dwell (docked_s >= NET_DWELL_S) the holder is not working or has no ore of this dock in cargo.
    // WHY: a holder that is unloading (working, ore still in cargo) is never sent away; a just-docked ship gets its dwell first.
    inline bool net_bad_holder(HolderState st, bool has_ore, bool full, int waiters, double docked_s)
    {
        if (waiters <= 0) return false;
        if (full || st == HS_PARKED) return true;
        if (docked_s < NET_DWELL_S) return false;
        return st != HS_WORKING || !has_ore;
    }

    // WHAT: unload stalled: items not all delivered, t >= wait_s, and the ore cargo has not grown for idle_s (idle_s 0 = off). Same rule as refuel_stalled.
    bool unload_stalled(bool done, double idle_s, double t, double wait_s, double hold_idle)
    {
        return !done && idle_s > 0 && t >= wait_s && hold_idle >= idle_s;
    }

    // WHAT: log gate for a status that changes often (wait text, queue place). key = what is shown in the log.
    // WHY: the Log tab (20 lines per ship) and the server log must not get a line per second.
    //       GATE_NEW = key changed (only when min_gap s passed since the last line); GATE_REMIND = same key for remind_s s.
    struct LogGate { std::string key; double last = -1e18; };
    enum GateAct { GATE_NONE, GATE_NEW, GATE_REMIND };
    inline GateAct log_gate(LogGate& g, const std::string& key, double now, double min_gap, double remind_s)
    {
        if (key != g.key)
        {
            if (now - g.last < min_gap) return GATE_NONE;   // too soon: the key stays old, the change logs later
            g.key = key; g.last = now;
            return GATE_NEW;
        }
        if (now - g.last >= remind_s) { g.last = now; return GATE_REMIND; }
        return GATE_NONE;
    }

    // WHAT: m:ss for a wait time: no leading zero on the minutes ("2:09", "0:45", "12:03").
    inline std::string net_mmss(double wait_s)
    {
        const long long total = wait_s > 0 ? static_cast<long long>(wait_s) : 0;
        const long long sec = total % 60;
        return std::to_string(total / 60) + ":" + (sec < 10 ? "0" : "") + std::to_string(sec);
    }

    // WHAT: Log tab text of a dock wait. No elapsed time in the start/change line; remind = the "Still waiting" reminder (with m:ss).
    inline std::string net_wait_log(NetReason r, const std::string& dock, const std::string& holder, double wait_s, bool remind)
    {
        if (remind) return "Still waiting for " + dock + " (" + net_mmss(wait_s) + (holder.empty() ? std::string() : " occupied by " + holder) + ")";
        if (r == NR_WAIT_UNMANAGED) return "Waiting for " + dock + " (held by ship " + holder + ", not managed)";
        if (r == NR_WAIT_FULL) return "Dock full (" + dock + "), waiting for space";
        if (r == NR_WAIT_QUEUE || holder.empty()) return "Waiting for " + dock;
        return "Waiting for " + dock + " (occupied by " + holder + ")";
    }

    // WHAT: status text for a ship's wait / refusal. Format is fixed here so the UI and tests see the same words.
    // subject = dock name (or ore name for NR_NOT_CONFIGURED); holder = name of the ship at the dock; wait_s = seconds waited;
    // qpos/qn = queue place, shown as " - queue p/n" only when more than one ship waits (qn > 1).
    inline std::string net_ship_text(NetReason r, const std::string& subject, const std::string& holder, double wait_s, int qpos, int qn)
    {
        const std::string q = qn > 1 ? " - queue " + std::to_string(qpos) + "/" + std::to_string(qn) : "";
        const std::string wt = net_mmss(wait_s);
        switch (r)
        {
        case NR_WAIT_BUSY: return "Waiting for " + subject + " (" + wt + " occupied by " + holder + ")" + q;
        case NR_WAIT_QUEUE: return "Waiting for " + subject + " (" + wt + ")" + q;
        case NR_WAIT_FULL: return "Dock full (" + subject + "), waiting for space" + q;
        case NR_WAIT_PARKED: return "Dock " + subject + " held by idle ship " + holder + ", no second dock to leave to";
        case NR_WAIT_UNMANAGED: return "Waiting for " + subject + " (held by ship " + holder + ", not managed)" + q;
        case NR_NOT_CONFIGURED: return "No unload dock configured for " + subject;
        case NR_DOCK_GONE: return "Unload dock " + subject + " no longer exists";
        case NR_NOTHING_TO_UNLOAD: return "Nothing to unload";
        default: return "";
        }
    }

    // WHAT: no-yield rock: a rock whose idle-watchdog exit (no ore gained on it) happened this many times in a row is skipped by the rock search.
    // WHY: a rock that gives nothing (spent, unreachable) would otherwise be picked again forever.
    constexpr int NOY_SKIP_VISITS = 3;
    inline bool noy_skip_now(int visits) { return visits >= NOY_SKIP_VISITS; }

    // WHAT: depleted rock: the rock gave ore on this visit (got_this_rock > 0), then no ore gained for DEPLETE_S with the laser on and the cargo not full.
    // WHY: a spent rock keeps its actor (HUD marker) for 30+ s; the ship sat on it. Before the first ore the no-yield watchdog still applies.
    constexpr double DEPLETE_S = 5.0;
    inline bool rock_depleted(int64_t got_this_rock, double idle_t, bool laser_on, bool cargo_full)
    {
        return got_this_rock > 0 && laser_on && !cargo_full && idle_t >= DEPLETE_S;
    }

    // WHAT: "rock lost" approach results (another ship took the rock) in a row in SEEK. This many = HALT "rock lost 5x".
    // WHY: a rock lost is a free retry, but the same rock vanishing again and again must still end the loop.
    constexpr int ROCK_LOST_MAX = 5;
    inline bool rock_lost_halt(int n) { return n >= ROCK_LOST_MAX; }

    // WHAT: CHECK returns to OFF with nothing in range (belt off), in a row, before HALT "no rock in range". 12 checks x MINE_ROCK_CHECK_S (5 s) = about 60 s.
    // WHY: the idle poll must not run forever with no rock anywhere near.
    constexpr int IDLE_POLL_MAX = 12;
    inline bool idle_poll_halt(int n) { return n >= IDLE_POLL_MAX; }

    // WHAT: every rock in range is skipped: a rock exists when the skip list is ignored, but none is found with it. HALT "rocks give no ore".
    inline bool all_rocks_skipped(bool rock_ignoring_skip, bool rock_with_skip) { return rock_ignoring_skip && !rock_with_skip; }

    // WHAT: all rocks skipped -> HALT "rocks give no ore" only without a belt (belt off/none). With a belt the sector counts as empty and the belt moves on.
    // WHY: a belt has other sectors to try; the no-sector case HALTs through the belt path ("Idle: no ore in any sector").
    inline bool noy_all_halt(const std::string& belt) { return belt_off(belt); }

    // WHAT: longest dock wait (dwait, queued or holding for a dock) in one trip before HALT "waited 15 min for a dock" (s).
    // WHY: a dock that never frees up must not hold the loop forever (not an error).
    constexpr double MINE_DWAIT_MAX_S = 900.0;
    inline bool dwait_halt(double waited_s) { return waited_s > MINE_DWAIT_MAX_S; }

    // WHAT: refuel amount is never below its own threshold (a refuel to amount ends at or above threshold).
    // WHY: a refuel that stops at amount below threshold would make the next CHECK refuel again at once.
    inline int fuel_amount_floor(int amount, int threshold) { return std::max(amount, threshold); }

    // WHAT: one bounded setting. key = INI key (global or [ship.<id>]). min/max in the key's unit (s, count, km, fuel units); dist: the value is a
    //       distance text (5km, 2500m, plain meters) and the bounds are meters. zero_ok: 0 = off, allowed and kept as is.
    // WHY: one table for the config load and for the Lua Operation tab, so out-of-range input is clamped or rejected the same way everywhere.
    struct SettingBound { const char* key; double min; double max; bool zero_ok; bool dist; };
    constexpr SettingBound SETTING_BOUNDS[] = {
        {"watchdog", 0, 1, false, false},
        {"idle_seconds", 30, 3600, true, false},
        {"max_attempts", 1, 10, false, false},
        {"wait_seconds", 0, 300, false, false},
        {"dock_hold_limit", 30, 3600, true, false},
        {"warp_trip_km", 1, 100, true, false},
        {"fuel_hydrogen_threshold", 0, 5000, false, false},
        {"fuel_hydrogen_amount", 0, 5000, false, false},
        {"fuel_warp_threshold", 0, 5000, false, false},
        {"fuel_warp_amount", 0, 5000, false, false},
        {"fuel_deuterium_threshold", 0, 5000, false, false},
        {"fuel_deuterium_amount", 0, 5000, false, false},
        {"fuel_margin", 0, 5000, false, false},
        {"fuel_amount", 0, 5000, false, false},
        {"radius", 100, 100000, true, true},
        {"max_radius", 100, 100000, true, true},
        {"warp_range", 1000, 100000, true, true},
        {"mining_distance", 1, 5000, false, true},
        {"flee_on_target", 0, 1, true, false},
        {"path_clearance_m", 0, 100, true, false},
        {"bubble_clearance_m", 0, 100, true, false},
        {"flee_target_range_km", 1, 100, false, false},
        {"low_power_return", 0, 1, false, false},
        {"reactor_auto_input", 0, 1, false, false},
    };

    // WHAT: the bound row for key, nullptr when the key is not bounded.
    inline const SettingBound* find_setting_bound(const std::string& key)
    {
        for (const SettingBound& b : SETTING_BOUNDS) if (key == b.key) return &b;
        return nullptr;
    }

    // WHAT: v clamped to the bound into out (zero_ok and v == 0 stays 0). true when the value changed.
    inline bool clamp_setting(const SettingBound& b, double v, double& out)
    {
        out = (b.zero_ok && v == 0.0) ? 0.0 : std::min(b.max, std::max(b.min, v));
        return out != v;
    }

    // WHAT: clamps every bounded key of one raw INI section (kv) in place. Each change adds one "key old -> new" text to msgs.
    // WHY: runs on load only; empty and unparsable values are left alone (apply_kv keeps the old value or the default).
    inline void clamp_kv(std::map<std::string, std::string>& kv, std::vector<std::string>& msgs)
    {
        for (const SettingBound& b : SETTING_BOUNDS)
        {
            auto it = kv.find(b.key);
            if (it == kv.end()) continue;
            const std::string t = trim(it->second);
            if (t.empty()) continue;
            double v = 0.0;
            if (b.dist)
            {
                v = parse_distance(t);
                if (v < 0.0) continue;
            }
            else
            {
                char* end = nullptr;
                v = std::strtod(t.c_str(), &end);
                if (end == t.c_str() || *end != '\0') continue;
            }
            double out = v;
            if (!clamp_setting(b, v, out)) continue;
            const std::string nt = std::to_string(static_cast<long long>(out)) + (b.dist ? "m" : "");
            msgs.push_back(std::string(b.key) + " " + t + " -> " + nt);
            it->second = nt;
        }
    }

    // WHAT: probe avoidance (mb_devcmd.hpp): watch limits and the command argument parsers. Pure, so tests/test_logic.cpp covers them.
    constexpr double PROBE_WATCH_MAX_S = 120.0;   // probe_avoid_watch seconds cap (larger values are clamped)
    constexpr size_t PROBE_WATCH_MAX_LINES = 500; // MiningProbeAvoid.txt line cap for the sampler (header and end line included)
    constexpr double PROBE_Q16_PER_M = 65536.0;   // PNum / PVector raw per metre (Q16.16)
    constexpr double PROBE_UU_PER_M = 100.0;      // FVector units per metre (UE standard)
    constexpr double PROBE_FWD_FIXED_M = 50.0;    // collision watch: fixed forward probe distance
    constexpr double PROBE_UP_M = 20.0;           // collision watch: raised probe height (m), the live AvoidanceQueryHeightOffset value

    // WHAT: true while the sampler should write another sample: one line below the cap (the last line is kept for the end line) and under the time asked for.
    inline bool probe_watch_more(size_t lines, double elapsed_s, double total_s) { return lines + 1 < PROBE_WATCH_MAX_LINES && elapsed_s < total_s; }

    // WHAT: "<pre><ship>" -> sid. false for another prefix, an empty or non-digit id.
    inline bool probe_parse_sid_pfx(const std::string& cmd, const std::string& pre, int& sid)
    {
        if (cmd.rfind(pre, 0) != 0) return false;
        const std::string a = cmd.substr(pre.size());
        if (a.empty() || a.find_first_not_of("0123456789") != std::string::npos) return false;
        sid = std::atoi(a.c_str());
        return sid > 0;
    }

    // WHAT: "probe_avoid:<ship>" -> sid. false for the bare "probe_avoid" or a non-digit id (the caller then uses the line's ship).
    inline bool probe_parse_sid(const std::string& cmd, int& sid) { return probe_parse_sid_pfx(cmd, "probe_avoid:", sid); }

    // WHAT: "<pre><ship>:<seconds>" -> sid and seconds (clamped to PROBE_WATCH_MAX_S). false when either part is missing or not a positive number.
    inline bool probe_parse_watch_pfx(const std::string& cmd, const std::string& pre, int& sid, double& secs)
    {
        if (cmd.rfind(pre, 0) != 0) return false;
        const size_t c = cmd.find(':', pre.size());
        if (c == std::string::npos) return false;
        const std::string a = cmd.substr(pre.size(), c - pre.size());
        const std::string b = cmd.substr(c + 1);
        if (a.empty() || a.find_first_not_of("0123456789") != std::string::npos) return false;
        if (b.empty() || b.find_first_not_of("0123456789.") != std::string::npos) return false;
        sid = std::atoi(a.c_str());
        secs = std::atof(b.c_str());
        if (sid <= 0 || secs <= 0.0) return false;
        secs = std::min(secs, PROBE_WATCH_MAX_S);
        return true;
    }

    // WHAT: "probe_avoid_watch:<ship>:<seconds>" -> sid and seconds.
    inline bool probe_parse_watch(const std::string& cmd, int& sid, double& secs) { return probe_parse_watch_pfx(cmd, "probe_avoid_watch:", sid, secs); }

    // WHAT: "probe_collision_watch:<ship>:<seconds>" -> sid and seconds.
    inline bool probe_parse_cwatch(const std::string& cmd, int& sid, double& secs) { return probe_parse_watch_pfx(cmd, "probe_collision_watch:", sid, secs); }

    // WHAT: "probe_collision_call:<ship>:<x>:<y>:<z>" -> sid and the local offset v[0..2] in metres. false when the id is not positive or a value is not a finite number.
    inline bool probe_parse_call(const std::string& cmd, int& sid, double v[3])
    {
        const std::string pre = "probe_collision_call:";
        if (cmd.rfind(pre, 0) != 0) return false;
        std::vector<std::string> parts;
        for (size_t st = pre.size();;)
        {
            const size_t c = cmd.find(':', st);
            parts.push_back(cmd.substr(st, c == std::string::npos ? std::string::npos : c - st));
            if (c == std::string::npos) break;
            st = c + 1;
        }
        if (parts.size() != 4 || parts[0].empty() || parts[0].find_first_not_of("0123456789") != std::string::npos) return false;
        sid = std::atoi(parts[0].c_str());
        if (sid <= 0) return false;
        for (int i = 0; i < 3; ++i)
        {
            char* e = nullptr;
            const double d = std::strtod(parts[i + 1].c_str(), &e);
            if (parts[i + 1].empty() || e == parts[i + 1].c_str() || *e != '\0' || !std::isfinite(d)) return false;
            v[i] = d;
        }
        return true;
    }

    // WHAT: box probe modes (probe_collision_box, probe_collision_boxwatch). local: box offsets in m, passed as uu (x100) with no frame change.
    //       world: the same offsets added to the ship location (uu, K2_GetActorLocation), axis aligned. raw: the typed numbers passed as uu, no conversion.
    constexpr int PROBE_BOX_LOCAL = 0;
    constexpr int PROBE_BOX_WORLD = 1;
    constexpr int PROBE_BOX_RAW = 2;
    constexpr size_t PROBE_BOX_HIT_LINES = 40;     // hit lines written per box call
    constexpr double PROBE_BOX_ALL_HALF_M = 50.0;  // boxwatch: box around the ship, half size per axis (m)
    constexpr double PROBE_BOX_FWD_HALF_M = 15.0;  // boxwatch: forward box half size in y and z (m)
    constexpr double PROBE_BOX_FWD_MIN_M = 25.0;   // boxwatch: forward box length floor (m)

    // WHAT: splits s on ':' (empty parts kept): "a::b" -> {"a", "", "b"}.
    inline std::vector<std::string> probe_split(const std::string& s)
    {
        std::vector<std::string> out;
        for (size_t st = 0;;)
        {
            const size_t c = s.find(':', st);
            out.push_back(s.substr(st, c == std::string::npos ? std::string::npos : c - st));
            if (c == std::string::npos) break;
            st = c + 1;
        }
        return out;
    }

    // WHAT: a whole string as a finite number (nothing left over). false when empty or not a number.
    inline bool probe_num(const std::string& t, double& v)
    {
        char* e = nullptr;
        v = std::strtod(t.c_str(), &e);
        return !t.empty() && e != t.c_str() && *e == '\0' && std::isfinite(v);
    }

    // WHAT: "local", "world", "raw" -> mode. Empty = local. false for any other word.
    inline bool probe_parse_box_mode(const std::string& t, int& mode)
    {
        if (t.empty() || t == "local") { mode = PROBE_BOX_LOCAL; return true; }
        if (t == "world") { mode = PROBE_BOX_WORLD; return true; }
        if (t == "raw") { mode = PROBE_BOX_RAW; return true; }
        return false;
    }

    // WHAT: mode as text for the file and the log.
    inline const char* probe_box_mode_name(int mode) { return mode == PROBE_BOX_WORLD ? "world" : mode == PROBE_BOX_RAW ? "raw" : "local"; }

    // WHAT: "probe_collision_box:<ship>:<minx>:<miny>:<minz>:<maxx>:<maxy>:<maxz>[:local|world|raw]" -> sid, lo[3], hi[3] (as typed) and mode.
    //       false when the ship id is not positive, a number is missing or not finite, min > max on an axis, or the mode word is unknown.
    inline bool probe_parse_box(const std::string& cmd, int& sid, double lo[3], double hi[3], int& mode)
    {
        const std::string pre = "probe_collision_box:";
        if (cmd.rfind(pre, 0) != 0) return false;
        const std::vector<std::string> p = probe_split(cmd.substr(pre.size()));
        if (p.size() != 7 && p.size() != 8) return false;
        if (p[0].empty() || p[0].find_first_not_of("0123456789") != std::string::npos) return false;
        sid = std::atoi(p[0].c_str());
        if (sid <= 0) return false;
        for (int i = 0; i < 3; ++i)
        {
            if (!probe_num(p[1 + i], lo[i]) || !probe_num(p[4 + i], hi[i])) return false;
            if (lo[i] > hi[i]) return false;
        }
        return probe_parse_box_mode(p.size() == 8 ? p[7] : std::string(), mode);
    }

    // WHAT: "probe_collision_boxwatch:<ship>:<seconds>[:local|world]" -> sid, seconds (clamped to PROBE_WATCH_MAX_S) and mode. raw is refused:
    //       the watch boxes are relative to the ship, there is no typed box to give as is.
    inline bool probe_parse_boxwatch(const std::string& cmd, int& sid, double& secs, int& mode)
    {
        const std::string pre = "probe_collision_boxwatch:";
        if (cmd.rfind(pre, 0) != 0) return false;
        const std::vector<std::string> p = probe_split(cmd.substr(pre.size()));
        if (p.size() != 2 && p.size() != 3) return false;
        if (p[0].empty() || p[0].find_first_not_of("0123456789") != std::string::npos) return false;
        sid = std::atoi(p[0].c_str());
        if (sid <= 0 || !probe_num(p[1], secs) || secs <= 0.0) return false;
        secs = std::min(secs, PROBE_WATCH_MAX_S);
        if (!probe_parse_box_mode(p.size() == 3 ? p[2] : std::string(), mode)) return false;
        return mode != PROBE_BOX_RAW;
    }

    // WHAT: box corners in FVector units from typed metres (m_lo, m_hi) and the ship location (ship_uu, uu). Mode rules in the header above.
    inline void probe_box_uu(int mode, const double m_lo[3], const double m_hi[3], const double ship_uu[3], double lo_uu[3], double hi_uu[3])
    {
        for (int i = 0; i < 3; ++i)
        {
            const double base = mode == PROBE_BOX_WORLD ? ship_uu[i] : 0.0;
            const double k = mode == PROBE_BOX_RAW ? 1.0 : PROBE_UU_PER_M;
            lo_uu[i] = base + k * m_lo[i];
            hi_uu[i] = base + k * m_hi[i];
        }
    }

    // WHAT: forward box length of the boxwatch (m): 50 * Throttle / 10, at least 25 m.
    inline double probe_box_fwd_m(int throttle) { return std::max(PROBE_BOX_FWD_MIN_M, PROBE_FWD_FIXED_M * throttle / 10.0); }

    // WHAT: the two boxwatch boxes in metres relative to the ship: all = +-50 m on x, y, z; fwd = x 0..d, y and z +-15 m (d = probe_box_fwd_m).
    inline void probe_watch_boxes(int throttle, double all_lo[3], double all_hi[3], double fwd_lo[3], double fwd_hi[3])
    {
        const double d = probe_box_fwd_m(throttle);
        for (int i = 0; i < 3; ++i)
        {
            all_lo[i] = -PROBE_BOX_ALL_HALF_M;
            all_hi[i] = PROBE_BOX_ALL_HALF_M;
        }
        fwd_lo[0] = 0.0;
        fwd_hi[0] = d;
        for (int i = 1; i < 3; ++i)
        {
            fwd_lo[i] = -PROBE_BOX_FWD_HALF_M;
            fwd_hi[i] = PROBE_BOX_FWD_HALF_M;
        }
    }

    // WHAT: metres to Q16.16 raw (the PNum / PVector Value unit: 65536 raw per m), rounded to the nearest raw unit.
    inline int64_t probe_m_to_q16(double m) { return static_cast<int64_t>(std::llround(m * PROBE_Q16_PER_M)); }

    // WHAT: metres to FVector units (UE standard, 100 uu per m).
    inline double probe_m_to_uu(double m) { return m * PROBE_UU_PER_M; }

    // WHAT: forward probe distance of the collision watch: 50 * Throttle / 10 m, Throttle clamped to at least 1.
    inline double probe_fwd_m(int throttle) { return PROBE_FWD_FIXED_M * std::max(throttle, 1) / 10.0; }

    // WHAT: bytes as lower-case hex pairs separated by spaces ("00 ff 0a"). Empty for n = 0.
    inline std::wstring probe_hex(const uint8_t* p, size_t n)
    {
        static const wchar_t* const kDigits = L"0123456789abcdef";
        std::wstring s;
        for (size_t i = 0; i < n; ++i)
        {
            if (i) s += L' ';
            s += kDigits[p[i] >> 4];
            s += kDigits[p[i] & 15];
        }
        return s;
    }

    // WHAT: true when a property or function name holds one of the avoidance / impulse words (case-insensitive).
    // WHY: the probe lists only the names that can be the avoidance logic; the full property dump covers the rest.
    inline bool probe_name_match(const std::wstring& name)
    {
        static const wchar_t* const kWords[] = {L"avoid", L"collision", L"obstacle", L"bubble", L"query", L"sweep", L"impulse", L"autopilot", L"proximity", L"overlap"};
        std::wstring l = name;
        for (wchar_t& ch : l) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + 32);
        for (const wchar_t* w : kWords) if (l.find(w) != std::wstring::npos) return true;
        return false;
    }

    // ---------- low power (mb_mining.hpp POWER monitor and response, mb_steer.hpp approach profile) ----------
    constexpr double LOWPOWER_GRACE_S = 10.0;   // low power this long (s) while flying or mining -> dock trip like fuel low (low_power_return)

    // WHAT: the low-power response may fire: low power for at least LOWPOWER_GRACE_S (lp_t = seconds in low power, 1 s samples).
    inline bool lowpower_due(double lp_t) { return lp_t >= LOWPOWER_GRACE_S; }

    // WHAT: loop guard. A dock stage is done (refuel or unload finished) after a low-power trip and the ship is still in low power -> HALT, no new trip.
    // WHY: dock -> seek -> low power -> dock would repeat forever when the dock does not clear the low power.
    inline bool lowpower_halt_at_dock(bool lp_trip, bool lp_due) { return lp_trip && lp_due; }

    // WHAT: approach timeout (s): base, or base / max_frac in low power (max_frac = FlightModel MaxLowPowerSpeed, a fraction 0..1). Other max_frac -> base.
    inline double approach_timeout_s(double base, bool lp, double max_frac)
    {
        if (!lp || max_frac <= 0.0 || max_frac >= 1.0) return base;
        return base / max_frac;
    }

    // WHAT: Throttle step cap: min(cap, lp_thr) in low power when lp_thr > 0 (FlightModel LowPowerThrottle), else cap.
    inline int lowpower_throttle_cap(int cap, bool lp, int lp_thr) { return (lp && lp_thr > 0) ? std::min(cap, lp_thr) : cap; }

    // WHAT: true when name holds one of the words (ASCII case-insensitive, words in lower case).
    inline bool name_has_word(const std::wstring& name, const wchar_t* const* words, size_t n)
    {
        std::wstring l = name;
        for (wchar_t& ch : l) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + 32);
        for (size_t i = 0; i < n; ++i) if (l.find(words[i]) != std::wstring::npos) return true;
        return false;
    }
    // WHAT: probe_power property/function name matches Power|Fuel|Energy|Battery|Reactor|Thruster|Generator (LowPower and Powered contain Power).
    inline bool power_name_match(const std::wstring& name)
    {
        static const wchar_t* const kWords[] = {L"power", L"fuel", L"energy", L"battery", L"reactor", L"thruster", L"generator"};
        return name_has_word(name, kWords, sizeof(kWords) / sizeof(kWords[0]));
    }
    // WHAT: probe_power component match: the class name holds Power|Generator|Reactor|Battery|Thruster|Fuel.
    inline bool power_obj_match(const std::wstring& cls)
    {
        static const wchar_t* const kWords[] = {L"power", L"generator", L"reactor", L"battery", L"thruster", L"fuel"};
        return name_has_word(cls, kWords, sizeof(kWords) / sizeof(kWords[0]));
    }
    // WHAT: probe_power highlight: a property name that may say pull / auto / feed / buffer / mode / enabled / active (the fuel draw logic).
    inline bool power_highlight_match(const std::wstring& name)
    {
        static const wchar_t* const kWords[] = {L"pull", L"auto", L"feed", L"buffer", L"request", L"refill", L"supply", L"source", L"priority", L"mode", L"benabled", L"bactive"};
        return name_has_word(name, kWords, sizeof(kWords) / sizeof(kWords[0]));
    }
    // WHAT: a stored fuel / energy amount name (fuel, buffer, stored, reserve, energy) for the watch and the dump.
    inline bool power_buffer_match(const std::wstring& name)
    {
        static const wchar_t* const kWords[] = {L"fuel", L"buffer", L"stored", L"reserve", L"energy"};
        return name_has_word(name, kWords, sizeof(kWords) / sizeof(kWords[0]));
    }
    // WHAT: probe_power_watch sample period (s): true once per 1 s of acc; acc keeps the remainder. dt = time since the last call.
    inline bool power_watch_due(double& acc, double dt)
    {
        acc += dt;
        if (acc < 1.0) return false;
        acc -= 1.0;
        return true;
    }
    // WHAT: one watch line (text, no newline): t seconds, low-power state (lp_known false = flag missing), PowerGenRate, PowerDraw, the stored
    //       buffer text and the cargo fuel text. Pure: the caller reads the game values.
    inline std::wstring power_watch_line(double t, bool lp_known, bool lp, const std::wstring& rate, const std::wstring& draw,
                                         const std::wstring& buf, const std::wstring& cargo)
    {
        const std::wstring st = !lp_known ? L"unknown" : lp ? L"LOW" : L"ok";
        return L"t=" + std::to_wstring(t) + L" lp=" + st + L" PowerGenRate=" + rate + L" PowerDraw=" + draw + L" buffer:" + buf + L" cargo:" + cargo;
    }

    // ---------- power grid (ship.PowerGrid: TotalProduction, TotalConsumption, ConsumerEfficiency) and reactor auto input ----------
    constexpr double POWER_BROWNOUT_EFF = 0.5;          // grid efficiency below this with production = brown-out (log only)
    constexpr double REACTOR_AUTO_PERIOD_S = 60.0;      // reactor auto-input check period while a bench ship is active (s)
    constexpr uint64_t REACTOR_AUTO_RATE_MS = 30000;    // one write attempt per reactor per this many ms
    constexpr int REACTOR_AUTO_MAX = 3;                 // attempts per reactor before the give-up line (then no more attempts)

    // WHAT: no power: the grid consumes (cons > 0), produces nothing (prod <= 0) and efficiency is 0. This is the lost state (grace LOWPOWER_GRACE_S).
    inline bool power_no_power(double cons, double prod, double eff) { return cons > 0.0 && prod <= 0.0 && eff <= 0.0; }
    // WHAT: brown-out: the grid produces but efficiency is below POWER_BROWNOUT_EFF while it consumes. Log only, no action.
    inline bool power_brownout(double cons, double prod, double eff) { return cons > 0.0 && prod > 0.0 && eff < POWER_BROWNOUT_EFF; }
    // WHAT: brown-out log event: 0 = no change, 1 = started, 2 = ended. One line per change, never per sample.
    inline int power_bo_event(bool was, bool now) { return was == now ? 0 : now ? 1 : 2; }
    // WHAT: periodic timer: acc += dt; true once per period (acc keeps the remainder).
    inline bool period_due(double& acc, double dt, double period)
    {
        acc += dt;
        if (acc < period) return false;
        acc -= period;
        return true;
    }
    // WHAT: reactor auto input action for one reactor. on = bAutoInputFuel already true (NONE). gave_up = the give-up line was written (NONE).
    //       attempts >= REACTOR_AUTO_MAX -> GIVEUP (once). Within REACTOR_AUTO_RATE_MS of the last attempt -> NONE. Else WRITE.
    enum ReactorAct { RA_NONE = 0, RA_WRITE = 1, RA_GIVEUP = 2 };
    inline int reactor_auto_action(bool on, int attempts, bool gave_up, uint64_t last_ms, uint64_t now_ms)
    {
        if (on || gave_up) return RA_NONE;
        if (attempts >= REACTOR_AUTO_MAX) return RA_GIVEUP;
        if (last_ms != 0 && now_ms - last_ms < REACTOR_AUTO_RATE_MS) return RA_NONE;
        return RA_WRITE;
    }
}
