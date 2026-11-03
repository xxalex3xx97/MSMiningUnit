// FILE: mods/MiningBenchMod/mb_logic.hpp
// WHAT: pure logic: config structs and INI value parsing (Config, apply_kv, eff_cfg, parse_distance, parse_items), command line
//       parsing, and the small rule functions the mining loop, belt and fuel code use (no game objects).
// WHY separate: these have no UE or Windows calls, so the unit tests (tests/test_logic.cpp) can compile and run them alone.
// DEPENDS ON (exact): std headers only (algorithm, cctype, cmath, cstdint, cstdlib, map, string, utility, vector). No UE4SS, no Windows.h.
// USED BY: mb_common.hpp (includes this first), mb_config.hpp, mb_cmdstate.hpp, mb_belt.hpp, mb_mining.hpp, mb_dockjob.hpp, mb_testcmd.hpp, tests/test_logic.cpp
// RULE: keep this file free of UE/Windows headers and STR(); use L"" literals.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <map>
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

    // WHAT: sweep (MS_SWEEP) limits: pick timeout (s), rescans before giving up, fly-back max distance (m), fly timeout (s), warp wait (s).
    constexpr double SWEEP_PICK_S = 10.0;
    constexpr int SWEEP_RESCANS = 2;
    constexpr double SWEEP_FLY_MAX_M = 4096.0;
    constexpr double SWEEP_FLY_TIMEOUT_S = 120.0;
    constexpr double SWEEP_WARP_WAIT_S = 15.0;
    constexpr double SWEEP_RESCAN_S = 2.0;   // wait before each rescan of the ore list (s)

    // WHAT: fly back by tractor flight only when same system and within SWEEP_FLY_MAX_M; else warp.
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

    // WHAT: dock busy retry wait in seconds for retry n (0-based): 2, 5, 10, 20, 40, then 60 for every later retry.
    double dock_backoff_s(int n)
    {
        static const double BO[] = {2, 5, 10, 20, 40, 60};
        return BO[std::min(n, 5)];
    }

    // WHAT: hull dropped below the baseline. baseline < 0 = no baseline yet, never a drop.
    bool hull_dropped(int32_t baseline, int32_t h)
    {
        return baseline >= 0 && h < baseline;
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
}
