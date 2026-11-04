// FILE: mods/MiningBenchMod/mb_config.hpp
// WHAT: Config struct, MiningBench.ini create/parse/hot reload, per-ship [ship.<id>] overrides, and the config change log.
// WHY separate: config is the input to every feature, so it is loaded first and makes no UObject calls.
// DEPENDS ON (exact): mb_common.hpp
// USED BY: the later mb_*.hpp feature headers and dllmain.cpp (reload_config, check_config, eff_cfg, g_cfg, g_raw_per_m)
// GAME FACTS: ASSUMPTION: raw PNum = meters * 65536 (raw_per_meter). Checked only by rock radii of 8.0 and 70.0 m and by integer dock grid cells (see g_raw_per_m). The earlier guess of 21845 was wrong (3x too many metres).
#pragma once
#include <map>
#include <vector>

namespace
{




    // WHAT: raw fixed-point units per meter as a compile-time constant (65536 = 2^16). Units: raw/m.
    // WHY: used by dump_struct_div for readable logs. g_raw_per_m is the value the mod math actually uses.
    constexpr double RAW_PER_METER = 65536.0;   // Hypothesis B, UNCALIBRATED: raw = meters * 65536

    // WHAT: raw per meter used by the mod math (double). Units: raw/m. Set by the raw_per_meter INI key.
    // WHY: a runtime calibration knob; 65536 is the default.
    double g_raw_per_m = 65536.0;               // raw per meter. 65536 verified: rock radii are exactly 8.0 / 70.0 m and dock grid cells are integers at this scale (21845 was a wrong guess, 3x too many metres)

    // WHAT: ordered list of (INI key, printed value) text pairs.
    // WHY: one list drives both the startup log and the change diff, which matches entries by index.
    using Fields = std::vector<std::pair<std::wstring, std::wstring>>;

    // WHAT: dock id list as quoted text "1,2,".
    std::wstring ids_text(const std::vector<int>& v)
    {
        std::wstring s;
        for (int d : v) s += std::to_wstring(d) + STR(",");
        return STR("'") + s + STR("'");
    }

    // WHAT: every config key with its value as text, in a fixed order.
    // WHY: reload_config diffs old and new config by index and logs only what changed.
    Fields config_fields(const Config& c)
    {
        Fields f = {
            {STR("enabled"), std::to_wstring(c.enabled)},
            {STR("resource"), STR("'") + widen(c.resource) + STR("'")},
            {STR("amount"), std::to_wstring(c.amount)},
            {STR("dock_id"), std::to_wstring(c.dock_id)},
            {STR("dock_id_name"), STR("'") + widen(c.dock_id_name) + STR("'")},
            {STR("dock_name_filter"), STR("'") + widen(c.dock_name_filter) + STR("'")},
            {STR("range_raw"), std::to_wstring(c.range_raw)},
            {STR("max_radius"), STR("'") + widen(c.max_radius) + STR("'")},
            {STR("radius"), STR("'") + widen(c.radius) + STR("'")},
            {STR("idle_seconds"), std::to_wstring(c.idle_seconds)},
            {STR("dock_hold_limit"), std::to_wstring(c.dock_hold_limit)},
            {STR("warp_trip_km"), std::to_wstring(c.warp_trip_km)},
            {STR("log_interval_seconds"), std::to_wstring(c.log_interval_seconds)},
            {STR("order_retry_seconds"), std::to_wstring(c.order_retry_seconds)},
            {STR("max_attempts"), std::to_wstring(c.max_attempts)},
            {STR("repeat"), std::to_wstring(c.repeat)},
            {STR("trace_interval_seconds"), std::to_wstring(c.trace_interval_seconds)},
            {STR("record"), std::to_wstring(c.record)},
            {STR("record_interval_seconds"), std::to_wstring(c.record_interval_seconds)},
            {STR("kill_switch"), std::to_wstring(c.kill_switch ? 1 : 0)},
            {STR("mining_distance"), STR("'") + widen(c.mining_distance) + STR("'")},
            {STR("fuel_margin"), std::to_wstring(c.fuel_margin)},
            {STR("fuel_amount"), std::to_wstring(c.fuel_amount)},
            {STR("raw_per_meter"), std::to_wstring(c.raw_per_meter)},
            {STR("steer_probe_speed_k"), std::to_wstring(c.steer_probe_speed_k)},
            {STR("refuel_dock_id"), std::to_wstring(c.refuel_dock_id)},
            {STR("refuel_dock_name"), STR("'") + widen(c.refuel_dock_name) + STR("'")}};
        // WHAT: the wait, fuel and ore keys, appended in the same order that reload_config parses them.
        // WHY: the change diff matches entries by index, so every config must list them in one fixed order.
        {
            std::wstring s;
            for (auto& [r, a] : c.items) s += widen(r) + STR(":") + std::to_wstring(a) + STR(",");
            f.push_back({STR("items"), STR("'") + s + STR("'")});
        }
        f.push_back({STR("wait_seconds"), std::to_wstring(c.wait_seconds)});
        for (int i = 0; i < N_FUEL; ++i)
        {
            const std::string base = std::string("fuel_") + FUEL_KEY[i];
            f.push_back({widen(base + "_on"), std::to_wstring(c.fuel[i].on)});
            f.push_back({widen(base + "_threshold"), std::to_wstring(c.fuel[i].threshold)});
            f.push_back({widen(base + "_amount"), std::to_wstring(c.fuel[i].amount)});
        }
        // WHAT: dock lists (unload per ore, refuel) appended last, as "id,id,".
        for (int i = 0; i < N_ORE; ++i)
            f.push_back({widen(std::string("unload_docks_") + ORE_KEY[i]), ids_text(c.ore_docks[i])});
        f.push_back({STR("refuel_docks"), ids_text(c.refuel_docks)});
        return f;
    }

    // WHAT: joins the pairs into one "key=value " line.
    // WHY: the startup log line.
    std::wstring join_fields(const Fields& f)
    {
        std::wstring s;
        for (auto& [k, v] : f) s += k + STR("=") + v + STR(" ");
        return s;
    }





    // WHAT: reads MiningBench.ini. first=true also creates it with documented defaults when missing, and appends keys that older INI files lack (ensure_key). Later calls parse only.
    // On any failure the previous config is kept and the error is logged.
    // WHY: a typo in the INI must not disable the mod or crash the tick.
    void reload_config(bool first)
    {
        auto path = get_dll_dir() / "MiningBench.ini";
        try
        {
            // The default file text below documents every key for the user. Its lines are data, not comments of this file.
            if (first && !std::filesystem::exists(path))
            {
                std::ofstream o(path);
                o << "# MiningBench.ini - set your own values. Lines starting with # are comments.\n"
                     "# enabled: 1 = mod active, 0 = off\n"
                     "enabled=1\n"
                     "# resource: item name (object name or display name) or numeric item id to mine\n"
                     "resource=\n"
                     "# amount: ore amount to mine on this row (ore gained); 0 = unlimited (mine until cargo full)\n"
                     "amount=0\n"
                     "# dock_id: numeric dock ID (see docks.txt / Mining tab) OR the dock's in-game name (case-insensitive exact match first, then contains; must match exactly one dock)\n"
                     "dock_id=0\n"
                     "# dock_name_filter: optional. If non-empty, the dock whose in-game name equals (else contains) this text is used and dock_id is ignored\n"
                     "dock_name_filter=\n"
                     "# range_raw: raw PNum Value passed to MiningBeacon::SetRange once per dock (scale unknown, see 'Range raw' log lines). 0 = leave unchanged\n"
                     "range_raw=0\n"
                     "# max_radius: clamp for radius, e.g. 10km, 8000m or plain meters; 0 = no clamp, empty = 50km. UNCALIBRATED scale (raw = meters*65536)\n"
                     "max_radius=50km\n"
                     "# radius: mining radius, e.g. 5km, 2500m or plain meters; replaces range_raw when set. UNCALIBRATED scale\n"
                     "radius=\n"
                     "# per-ship overrides: add sections [ship.<id>] at the END of the file with keys enabled, repeat, resource, amount, items, dock_id, dock_name_filter, radius, idle_seconds; missing key = global value\n"
                     "# items: per-ship ordered queue ITEM_Cobalt:500,ITEM_Graphite:500; replaces resource/amount\n"
                     "# idle_seconds: mining loop idle limit: go home after this many idle seconds (no rock in range / no ore gained); refuel stall halt after wait_seconds. 0 = off\n"
                     "idle_seconds=0\n"
                     "# log_interval_seconds: how often the ship scan / discovery runs\n"
                     "log_interval_seconds=10\n"
                     "# order_retry_seconds: min seconds between order attempts per ship\n"
                     "order_retry_seconds=30\n"
                     "# max_attempts: max order attempts per ship per game session\n"
                     "max_attempts=3\n"
                     "# repeat: 1 = after unloading undock and mine again, 0 = stop and wait (per-ship key allowed). Not acted on yet in M3a\n"
                     "repeat=0\n"
                     "# trace_interval_seconds: seconds between per-ship trace log lines for enabled bench ships (global only)\n"
                     "trace_interval_seconds=5\n"
                     "# record: no effect since 0.0.1 (recorder removed)\n"
                     "record=0\n"
                     "# record_interval_seconds: seconds between recorder lines (global only)\n"
                     "record_interval_seconds=2\n"
                     "# refuel_dock_id: numeric dock id OR in-game dock name of the refuel dock (empty = none; per-ship key allowed)\n"
                     "refuel_dock_id=\n";
                Output::send<LogLevel::Normal>(STR("[MiningBenchMod] created default {}\n"), path.wstring());
            }
            // kv: global keys as text. skv: per-ship sections keyed by ship ID. has_sections: a [section] was seen, so ensure_key must not append after it.
            std::map<std::string, std::string> kv;
            std::map<int, std::map<std::string, std::string>> skv;
            bool has_sections = false;
            {
                std::ifstream f(path);
                if (!f) throw std::runtime_error("cannot open MiningBench.ini");
                std::map<std::string, std::string>* cur = &kv;
                std::string line;
                while (std::getline(f, line))
                {
                    line = trim(line);
                    if (line.empty() || line[0] == '#') continue;
                    if (line[0] == '[')
                    {
                        has_sections = true;
                        auto rb = line.find(']');
                        std::string name = trim(line.substr(1, rb == std::string::npos ? std::string::npos : rb - 1));
                        cur = nullptr;
                        if (name.rfind("ship.", 0) == 0)
                        {
                            std::string num = trim(name.substr(5));
                            if (!num.empty() && num.find_first_not_of("0123456789") == std::string::npos) cur = &skv[to_int(num, -1)];
                        }
                        continue;
                    }
                    auto eq = line.find('=');
                    if (eq == std::string::npos || !cur) continue;
                    (*cur)[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
                }
            }
            // WHAT: on first load only, appends the comment and "key=value" when the key is missing.
            // WHY: older INI files gain new keys without losing the user's edits. Skipped when [ship.<id>] sections exist, since an appended key would land inside the last section.
            auto ensure_key = [&](const char* k, const char* v, const char* comment) {
                if (!first || has_sections || kv.count(k)) return;
                std::ofstream o(path, std::ios::app);
                o << "\n# " << comment << "\n" << k << "=" << v << "\n";
                kv[k] = v;
            };
            ensure_key("order_retry_seconds", "30", "order_retry_seconds: min seconds between order attempts per ship");
            ensure_key("max_attempts", "3", "max_attempts: max order attempts per ship per game session");
            ensure_key("repeat", "0", "repeat: 1 = after unloading undock and mine again, 0 = stop and wait (per-ship key allowed)");
            ensure_key("trace_interval_seconds", "5", "trace_interval_seconds: seconds between per-ship trace log lines (global only)");
            ensure_key("record", "0", "record: no effect since 0.0.1 (recorder removed)");
            ensure_key("record_interval_seconds", "2", "record_interval_seconds: seconds between recorder lines (global only)");
            ensure_key("kill_switch", "0", "kill_switch: 1 = the mod stops touching the game completely (hot reload, global only)");
            ensure_key("warp_range", "50km", "warp_range: belt mode: a rock farther than this from the ship is not flown to, the ship warps to the next sector instead; also caps radius (m/km, empty/0 = 50km; per-ship key allowed)");
            ensure_key("mining_distance", "50m", "mining_distance: distance from the rock in m/km, e.g. 200m (empty = 50m; per-ship key allowed)");
            ensure_key("fuel_margin", "0", "fuel_margin: int fuel margin, 0 = unset (per-ship key allowed)");
            ensure_key("fuel_amount", "0", "fuel_amount: int fuel amount, 0 = unset (per-ship key allowed)");
            ensure_key("raw_per_meter", "65536", "raw_per_meter: raw PNum units per meter (calibration; global only)");
            ensure_key("steer_probe_speed_k", "60", "steer_probe_speed_k: speed limit = MaxSpeed * k, raw per second (global only)");
            ensure_key("refuel_dock_id", "", "refuel_dock_id: numeric dock id or in-game dock name; empty = none (per-ship key allowed)");
            ensure_key("range_raw", "0", "range_raw: raw PNum Value passed to MiningBeacon::SetRange once per dock (scale unknown, see 'Range raw' log lines). 0 = leave unchanged");
            ensure_key("dock_name_filter", "", "dock_name_filter: optional dock in-game name (equals, else contains); overrides dock_id when non-empty");
            {
                // WHAT: first-load defaults for the wait, fuel and ore keys, taken from the Config struct defaults.
                // WHY: the key and comment strings are locals, so the const char* given to ensure_key stay valid during the call.
                const Config def{};
                ensure_key("wait_seconds", std::to_string(def.wait_seconds).c_str(), "wait_seconds: seconds to wait at the dock after the order list (global only)");
                ensure_key("dock_hold_limit", std::to_string(def.dock_hold_limit).c_str(), "dock_hold_limit: max seconds at the dock for unload/refuel with items left before the ship undocks, waits 30 s and docks again (10 rounds, then undocks and goes idle; not an error; 0 = no limit; per-ship override ok)");
                ensure_key("warp_trip_km", std::to_string(def.warp_trip_km).c_str(), "warp_trip_km: a dock trip (unload/refuel/dock) longer than this many km warps if the ship has warp fuel, else it flies; per-ship override ok");
                for (int i = 0; i < N_FUEL; ++i)
                {
                    const std::string base = std::string("fuel_") + FUEL_KEY[i];
                    const std::string name = FUEL_KEY[i];
                    const std::string on = base + "_on", thr = base + "_threshold", amt = base + "_amount";
                    const std::string c_on = on + ": 1 = refuel with " + name + " (global only)";
                    const std::string c_thr = thr + ": refuel when " + name + " is below this (global only)";
                    const std::string c_amt = amt + ": amount to wait for / filter for " + name + " (global only)";
                    ensure_key(on.c_str(), std::to_string(def.fuel[i].on).c_str(), c_on.c_str());
                    ensure_key(thr.c_str(), std::to_string(def.fuel[i].threshold).c_str(), c_thr.c_str());
                    ensure_key(amt.c_str(), std::to_string(def.fuel[i].amount).c_str(), c_amt.c_str());
                }
            }

            // WHAT: builds the new global config nc from kv.
            // WHY: parse into a fresh struct; g_cfg is swapped only at the end, so a failure leaves the old config in place.
            Config nc; apply_kv(nc, kv, false);
            if (kv.count("range_raw")) nc.range_raw = to_int(kv["range_raw"], 0);
            if (kv.count("max_radius")) nc.max_radius = kv["max_radius"];
            if (kv.count("log_interval_seconds")) nc.log_interval_seconds = to_int(kv["log_interval_seconds"], 10);
            if (kv.count("order_retry_seconds")) nc.order_retry_seconds = to_int(kv["order_retry_seconds"], 30);
            if (kv.count("max_attempts")) nc.max_attempts = to_int(kv["max_attempts"], 3);
            if (kv.count("trace_interval_seconds")) nc.trace_interval_seconds = to_int(kv["trace_interval_seconds"], 5);
            // WHY: a 0 or negative interval would make the tick run every frame, so every timer gets a 1 s minimum (the other interval clamps below work the same way).
            if (nc.trace_interval_seconds < 1) nc.trace_interval_seconds = 1;
            if (kv.count("record")) nc.record = to_int(kv["record"], 0);
            if (kv.count("record_interval_seconds")) nc.record_interval_seconds = to_int(kv["record_interval_seconds"], 2);
            if (nc.record_interval_seconds < 1) nc.record_interval_seconds = 1;
            if (kv.count("kill_switch")) nc.kill_switch = to_int(kv["kill_switch"], 0) != 0;
            if (nc.log_interval_seconds < 1) nc.log_interval_seconds = 1;
            // WHAT: wait and per-fuel keys (global only). An empty or non-number value falls back to the struct default (to_int).
            // WHY: the INI may predate these keys; ensure_key below adds them on first load.
            if (kv.count("wait_seconds")) nc.wait_seconds = to_int(kv["wait_seconds"], 10);
            for (int i = 0; i < N_FUEL; ++i)
            {
                const std::string base = std::string("fuel_") + FUEL_KEY[i];
                if (kv.count(base + "_on")) nc.fuel[i].on = to_int(kv[base + "_on"], nc.fuel[i].on);
                if (kv.count(base + "_threshold")) nc.fuel[i].threshold = to_int(kv[base + "_threshold"], nc.fuel[i].threshold);
                if (kv.count(base + "_amount")) nc.fuel[i].amount = to_int(kv[base + "_amount"], nc.fuel[i].amount);
            }
            // raw per meter must be positive. The value also replaces g_raw_per_m, which the mod math uses.
            if (kv.count("raw_per_meter"))
            {
                int r = to_int(kv["raw_per_meter"], 0);
                if (r > 0) { nc.raw_per_meter = r; g_raw_per_m = static_cast<double>(r); }
            }
            if (kv.count("steer_probe_speed_k")) nc.steer_probe_speed_k = to_int(kv["steer_probe_speed_k"], 60);

            // WHAT: old and new key lists, diffed after the swap below.
            // WHY: g_cfg and g_ship_kv are replaced before the logs run, so the logs show the new values.
            Fields old_f = config_fields(g_cfg);
            Fields new_f = config_fields(nc);
            g_cfg = nc;
            g_ship_kv = skv;
            // WHAT: logs each [ship.<id>] section and its effective values.
            // WHY: so the user can check what the mod read.
            for (auto& [sid_, m_] : skv)
            {
                std::wstring ss;
                for (auto& [k_, v_] : m_) ss += widen(k_) + STR("=") + widen(v_) + STR(" ");
                Config ec = eff_cfg(sid_);
                log_line(STR("ship config id=") + std::to_wstring(sid_) + STR(" ") + ss + STR("| effective enabled=") + std::to_wstring(ec.enabled) + STR(" repeat=") + std::to_wstring(ec.repeat) + STR(" radius='") + widen(ec.radius) + STR("' (UNCALIBRATED, raw=meters*65536) mining_distance='") + widen(ec.mining_distance) + STR("'") +STR(" fuel_margin=") + std::to_wstring(ec.fuel_margin) + STR(" fuel_amount=") + std::to_wstring(ec.fuel_amount) + STR(" refuel_dock_id=") + std::to_wstring(ec.refuel_dock_id) + STR(" refuel_dock_name='") + widen(ec.refuel_dock_name) + STR("'"));
            }
            // WHY: startup logs the whole config once; later reloads log only the keys whose value changed.
            if (first)
            {
                log_line(STR("config ") + join_fields(new_f));
            }
            else
            {
                for (size_t i = 0; i < new_f.size(); ++i)
                    if (old_f[i].second != new_f[i].second)
                        log_line(STR("config changed ") + new_f[i].first + STR(": ") + old_f[i].second + STR(" -> ") + new_f[i].second);
                g_skip.clear();
                log_line(STR("config reloaded: ") + join_fields(new_f));
            }
            // remember the file time, so check_config() does not reload until the file changes.
            g_ini_mtime = std::filesystem::last_write_time(path);
        }
        catch (const std::exception& e)
        {
            // WHY: read or parse error: the previous config stays (g_cfg unchanged) and the reason is logged. A half-edited INI must not stop the mod.
            log_line(STR("config read failed, keeping previous config: ") + widen(e.what()));
            std::error_code ec;
            // also remember the new file time: a broken file is then not re-parsed on every tick.
            auto t = std::filesystem::last_write_time(path, ec);
            if (!ec) g_ini_mtime = t;
        }
    }

    // WHAT: reloads MiningBench.ini if its file time changed since the last load.
    // WHY: lets the user edit the INI without restarting the game. (A DLL change still needs a full restart.) Called from tick_work on every scan.
    void check_config()
    {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(get_dll_dir() / "MiningBench.ini", ec);
        if (!ec && t != g_ini_mtime) reload_config(false);
    }
}
