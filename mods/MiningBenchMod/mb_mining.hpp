// FILE: mods/MiningBenchMod/mb_mining.hpp
// WHAT: M3 mining loop: per bench ship state machine (check, undock, seek rock, mine, dock trip, hold at dock, stop), the
//       fuel/cargo checks that send the ship home, STOP ALL latch, Start/Stop loop run flag, and the MiningStatus.txt writer for the Lua Loop tab.
// WHY separate: the loop reuses the Test-tab command bus (run_cmd, job_start, move_start) and must run from cmd_tick, so it sits
//       between the job code and the Test-tab dispatcher.
// DEPENDS ON: all headers up to mb_dockjob.hpp (mb_common, mb_config, mb_ue, mb_rocks, mb_steer, mb_dockjob, mb_orders, mb_cmdstate).
//             run_cmd is forward-declared here and defined in mb_testcmd.hpp.
// USED BY: mb_testcmd.hpp (mining_tick from cmd_tick, mine_halt from run_cmd stop_all, mine_cmd/mine_cmd_all from run_cmd), dllmain.cpp (mine_set_bench from tick_work).
// CHUNK HOLD: mb_chunkhold.hpp (included after this file) keeps the chunks around a far bench ship loaded; forward-declared below, called from mine_step/mine_halt/mine_set_bench.
// GAME FACTS: ASSUMPTION: the loop runs only on the game thread (engine tick). Status file: MiningStatus.txt, lines
//       "<ship id>|<ship name>|<simple status>|<state>: <result>" (simple = idle/mining/traveling/refueling/unloading).
//       MiningStatusX.txt (same dir, written every second and at job start/end): "<ship id>|<run 0/1>|<cargo %>|<speed m/s>|<next state>|<left items or -1>|<job>|<jobphase>|<resource or ->|<sys>,<chx>,<chy>,<chz> or ->".
//       MiningLog.txt (same dir, written when changed): "<ship id>|<hh:mm:ss>|<text>" per Log tab line, last 20 per ship, oldest first (mlog / mine_log_write).
//       ROW LOGIC: each queue row amount = target ore mined (got). amount 0 = unlimited (mine until cargo full). Docks only on cargo full or fuel low.
//       Row reached -> next row, keep mining (no dock). Queue end: repeat on = wrap, repeat off = go home, unload, HALT.
//       Cargo probe: one "MINE cargo probe ship=" line per ship with capacity/size-like properties (names unconfirmed).
#pragma once

namespace
{
    std::wstring run_cmd(int seq, int sid, const std::string& cmd);   // defined in mb_testcmd.hpp
    // chunk hold (mb_chunkhold.hpp, defined after this file)
    void chunk_hold_update(int sid, UObject* ship);   // SEEK/MINE, arrival, CHECK nothing-in-range (undocked): set the ship's cube
    void chunk_hold_release(int sid);                 // drop the ship's cube (no-op when none)
    void chunk_hold_target_clear(int sid);                          // back to the ship's own chunk
    void chunk_hold_target(int sid, int sys, const int64_t c[3]);   // centre the ship's cube on this chunk instead of the ship's own (cleared by chunk_hold_release)
    void chunk_hold_tick(int sid);                    // forced rock passes at +1 s and +3 s after a set
    bool chunk_hold_settling(int sid);                // true while the ship's cube was set less than 4 s ago
    void chunk_hold_keep_only(const std::vector<int>& sids);   // release holds of ships no longer on the bench

    constexpr double MINE_FUEL_CHECK_S = 5.0;   // fuel check period while flying out / mining (s)
    constexpr double MINE_HOLD_CHECK_S = 2.0;   // at-dock completion check period (s)
    constexpr double MINE_BELT_EMPTY_S = 10.0;  // seek with no rock in range for this long -> warp to the belt sector with the ore (s)
    constexpr double MINE_WARP_TIMEOUT_S = 90.0; // warp not arrived after this long -> HALT (s, real warp: aim + charge)
    constexpr double MINE_FLEE_GRACE_S = 10.0;   // after STOP ALL no flee for this long (s)
    constexpr double MINE_DOCK_RETRY_WAIT_S = 30.0; // dock time limit: wait after the undock before docking again (s)

    enum MineState { MS_OFF = 0, MS_CHECK, MS_UNDOCK, MS_SEEK, MS_MINE, MS_TRIP, MS_HOLD, MS_HALT, MS_WARP, MS_DLWAIT, MS_COLLECT, MS_SWEEP };
    const TCHAR* mine_state_name(int s)
    {
        static const TCHAR* n[] = {STR("idle"), STR("checking"), STR("undocking"), STR("approaching rock"), STR("mining"), STR("flying to dock"), STR("at dock"), STR("stopped"), STR("warping"), STR("waiting to dock again"), STR("collecting ore"), STR("collecting ore (sweep)")};
        return n[s];
    }

    // WHAT: MS_SWEEP ore list entry: floating ore actor, its position (uu) at the last scan, done = already tried this sweep.
    struct SwOre { UObject* a = nullptr; double p[3] = {0, 0, 0}; bool done = false; };
    enum { SW_UNDOCK = 0, SW_RETURN, SW_FLY, SW_WARP, SW_PICK, SW_APPROACH, SW_PULL };

    // WHAT: per-ship mining loop state. anchor = ship actor position (uu) at undock = centre of the search area.
    struct MineSt
    {
        int st = MS_OFF;
        double t = 0.0, idle_t = 0.0, rng_t = 0.0, fuel_t = 0.0;
        double chk_t = MINE_ROCK_CHECK_S;      // first check runs at once
        int64_t last_ore = -1;
        int64_t got = 0;                       // ore mined on the current queue row (survives dock trips)
        double anchor[3] = {0, 0, 0};
        std::string trip;                      // "dock_refuel" | "dock_mining" for the current trip
        bool seek_started = false, no_rock = false, leave = false;
        bool fin = false;                      // queue finished (repeat off): unload, then stop
        bool noore = false;   // no sector has the ore: go home, then idle (HALT, not an error)
        int fails = 0, unl_chk = 0, rock = 0;
        int64_t unl_have = -1;                 // resource ore in cargo when the current trip started (-1 unknown): the unload guard counts only trips that did not shrink it
        bool run = false;                      // Start loop pressed (runtime only: a game restart means stopped)
        double hold_idle = 0.0;                // refuel stall: seconds with no fuel gained at dock
        int64_t hold_last = -1;                // refuel stall: fuel cargo total at the last check
        std::wstring result = STR("idle");
        double pos[3] = {0, 0, 0};             // ship actor location at the last status sample (uu)
        bool pos_ok = false, probed = false, wprobed = false;   // pos valid / cargo probe already logged
        double spd = 0.0;                      // speed (m/s) from the last status sample
        int cargo_pct = -1;                    // cargo fill % at the last status sample (-1 unknown)
        double clog_t = 30.0;                  // seconds since the last "MINE cargo" debug line (first sample logs)
        int64_t eta_left = -1;                 // items still to move while MS_HOLD; -1 = unknown / not holding
        double prog_t = 0.0;                   // seconds mining since the last "Mined" log line
        LogGate wait_gate, q_gate;             // dock wait Log tab gates (dock_wait_text); reset per trip
        bool dwait = false, err = false;       // dwait: waiting for a dock (queued in the dock network) / err: stopped by an error (red ERROR)
        double dwait_t = 0.0;                  // seconds since the last safety retry (60 s)
        double dwait_shown = -1.0;             // dwait: the second the wait text was last written (-1 = write now)
        int hold_ph = 0;                       // dwait sub-phase: 0 none, 1 flying/warping to the holding slot, 2 holding there
        bool hold_warp = false;                // hold_ph 1: a warp (true) or a point flight (false)
        double hold_t = 0.0;                   // hold_ph 1: seconds since the flight/warp to the slot started
        int dl_n = 0;                          // dock time limit: undock/wait/redock rounds used (reset on normal finish, STOP ALL, Start)
        double dl_wait_t = 0.0;                // MS_DLWAIT: seconds waited after the undock
        bool btn = false;                      // trip started by a Dock/Unload/Refuel button: m.leave is set on purpose, must not abort the dock wait
        bool hold_seen = false;                // MS_HOLD: "work done" already logged
        std::wstring left_last;                // MS_HOLD: last "left" text logged
        int warp_to = 0;                       // sector id of a pending or running belt warp (0 = none)
        double warp_t = 0.0;                   // seconds since the belt warp started (MS_WARP timeout)
        std::set<int> tried;                   // sectors already searched without a rock this belt run (cleared when mining starts)
        int32_t hull = -1;                     // Health at the last check (-1 = no baseline); a drop = attacked
        bool flee = false;                     // hull hit: the dock trip ends in HALT (trip_end)
        int64_t unl_prev = -1;                 // unload chain: docked-ore cargo after the last dock trip (-1 = first hop)
        bool unl_undock = false;               // MS_UNDOCK: the undock is the unload chain's (then dock_mining trip), not the seek undock
        double since_stop = 1e9;               // seconds since STOP ALL (mine_halt)
        double empty_t = 0.0;                  // CHECK: sum of check periods with no rock in range in a row (belt hop at MINE_BELT_EMPTY_S); reset on rock found / loop start / stop
        int col_rock = 0;                      // MS_COLLECT: rock id to return to after a cargo-full trip (0 = none)
        int col_next = 0;                      // MS_COLLECT end: 0 seek next rock, 1 go home (queue done)
        std::wstring col_why;                  // MS_COLLECT: reason it started (text shown at the end)
        double col_t = 0.0, col_stall = 0.0, col_scan_t = 0.0;   // MS_COLLECT: seconds total / since ore count dropped / since the last scan
        int col_last = -1;                     // MS_COLLECT: floating ore count at the last scan (-1 none, 0 scan failed)
        bool sweep = false;                    // MS_SWEEP active (collect ore action)
        int sw_ph = 0, sw_rescans = 0, sw_sys = 0;   // MS_SWEEP phase (SW_*), rescans left, saved system id
        int64_t sw_ch[3] = {0, 0, 0};          // MS_SWEEP: saved ship chunk (return point)
        double sw_loc[3] = {0, 0, 0};          // MS_SWEEP: saved ship raw location (return point)
        bool sw_back = false, sw_nofly = false;   // sw_back: return to the saved point after the unload; sw_nofly: flight back failed, warp instead
        double sw_t = 0.0;                     // MS_SWEEP: seconds in the current phase
        UObject* sw_ore = nullptr;             // MS_SWEEP: ore actor being picked up
        std::vector<SwOre> sw_list;            // MS_SWEEP: floating ore cache (nearest first when picking)
        int sw_found = -1;                     // MS_SWEEP: BP_FloatingOre_C count of the last scan (-1 none yet)
        bool sw_frame = false;                 // MS_SWEEP: ship frame readable at the last scan
        int sw_undock_n = 0;                   // MS_SWEEP: undock tries while docked and position unreadable (guard)
    };
    std::map<int, MineSt> g_mine;

    // WHAT: Log tab. Per ship, the last MINE_LOG_KEEP lines (local time hh:mm:ss + user-readable text), oldest first. Written to MiningLog.txt.
    constexpr size_t MINE_LOG_KEEP = 20;
    constexpr double MINE_PROGRESS_S = 30.0;   // "Mined x/y" line period while mining (s)
    struct MineLogLine { std::wstring hms; std::wstring text; };
    std::map<int, std::vector<MineLogLine>> g_mine_log;

    // WHAT: local wall clock as hh:mm:ss.
    std::wstring clock_hms()
    {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t b[16]; swprintf(b, 16, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return b;
    }

    // WHAT: appends one Log tab line for ship sid (drops the oldest past MINE_LOG_KEEP).
    void mlog(int sid, const std::wstring& text)
    {
        auto& v = g_mine_log[sid];
        v.push_back({clock_hms(), text});
        if (v.size() > MINE_LOG_KEEP) v.erase(v.begin());
    }

    // WHAT: display name of a resource key for the Log tab; falls back to the key text.
    std::wstring item_name(const std::string& res)
    {
        std::wstring why;
        UObject* it = item_c(res, why);
        std::wstring n = alive(it) ? obj_display(it) : std::wstring();
        return n.empty() ? widen(res) : n;
    }
    std::vector<int> g_bench_ids;
    std::map<int, std::wstring> g_bench_names;   // ship display name per bench ship id (status file)
    double g_mine_status_acc = 0.0;

    // WHAT: m.trip is the refuel trip (dock_refuel); else unload (dock_mining).
    bool is_refuel(const MineSt& m) { return m.trip == "dock_refuel"; }

    // WHAT: undock job result is a failure (not ok, not still waiting).
    bool undock_failed(const std::wstring& r) { return !ok_res(r) && r != STR("waiting"); }

    // set the status text; log only when it changes. shown = Log tab text (empty: the status text, prefixed "Stopped: " when halted).
    void mset(int sid, MineSt& m, const std::wstring& text, const std::wstring& shown = STR(""))
    {
        if (m.result == text) return;
        m.result = text;
        log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" [") + mine_state_name(m.st) + STR("] ") + text);
        if (!shown.empty()) mlog(sid, shown);
        else if (text.rfind(STR("at dock, waiting"), 0) != 0) mlog(sid, m.st == MS_HALT ? STR("Stopped: ") + text : text);   // per-item countdown would flood the 20-line ring
    }

    // laser + tractor on/off through the Test-command code (seq -1: no command result is touched)
    void mine_fire(int sid, bool on)
    {
        const std::wstring a = run_cmd(-1, sid, on ? "laser_on" : "laser_off");
        const std::wstring b = run_cmd(-1, sid, on ? "tractor_on" : "tractor_off");
        if (a.rfind(STR("error"), 0) == 0 || b.rfind(STR("error"), 0) == 0)
            log_line(STR("MINE fire ") + std::wstring(on ? STR("on") : STR("off")) + STR(" laser=") + a + STR(" tractor=") + b);
    }

    // stop firing, cancel this ship's move job, throttle/rotation to zero
    void mine_stop_work(UObject* ship, int sid)
    {
        mine_fire(sid, false);
        mv_cancel(sid);
        zero_all(ship);
    }


    // nearest rock holding `item` measured from the anchor (not the ship); nullptr when none or outside the radius
    UObject* rock_in_range(int sid, const double anchor[3], UObject* item, double rad_m)
    {
        rock_list_ensure();
        ActFrame f;
        std::copy(anchor, anchor + 3, f.loc);
        ActAim r;
        UObject* a = nearest_item_actor(sid, f, item, r);
        if (!a || !actor_ok(a)) return nullptr;
        if (rad_m > 0.0 && r.dist_m > rad_m) return nullptr;
        return a;
    }

    // WHAT: chunk (system + 3 ints) of the save-data asteroid nearest to the ship that holds `item` and is alive; the ore list is shared by all ships and refreshed at most once a minute.
    // WHY: the rock ACTORS only exist in loaded chunks (the player's scan list and the world scan never see the rest), but the save objects of every rock exist always.
    //      Holding the chunks around that rock makes its actor load, then the normal rock search finds it.
    // radius_m <= 0: no limit. Returns false when the ship position is unreadable or no rock of the ore is in the ship's system within the radius.
    // per ship: chunk -> times it was picked since the ship last had a rock in range
    std::map<int, std::map<std::tuple<int64_t, int64_t, int64_t>, int>>& save_rock_picks() { static std::map<int, std::map<std::tuple<int64_t, int64_t, int64_t>, int>> m; return m; }
    bool save_rock_nearest(int sid, UObject* ship, UObject* item, double radius_m, int& sys_out, int64_t out[3], int64_t& dist_chunks)
    {
        struct SaveRock { int sys; int64_t c[3]; };
        static std::map<std::wstring, std::pair<ULONGLONG, std::vector<SaveRock>>> cache;   // ore item name -> (time, rocks)
        int sys0 = 0; int64_t ch0[3] = {0, 0, 0};
        if (!alive(item) || !ship_pos(ship, sys0, ch0)) return false;
        auto& e = cache[item->GetName()];
        const ULONGLONG now = GetTickCount64();
        if (e.first == 0 || now - e.first >= 60000)
        {
            e.first = now;
            e.second.clear();
            std::vector<UObject*> saves;
            UObjectGlobals::FindAllOf(STR("SmallResourceAsteroidObject"), saves);
            for (UObject* o : saves)
            {
                if (!alive(o)) continue;
                auto* arr = o->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("ResourceItems"));
                bool has = false;
                for (int32_t i = 0; arr && i < arr->Num(); ++i) if ((*arr)[i] == item) has = true;
                if (!has) continue;
                bool al = true; get_bool_prop(o, STR("bAlive"), al);
                SaveRock r;
                r.sys = sys_id(o);
                if (!al || r.sys <= 0 || !get_chunk(o, r.c)) continue;
                e.second.push_back(r);
            }
            log_line(STR("SAVEROCK ") + item->GetName() + STR(" alive save rocks=") + std::to_wstring(e.second.size()));
        }
        // WHY: a rock that was picked many times and still gave no actor in range (mined out, or loaded but outside the radius) is skipped, so the next nearest is tried.
        auto& picks = save_rock_picks()[sid];
        const int64_t max_chunks = radius_m > 0.0 ? static_cast<int64_t>(radius_m / 2048.0) + 1 : INT64_MAX;
        int64_t best = -1;
        for (const SaveRock& r : e.second)
        {
            if (r.sys != sys0) continue;
            const int64_t d = std::max({std::llabs(r.c[0] - ch0[0]), std::llabs(r.c[1] - ch0[1]), std::llabs(r.c[2] - ch0[2])});
            if (d > max_chunks || (best >= 0 && d >= best)) continue;
            if (const auto pk = picks.find({r.c[0], r.c[1], r.c[2]}); pk != picks.end() && pk->second >= 6) continue;
            best = d; sys_out = r.sys; for (int i = 0; i < 3; ++i) out[i] = r.c[i];
        }
        dist_chunks = best;
        if (best >= 0) ++picks[{out[0], out[1], out[2]}];
        return best >= 0;
    }

    // WHAT: nothing in range for this ship: hold the chunks around the nearest save-data rock of the ore so its actor loads. true = a hold was requested.
    bool hold_nearest_save_rock(UObject* ship, int sid, UObject* item, double radius_m)
    {
        int sys = 0; int64_t c[3] = {0, 0, 0}, d = 0;
        if (!save_rock_nearest(sid, ship, item, radius_m, sys, c, d)) { chunk_hold_target_clear(sid); return false; }
        chunk_hold_target(sid, sys, c);
        chunk_hold_update(sid, ship);
        return true;
    }

    // true when Inventory.HasSpaceForItems({item, 1}) returns false. Unusable function -> false (logged once).
    bool cargo_full(UObject* ship, UObject* item)
    {
        UObject* inv = get_obj_prop(ship, STR("Inventory"));
        UFunction* fn = alive(inv) ? inv->GetFunctionByNameInChain(STR("HasSpaceForItems")) : nullptr;
        FProperty* ip = fn ? fn->FindProperty(FName(STR("Items"), FNAME_Add)) : nullptr;
        auto* sp = ip ? CastField<FStructProperty>(ip) : nullptr;
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        auto* rb = rp ? CastField<FBoolProperty>(rp) : nullptr;
        FProperty* fi = (sp && sp->GetStruct()) ? sp->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add)) : nullptr;
        FProperty* fa = (sp && sp->GetStruct()) ? sp->GetStruct()->FindProperty(FName(STR("Amount"), FNAME_Add)) : nullptr;
        if (!rb || !fi || !fa)
        {
            static bool warned = false;
            if (!warned) { warned = true; tlog(STR("cargo_full: HasSpaceForItems unusable")); }
            return false;
        }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        *reinterpret_cast<UObject**>(buf.data() + ip->GetOffset_Internal() + fi->GetOffset_Internal()) = item;
        *reinterpret_cast<int32_t*>(buf.data() + ip->GetOffset_Internal() + fa->GetOffset_Internal()) = 1;
        inv->ProcessEvent(fn, buf.data());
        return (buf[rp->GetOffset_Internal()] & rb->GetFieldMask()) == 0;
    }

    // WHAT: shared floating ore scan (all ships): BP_FloatingOre_C actors with their positions (uu) at scan time.
    // WHY: FindAllOf plus one K2 read per actor is the expensive part (about 9000 actors). Done at most every ORE_SCAN_MS
    //      (or forced: sweep start and rescans), once for all ships, not per ship every 2 s.
    struct OreEnt { UObject* a = nullptr; double p[3] = {0, 0, 0}; };
    struct OreScan { ULONGLONG ms = 0; int found = 0; std::vector<OreEnt> list; };
    OreScan g_ore_scan;
    constexpr ULONGLONG ORE_SCAN_MS = 60000;   // one scan per 60 s for the whole mod (forced: sweep start, rescans, sweep end)
    int g_ore_pub = -1;                        // count last written to MiningOre.txt (-1 = never)

    // WHAT: writes MiningOre.txt "count|unix_seconds" for the Lua Collect button, only when the count changed.
    void ore_publish(size_t n)
    {
        if (static_cast<int>(n) == g_ore_pub) return;
        g_ore_pub = static_cast<int>(n);
        try { write_atomic(get_dll_dir() / "MiningOre.txt", std::to_string(n) + "|" + std::to_string(static_cast<long long>(std::time(nullptr))) + "\n"); } catch (...) {}
    }

    // WHAT: the shared scan. Refreshed when forced or older than ORE_SCAN_MS (ore_scan_due). Returns the cache (valid until the next call).
    OreScan& ore_scan(bool force)
    {
        const ULONGLONG now = GetTickCount64();
        if (!ore_scan_due(force, g_ore_scan.ms, now, ORE_SCAN_MS)) return g_ore_scan;
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(STR("BP_FloatingOre_C"), v);
        g_ore_scan.list.clear();
        for (UObject* a : v)
        {
            if (!alive_seh(a)) continue;   // ore actors are not in the rock lists: actor_ok would always fail
            OreEnt e;
            e.a = a;
            std::wstring why;
            if (!actor_vec_call(a, STR("K2_GetActorLocation"), e.p, why)) continue;
            g_ore_scan.list.push_back(e);
        }
        g_ore_scan.found = (int)v.size();
        g_ore_scan.ms = now;
        ore_publish(g_ore_scan.list.size());
        return g_ore_scan;
    }

    // WHAT: floating ore actors (BP_FloatingOre_C) within range_m of the ship, from the shared scan. -1 = ship frame unreadable.
    // WHY: the collect state waits while dropped ore still floats near the ship. Cached positions; alive_seh only on entries in range.
    int ore_near(UObject* ship, double range_m)
    {
        ActFrame f;
        if (!act_frame(ship, f)) return -1;
        OreScan& sc = ore_scan(false);
        int n = 0;
        for (size_t i = 0; i < sc.list.size();)
        {
            const OreEnt& e = sc.list[i];
            double d[3] = {e.p[0] - f.loc[0], e.p[1] - f.loc[1], e.p[2] - f.loc[2]};
            if (norm3(d) / MV_UU_PER_M <= range_m)
            {
                if (!alive_seh(e.a)) { sc.list.erase(sc.list.begin() + (std::ptrdiff_t)i); continue; }   // picked up / gone
                ++n;
            }
            ++i;
        }
        return n;
    }

    // enter SEEK: next step scans for a rock at once
    void enter_seek(MineSt& m)
    {
        m.st = MS_SEEK; m.t = 0.0; m.chk_t = MINE_ROCK_CHECK_S; m.rng_t = 0.0; m.no_rock = false; m.seek_started = false;
        m.empty_t = 0.0;
    }

    // WHAT: next queue row for ship sid; wrapped = true when it passed the last row (back to row 0). No queue = 1 virtual row.
    void item_advance(int sid, bool& wrapped)
    {
        const Config c = eff_cfg(sid);
        const int n = c.has_items ? (int)c.items.size() : 1;
        g_item_idx[sid] = item_next(g_item_idx[sid], n, wrapped);
    }

    // WHAT: ship sid gives up its holding slot: a flight to it is cancelled (hold_ph 1, ship alive), the slot is freed on every dock, phase and hold reset.
    // WHY: the dock reservation is not touched here (a grant keeps it); net_leave_slot adds the queue/reservation part.
    void slot_clear(int sid)
    {
        const auto mi = g_mine.find(sid);
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (mi != g_mine.end() && mi->second.hold_ph == 1 && alive(ship))
        {
            if (mi->second.hold_warp) warp_cancel(sid);
            mv_cancel(sid);
        }
        if (mi != g_mine.end()) { mi->second.hold_ph = 0; mi->second.hold_t = 0.0; mi->second.hold_warp = false; }
        for (auto& kv : g_net_docks) net_slot_free(kv.second, sid);
        const auto si = g_net_ships.find(sid);
        if (si != g_net_ships.end()) { si->second.slot = 0; si->second.phase = 0; }
    }

    // WHAT: ship sid leaves the dock network for good (stop, halt, new trip, ship gone): holding slot cleared (slot_clear), then net_leave.
    void net_leave_slot(int sid)
    {
        g_dock_after_undock.erase(sid);   // a stopped or new trip does not start its dock command after an undock
        slot_clear(sid);
        net_leave(sid);
    }

    // WHAT: cancel this ship's running dock/undock job (same steps as stop_all: erase job, order/filter state, autopilot off).
    // WHY: a new dock trip or undock must not fail with "dock job already running for this ship".
    void cancel_dock_job(UObject* ship, int sid)
    {
        warp_cancel(sid);   // a warp waiting for this undock / aiming is cancelled too (a charging one finishes)
        net_leave_slot(sid);     // a new dock pick follows: the old queue place and reservation go
        g_unl_ores.erase(sid);
        bool any = false;
        for (size_t i = 0; i < g_jobs.size();)
        {
            if (g_jobs[i].sid != sid) { ++i; continue; }
            tlog(STR("dock job seq=") + std::to_wstring(g_jobs[i].seq) + STR(" cancelled by mining loop"));
            if (g_jobs[i].cmd != "undock") chunk_hold_target_clear(sid);   // same rule as job_finish: a far trip's cube target ends with the job
            g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(i));
            any = true;
        }
        if (!any) return;
        g_ob.erase(sid); g_pf.erase(sid);
        std::wstring err;
        set_autopilot(sid, false, err);
        zero_all(ship);
    }

    // WHAT: wait text of a ship queued for a dock (dock network verdict + elapsed time). A flee shows "Fleeing: waiting for dock".
    std::wstring net_wait_text(int sid, bool fleeing)
    {
        const auto it = g_net_ships.find(sid);
        if (it == g_net_ships.end() || it->second.waits_dock == 0) return STR("Waiting for dock");
        const NetShip& s = it->second;
        if (fleeing && (s.reason == NR_WAIT_BUSY || s.reason == NR_WAIT_QUEUE)) return STR("Fleeing: waiting for dock");
        int qpos = 0, qn = 0;
        const auto dit = g_net_docks.find(s.waits_dock);
        if (dit != g_net_docks.end())
        {
            qn = static_cast<int>(dit->second.queue.size());
            for (size_t i = 0; i < dit->second.queue.size(); ++i) if (dit->second.queue[i] == sid) qpos = static_cast<int>(i) + 1;
        }
        return widen(net_ship_text(s.reason, net_dock_label(s.waits_dock), s.blocked_by ? net_ship_label(s.blocked_by) : std::string(),
                                   g_now - s.wait_since, qpos, qn));
    }

    // WHAT: wait of a queued dock trip, every call (once a second). Status (m.result) is set directly: mset would log each changed second.
    // WHAT: Log tab only: one line at the wait start or when dock / holder / reason change, "Still waiting" every 60 s, and the queue
    //       place when it changes (at most every 10 s, only with more than one waiter). No elapsed time in those lines.
    void dock_wait_text(int sid, MineSt& m)
    {
        m.result = net_wait_text(sid, g_flee_pick.count(sid) != 0);
        const auto it = g_net_ships.find(sid);
        if (it == g_net_ships.end() || it->second.waits_dock == 0) return;
        const NetShip& s = it->second;
        const std::string key = std::to_string(s.waits_dock) + "|" + std::to_string(s.blocked_by) + "|" + net_reason_text(s.reason);
        const GateAct wa = log_gate(m.wait_gate, key, g_now, 0.0, 60.0);
        if (wa != GATE_NONE)
            mlog(sid, widen(net_wait_log(s.reason, net_dock_label(s.waits_dock), s.blocked_by ? net_ship_label(s.blocked_by) : std::string(),
                                         g_now - s.wait_since, wa == GATE_REMIND)));
        const auto dit = g_net_docks.find(s.waits_dock);
        const int qn = dit == g_net_docks.end() ? 0 : static_cast<int>(dit->second.queue.size());
        const int qpos = net_queue_pos(s);
        const std::string qk = qn > 1 ? std::to_string(qpos) + "/" + std::to_string(qn) : std::string();
        if (log_gate(m.q_gate, qk, g_now, 10.0, 1e18) == GATE_NEW && qn > 1)
            mlog(sid, widen("Queue " + std::to_string(qpos) + " of " + std::to_string(qn)));
    }

    // WHAT: dock id the ship is docked at (DockID, else the Dock object). 0 = none.
    int dock_of(UObject* ship)
    {
        int32_t did = 0;
        get_int_prop(ship, STR("DockID"), did);
        if (did <= 0) { UObject* d = get_obj_prop(ship, STR("Dock")); if (alive(d)) did = obj_id(d); }
        return did > 0 ? did : 0;
    }

    // WHAT: the ore cargo of the dock trip shrank at this dock: it takes ore, so the FULL mark goes.
    void net_full_clear(int did)
    {
        const auto it = g_net_docks.find(did);
        if (it != g_net_docks.end()) it->second.full = false;
    }

    // WHAT: MS_HOLD unload stalled (ore cargo not shrinking for idle_seconds): mark the dock FULL, undock at once (no 30 s wait), then the
    //       trip re-picks: the FULL dock is skipped, the next dock of the ore's list is used (net_pick).
    void dock_full_leave(UObject* ship, int sid, MineSt& m)
    {
        const int did = dock_of(ship);
        if (did > 0) { NetDock& nd = g_net_docks[did]; nd.id = did; nd.full = true; nd.full_since = g_now; }
        mlog(sid, STR("Dock ") + widen(net_dock_label(did)) + STR(" is full, undocking to try the next dock"));
        log_line(STR("NET dock=") + std::to_wstring(did) + STR(" FULL by=") + std::to_wstring(sid));
        cancel_dock_job(ship, sid);
        g_job_res.erase(sid);
        const std::wstring ur = job_start(sid, "undock", -1);
        if (undock_failed(ur)) { m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: undock failed: ") + ur, STR("ERROR: undock failed: ") + ur); return; }
        m.st = MS_DLWAIT; m.dl_wait_t = MINE_DOCK_RETRY_WAIT_S; m.left_last.clear();   // MS_DLWAIT: undock finishes, then the dock trip runs again at once
        mset(sid, m, STR("Dock full, undocking to try the next dock"), STR("Dock full, next dock"));
    }

    // WHAT: cargo sum of ores that have an unload dock (names = those ores); nodock = ores in cargo with no dock list.
    int64_t unload_pending(UObject* ship, const Config& c, std::wstring& names, std::wstring& nodock)
    {
        int64_t sum = 0;
        for (int i = 0; i < N_ORE; ++i)
        {
            std::wstring w;
            UObject* it = item_c(ORE_ITEM[i], w);
            const int64_t have = alive(it) ? inv_total(ship, it) : 0;
            if (have <= 0) continue;
            if (c.ore_docks[i].empty()) nodock += widen(ORE_KEY[i]) + STR(" ");
            else { sum += have; names += widen(ORE_KEY[i]) + STR(" "); }
        }
        return sum;
    }

    // start a dock trip chosen by the auto rule. Failure -> HALT with the reason.
    void trip_begin(UObject* ship, int sid, MineSt& m, const std::wstring& why, const std::string& force = "")
    {
        cancel_dock_job(ship, sid);
        std::wstring w;
        m.trip = force.empty() ? auto_rule(sid, ship, w) : force;
        {
            // a refuel with ore of a dock in cargo unloads first (Control rule); the refuel follows from CHECK with empty cargo
            std::wstring nm, nd;
            const bool ore = unload_pending(ship, eff_cfg(sid), nm, nd) > 0;
            if (m.trip == "dock_refuel" && net_unload_first(ore, P_REFUEL))
            {
                m.trip = "dock_mining";
                mlog(sid, STR("Control: ") + widen(net_ship_label(sid)) + STR(" must unload first, then refuel"));
                log_line(STR("CONTROL ship=") + std::to_wstring(sid) + STR(" must unload first, then refuel"));
            }
        }
        std::wstring wi;
        UObject* it0 = item_c(eff_cfg(sid).resource, wi);
        m.unl_have = alive(it0) ? inv_total(ship, it0) : -1;
        g_job_res.erase(sid);
        const std::wstring r = job_start(sid, m.trip, -1);
        m.t = 0.0; m.seek_started = false; m.no_rock = false;
        m.err = false; m.dwait = false; m.warp_to = 0;
        m.wait_gate = LogGate(); m.q_gate = LogGate();   // a new trip starts its own wait lines
        if (r == STR("waiting dock")) { m.st = MS_TRIP; m.dwait = true; m.dwait_t = 0.0; m.dwait_shown = -1.0; dock_wait_text(sid, m); return; }
        if (r == STR("nothing to unload")) { g_flee_pick.erase(sid); m.st = MS_HALT; mset(sid, m, STR("Nothing to unload")); return; }
        if (net_real_error(r)) { g_flee_pick.erase(sid); m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: ") + r.substr(7), STR("ERROR: ") + r.substr(7)); return; }
        if (r != STR("waiting")) { g_flee_pick.erase(sid); m.st = MS_HALT; mset(sid, m, STR("dock trip failed: ") + r); return; }
        g_flee_pick.erase(sid);   // the flee pick is used up once its job runs
        m.st = MS_TRIP;
        const std::wstring shown = is_refuel(m) ? STR("Refueling: ") + w : STR("Docking to unload: ") + why;
        mset(sid, m, STR("going to ") + std::wstring(is_refuel(m) ? STR("refuel dock") : STR("mining dock")) + STR(" (") + why + STR("; ") + w + STR(")"), shown);
    }

    // stop work and fly home (auto rule picks refuel or unload)
    void go_home(UObject* ship, int sid, MineSt& m, const std::wstring& why)
    {
        mine_stop_work(ship, sid);
        trip_begin(ship, sid, m, why);
    }

    // WHAT: start MS_COLLECT: laser off, tractor on, ship holds still while floating ore is pulled in. kind: col_next (0 seek next rock, 1 go home).
    // WHY: ore dropped by the laser must be pulled in before the loop leaves the rock or docks, else it is lost.
    void begin_collect(UObject* ship, int sid, MineSt& m, int kind, const std::wstring& text)
    {
        run_cmd(-1, sid, "laser_off");
        run_cmd(-1, sid, "tractor_on");
        mv_cancel(sid);
        zero_all(ship);
        m.st = MS_COLLECT; m.t = 0.0; m.col_t = 0.0; m.col_stall = 0.0; m.col_scan_t = COLLECT_SCAN_S; m.col_last = -1;
        m.col_next = kind; m.col_why = text;
        mset(sid, m, STR("collecting leftover ore"), STR("Collecting leftover ore"));
    }

    // 0 = nothing to do (belt off / already in target sector / unreadable), 1 = warp started (MS_WARP), -1 = HALT set
    // mark_cur: the current sector has no rock left, so it is skipped when choosing the next one
    int belt_go(UObject* ship, int sid, MineSt& m, const Config& c, UObject* item, bool mark_cur)
    {
        if (belt_off(c.belt)) return 0;
        if (mark_cur) { const int c0 = cur_sector_id(ship); if (c0 > 0) m.tried.insert(c0); }
        int cur = 0;
        std::wstring nm, why;
        const int tg = belt_target(ship, c.belt, item, m.tried, cur, nm, why);
        if (tg == -2) { m.fin = true; m.noore = true; go_home(ship, sid, m, why); return -1; }
        if (tg < 0) return 0;
        if (tg == 0) { mine_stop_work(ship, sid); m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: ") + why, STR("ERROR: ") + why); return -1; }
        if (tg == cur) return 0;
        const std::wstring r = warp_real_cmd(sid, std::to_string(tg));
        if (!ok_res(r)) { m.tried.insert(tg); mine_stop_work(ship, sid); m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: warp failed: ") + r, STR("ERROR: warp failed: ") + r); return -1; }
        mine_stop_work(ship, sid);
        m.col_rock = 0;   // a belt warp leaves the rock behind: no return to it
        m.warp_to = tg; m.warp_t = 0.0; m.chk_t = 0.0; m.st = MS_WARP;
        mset(sid, m, STR("warping to sector ") + std::to_wstring(tg), STR("Warping to ") + nm + STR(" (") + std::to_wstring(tg) + STR(")"));
        return 1;
    }

    // after an undock: warp first when a belt warp is pending, else seek. true = warping (or already halted).
    bool after_undock(UObject* ship, int sid, MineSt& m, const Config& c, UObject* item)
    {
        if (m.warp_to > 0) { m.warp_to = 0; if (belt_go(ship, sid, m, c, item, false) != 0) return true; }
        enter_seek(m);
        return false;
    }

    // trip finished at the dock: autopilot off, then continue / stop. Orders are left in place (next build clears them, undock reuses a DeliverOrder).
    void trip_end(UObject* ship, int sid, MineSt& m, const Config& c)
    {
        std::wstring err;
        set_autopilot(sid, false, err);
        zero_all(ship);
        // hull hit: fled to dock, the trip is over for good
        if (m.flee) { m.flee = false; m.fin = false; m.err = true; m.st = MS_HALT; mset(sid, m, STR("ERROR: hull damaged, fled, holding near dock, stopped"), STR("ERROR: ship was attacked, fled, holding near dock, stopped")); return; }
        // unload chain: another dock trip while docked ores are still in cargo and it shrank; an ore with no dock left = HALT
        if (!is_refuel(m) && !g_dock_pick.count(sid))
        {
            std::wstring names, nodock;
            const int64_t now = unload_pending(ship, c, names, nodock);
            if (unload_chain_more(m.unl_prev, now))
            {
                m.unl_prev = now;
                mlog(sid, STR("Unload: undock before next dock, ore: ") + names);
                cancel_dock_job(ship, sid);
                g_job_res.erase(sid);
                const std::wstring r = job_start(sid, "undock", -1);
                if (r == STR("waiting")) { m.unl_undock = true; m.st = MS_UNDOCK; m.t = 0.0; mset(sid, m, STR("undocking for next dock")); return; }
                if (!ok_res(r)) { m.sweep = false; m.fin = false; m.err = true; m.st = MS_HALT; mset(sid, m, STR("ERROR: undock failed: ") + r, STR("ERROR: undock failed: ") + r); return; }
                trip_begin(ship, sid, m, STR("more ore to unload: ") + names, "dock_mining");
                return;
            }
            m.unl_prev = -1;
            if (!nodock.empty())
            {
                m.sweep = false; m.fin = false; m.err = true; m.st = MS_HALT;
                mlog(sid, STR("ERROR: no unload dock for ") + nodock);
                mset(sid, m, STR("ERROR: no unload dock for ") + nodock, STR("ERROR: no unload dock for ") + nodock);
                return;
            }
        }
        else m.unl_prev = -1;
        // sweep: unloaded, back to the sweep (undock, return to the field)
        if (m.sweep)
        {
            g_job_res.erase(sid);   // the dock trip result must not count as the undock result
            m.sw_ph = SW_UNDOCK; m.sw_nofly = false; m.st = MS_SWEEP; m.leave = false; m.t = 0.0;
            log_line(STR("SWEEP ship=") + std::to_wstring(sid) + STR(" ph=0 after dock trip"));
            mset(sid, m, STR("unloaded, returning to sweep"), STR("Collecting ore"));
            return;
        }
        const bool refuel = is_refuel(m);
        m.t = 0.0;
        if (m.leave) { m.st = MS_HALT; mset(sid, m, STR("switched off, parked at dock")); return; }
        if (!refuel && m.fin) { m.fin = false; const bool no = m.noore; m.noore = false; m.st = MS_HALT; if (no) mset(sid, m, STR("idle: no sector has the ore, parked at base"), STR("Idle: no ore in any sector")); else mset(sid, m, STR("queue done (repeat off), waiting for instruction")); return; }
        m.st = MS_CHECK; m.fails = 0;
        mset(sid, m, STR("trip done, checking"));
    }

    bool player_flies(UObject* ship)
    {
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        int32_t p = -1;
        return alive(ctrl) && get_int_prop(ship, STR("PilotID"), p) && p == obj_id(ctrl);
    }

    void flee_now(UObject* ship, int sid, MineSt& m, int32_t was, int32_t h)
    {
        m.hull = -1;
        mine_stop_work(ship, sid);
        m.flee = true; m.leave = false;
        mlog(sid, STR("ERROR: hull hit (") + std::to_wstring(was) + STR(" -> ") + std::to_wstring(h) + STR("), fleeing to dock"));
        g_flee_pick.insert(sid);   // dock pick purpose P_FLEE (mb_net.hpp)
        trip_begin(ship, sid, m, STR("hull hit ") + std::to_wstring(was) + STR(" -> ") + std::to_wstring(h), "dock_mining");
        if (m.st == MS_HALT) { m.err = true; m.flee = false; }
    }

    // WHAT: true when the ship is parked at a dock (Dock object alive and DockID set).
    bool ship_docked(UObject* ship)
    {
        UObject* dk = get_obj_prop(ship, STR("Dock"));
        int32_t did = 0;
        get_int_prop(ship, STR("DockID"), did);
        return alive(dk) && did != 0;
    }

    // WHAT: steps before the state switch of mine_step (0 hull hit on idle, 1 switched off, 1b hull hit while flying, 2 fuel check).
    //       true = step ended the tick (caller returns); false = run the state switch.
    bool step_pre(UObject* ship, int sid, MineSt& m, double dt, bool on)
    {
        // 0. hull hit on idle/stopped ship: same flee as 1b. Skipped: fleeing, STOP ALL < MINE_FLEE_GRACE_S ago, docked, player pilots it.
        m.since_stop += dt;
        if (m.st == MS_OFF || m.st == MS_HALT)
        {
            int32_t h = 0;
            const bool docked = ship_docked(ship);
            if (m.flee || m.since_stop < MINE_FLEE_GRACE_S || docked || !get_int_prop(ship, STR("Health"), h) || player_flies(ship)) m.hull = -1;
            else if (hull_dropped(m.hull, h)) { flee_now(ship, sid, m, m.hull, h); return true; }
            else m.hull = h;
        }
        // 1. switched off: idle states reset; flying states stop work and fly home; trip states finish and park
        if (!on)
        {
            if (m.st == MS_HALT) return true;   // keep the stop reason; Start resets it
            if (m.st == MS_OFF || m.st == MS_CHECK) { chunk_hold_release(sid); m.st = MS_OFF; m.leave = false; m.empty_t = 0.0; mset(sid, m, m.run ? STR("idle: Enabled is off") : STR("idle")); return true; }
            if (m.st == MS_SEEK || m.st == MS_MINE || m.st == MS_WARP || m.st == MS_COLLECT) { m.leave = true; go_home(ship, sid, m, STR("mining switched off")); return true; }
            m.leave = true;   // UNDOCK / TRIP / HOLD: let them end, then park
        }
        else m.leave = false;
        // 1b. hull hit while flying / mining / warping: stop work, dock trip (flee), trip_end halts with the red ERROR
        if (m.st == MS_SEEK || m.st == MS_MINE || m.st == MS_WARP || m.st == MS_COLLECT || m.st == MS_SWEEP)
        {
            int32_t h = 0;
            if (get_int_prop(ship, STR("Health"), h))
            {
                if (m.hull < 0) log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" hull baseline ") + std::to_wstring(h));
                if (hull_dropped(m.hull, h)) { flee_now(ship, sid, m, m.hull, h); return true; }
                m.hull = h;
            }
        }
        else if (m.st != MS_OFF && m.st != MS_HALT) m.hull = -1;
        // 2. fuel check while flying out / mining
        if (m.st == MS_SEEK || m.st == MS_MINE || m.st == MS_COLLECT || m.st == MS_SWEEP)
        {
            m.fuel_t += dt;
            if (m.fuel_t >= MINE_FUEL_CHECK_S)
            {
                m.fuel_t = 0.0;
                std::wstring w;
                if (auto_rule(sid, ship, w) == "dock_refuel") { go_home(ship, sid, m, STR("fuel low: ") + w); return true; }
            }
        }
        return false;
    }

    // WHAT: MS_OFF: idle check (rock + config), then CHECK.
    void st_off(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        m.err = false;
        m.chk_t += dt;
        if (m.chk_t < MINE_ROCK_CHECK_S) return;
        m.chk_t = 0.0;
        if (c.resource.empty() || parse_distance(c.mining_distance) < 0.0) { mset(sid, m, STR("not configured: resource, amount, mining distance")); return; }
        g_sel_rocks.erase(sid);   // the loop picks the nearest rock of the chosen ore; a leftover Test rock_sel must not hijack it
        m.st = MS_CHECK; m.t = 0.0; m.fails = 0; m.unl_chk = 0;   // empty_t is NOT reset here: "nothing in range" returns to MS_OFF, so this runs every check and would wipe the count
        mset(sid, m, STR("checking"));
        return;
    }
    // WHAT: "cargo still full after unload" guard. true = fire (caller HALTs). A shrink of the resource ore since the trip began resets the count.
    //       Counts only when the last dock job ended ok; never while waiting for a dock (dwait, or the job result is "waiting dock").
    // WHY: a trip that found its dock busy, or unloaded nothing because the dock did not take the ore, is not a failed unload to count.
    bool unl_guard_fire(UObject* ship, int sid, MineSt& m, UObject* item)
    {
        const int64_t now = alive(item) ? inv_total(ship, item) : -1;
        if (m.unl_have >= 0 && now >= 0 && now < m.unl_have) { m.unl_chk = 0; return false; }
        if (m.dwait || !ok_res(g_job_res[sid])) return false;
        return m.unl_chk++ >= 1;
    }
    // WHAT: MS_CHECK: fuel/queue/cargo decisions, belt target, rock-in-range test, then undock job.
    void st_check(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        std::wstring why;
        UObject* item = item_c(c.resource, why);
        if (!alive(item)) { m.st = MS_OFF; m.chk_t = 0.0; mset(sid, m, STR("error: ") + why); return; }
        // not docked: no trip, go straight to the rock check (job_start "undock" answers "ok already free")
        std::wstring w;
        if (auto_rule(sid, ship, w) == "dock_refuel") { trip_begin(ship, sid, m, STR("fuel low")); return; }
        if (m.fin) { trip_begin(ship, sid, m, STR("queue done, unloading")); return; }
        if (cargo_full(ship, item) || m.cargo_pct >= 100)   // pct 100 = every non-fuel slot used (a partial stack can still take 1: HasSpaceForItems says yes)
        {
            // loop guard: a second unload from CHECK without mining in between means the cargo is not in the unload list
            if (unl_guard_fire(ship, sid, m, item)) { m.st = MS_HALT; mset(sid, m, STR("cargo still full after unload (does the ore have an unload dock?)")); return; }
            trip_begin(ship, sid, m, STR("cargo ready to unload"));
            return;
        }
        ActFrame f;
        if (!act_frame(ship, f)) { m.st = MS_OFF; m.chk_t = 0.0; mset(sid, m, STR("ship actor unreadable")); return; }
        std::copy(f.loc, f.loc + 3, m.anchor);
        const bool in_range = rock_in_range(sid, m.anchor, item, mine_radius_m(c));   // one scan for all the checks below
        // belt: ship not in the target sector -> warp after the undock (after_undock); HALT when no sector has the ore
        if (!belt_off(c.belt) && !in_range)   // rock in range here -> mine it, warp only when none
        {
            int cur = 0;
            std::wstring nm, why2;
            const int t = belt_target(ship, c.belt, item, m.tried, cur, nm, why2);
            if (t == -2) { m.fin = true; m.noore = true; go_home(ship, sid, m, why2); return; }
            if (t == 0) { m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: ") + why2, STR("ERROR: ") + why2); return; }
            if (t > 0 && t != cur) m.warp_to = t;
        }
        if (m.warp_to == 0 && !in_range)
        {
            // undocked ship (far sector, Start loop): hold its chunk cube so the next check sees the rocks. The belt counter waits while it settles.
            const bool docked = ship_docked(ship);
            if (!hold_nearest_save_rock(ship, sid, item, mine_radius_m(c)) && !docked) chunk_hold_update(sid, ship);
            if (!chunk_hold_settling(sid)) m.empty_t += MINE_ROCK_CHECK_S;
            // belt, ship not docked: no rock in range for MINE_BELT_EMPTY_S and none of the ore at all (unlimited) -> next sector (same as SEEK)
            // WHY: the HUD list is the PLAYER's scan list and can miss whole ores (silica rocks seen on the HUD were not in it); a docked ship or belt off never asked for the world scan.
            rock_fallback_request();
            if (!belt_off(c.belt) && !docked)
            {
                if (m.empty_t >= MINE_BELT_EMPTY_S && !in_range && belt_go(ship, sid, m, c, item, true) != 0) return;
            }
            m.st = MS_OFF; m.chk_t = 0.0; mset(sid, m, STR("nothing in range")); return;   // Update 15: do not undock
        }
        m.empty_t = 0.0;
        cancel_dock_job(ship, sid);
        g_job_res.erase(sid);
        const std::wstring r = job_start(sid, "undock", -1);
        if (r == STR("waiting")) { m.st = MS_UNDOCK; m.t = 0.0; mset(sid, m, STR("undocking")); return; }
        if (ok_res(r)) { if (!after_undock(ship, sid, m, c, item)) mset(sid, m, STR("already free, seeking rock")); return; }
        m.st = MS_HALT; mset(sid, m, STR("undock failed: ") + r);
        return;
    }
    // WHAT: MS_UNDOCK: waits for the undock job, then seek.
    void st_undock(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        for (const DockJob& j : g_jobs) if (j.sid == sid) return;
        const std::wstring r = g_job_res[sid];
        if (m.unl_undock)   // unload chain: undock done, next dock_mining trip (trip_end)
        {
            m.unl_undock = false;
            if (ok_res(r)) { trip_begin(ship, sid, m, STR("more ore to unload"), "dock_mining"); return; }
            m.sweep = false; m.fin = false; m.err = true; m.st = MS_HALT; mset(sid, m, STR("ERROR: undock failed: ") + r, STR("ERROR: undock failed: ") + r);
            return;
        }
        if (ok_res(r)) { std::wstring wy; UObject* it2 = item_c(c.resource, wy); if (!after_undock(ship, sid, m, c, it2)) mset(sid, m, STR("undocked, seeking rock")); }
        else { m.st = MS_HALT; mset(sid, m, STR("undock failed: ") + r); }
        return;
    }
    // WHAT: MS_SEEK: approach the nearest rock (move job), belt warp / idle rules.
    void st_seek(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        if (mv_active(sid)) return;   // this ship's own move job is still running
        std::wstring why;
        UObject* item = item_c(c.resource, why);
        if (!alive(item)) { mine_stop_work(ship, sid); m.st = MS_HALT; mset(sid, m, STR("error: ") + why); return; }
        if (m.seek_started)
        {
            m.seek_started = false;
            const std::wstring r = g_mv_res[sid];
            if (ok_res(r))
            {
                m.fails = 0; m.unl_chk = 0;
                m.tried.clear();   // a rock is being mined: every sector may be searched again later
                chunk_hold_target_clear(sid); save_rock_picks().erase(sid);   // at the rock: hold the ship's own chunks again
                m.last_ore = inv_total(ship, item);
                if (m.col_rock > 0)   // back at the rock left for cargo full: collect the floating ore before mining
                {
                    m.col_rock = 0; g_sel_rocks.erase(sid);
                    begin_collect(ship, sid, m, 0, STR("leftovers collected, seeking next rock"));
                    return;
                }
                m.st = MS_MINE; m.t = 0.0; m.idle_t = 0.0; m.chk_t = 0.0;
                mine_fire(sid, true);
                mset(sid, m, STR("mining rock ") + std::to_wstring(m.rock), STR("Mining ") + item_name(c.resource) + STR(" (rock ") + std::to_wstring(m.rock) + STR(")"));
                return;
            }
            // rock lost (another ship mined it) = free retry, not a failed attempt
            if (r.find(STR("rock lost")) == std::wstring::npos && ++m.fails >= std::max(1, c.max_attempts)) { mine_stop_work(ship, sid); m.st = MS_HALT; mset(sid, m, STR("approach failed ") + std::to_wstring(m.fails) + STR("x: ") + r); return; }
            mset(sid, m, STR("approach ended: ") + r + STR(", retrying"));
        }
        // cargo full with ore still floating: the next seek returns to that rock first (set in st_mine)
        if (m.col_rock > 0) { if (actor_ok(rock_actor_by_id(m.col_rock))) g_sel_rocks[sid] = m.col_rock; else m.col_rock = 0; }
        if (m.no_rock && !chunk_hold_settling(sid)) m.rng_t += dt;   // the belt counter starts 4 s after the hold was set
        m.chk_t += dt;
        if (m.chk_t >= MINE_ROCK_CHECK_S)
        {
            m.chk_t = 0.0;
            UObject* rock = rock_in_range(sid, m.anchor, item, mine_radius_m(c));
            m.no_rock = !rock;
            if (!rock)
            {
                mset(sid, m, STR("nothing in range"));
                hold_nearest_save_rock(ship, sid, item, mine_radius_m(c));   // the rock may exist only as save data: load its chunks
                rock_fallback_request();   // any seek (belt on or off): the HUD list can miss ores; belt seek: the next rock_in_range may use the FindAllOf fallback
                // belt: no rock in range for MINE_BELT_EMPTY_S and no rock of the ore at all (unlimited search) -> warp to the next sector
                if (!belt_off(c.belt) && m.rng_t >= MINE_BELT_EMPTY_S && !rock_in_range(sid, m.anchor, item, mine_radius_m(c)) && belt_go(ship, sid, m, c, item, true) != 0) return;
            }
            else
            {
                m.rng_t = 0.0;
                g_mv_res.erase(sid);
                const std::wstring r = move_start(sid, "approach_rock", -1);
                if (r == STR("waiting")) { m.seek_started = true; m.rock = g_mvs[sid].rock_id; mset(sid, m, STR("approaching rock ") + std::to_wstring(m.rock), STR("Approaching ") + item_name(c.resource) + STR(" rock ") + std::to_wstring(m.rock)); }
                else
                {
                    mset(sid, m, STR("approach not started: ") + r);
                    if (++m.fails >= std::max(1, c.max_attempts)) { m.st = MS_HALT; mset(sid, m, STR("approach failed ") + std::to_wstring(m.fails) + STR("x: ") + r); return; }
                }
            }
        }
        if (m.no_rock && c.idle_seconds > 0 && m.rng_t >= c.idle_seconds)
        {
            m.col_rock = 0; g_sel_rocks.erase(sid);
            go_home(ship, sid, m, STR("nothing in range for ") + std::to_wstring(c.idle_seconds) + STR(" s"));
        }
        return;
    }
    // WHAT: MS_MINE: mining progress, row target, cargo full / idle / rock gone.
    void st_mine(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        std::wstring why;
        UObject* item = item_c(c.resource, why);
        if (!alive(item)) { mine_stop_work(ship, sid); m.st = MS_HALT; mset(sid, m, STR("error: ") + why); return; }
        const int64_t inv = inv_total(ship, item);
        if (inv >= 0)
        {
            if (inv > m.last_ore) { m.got += got_delta(inv, m.last_ore); m.idle_t = 0.0; }
            else m.idle_t += dt;
            m.last_ore = inv;
        }
        const bool full = cargo_full(ship, item) || m.cargo_pct >= 100;   // m.cargo_pct: sampled every status second (mine_sample)
        m.chk_t += dt;
        if (m.chk_t >= MINE_ROCK_CHECK_S)
        {
            m.chk_t = 0.0;
            log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" ore=") + i64w(inv) + STR("/") + std::to_wstring(c.amount) + STR(" full=") + std::to_wstring(full ? 1 : 0) +
                     STR(" pct=") + std::to_wstring(m.cargo_pct) +
                     STR(" idle_t=") + std::to_wstring(m.idle_t) + STR(" rock=") + std::to_wstring(m.rock));
        }
        m.prog_t += dt;
        if (m.prog_t >= MINE_PROGRESS_S)
        {
            m.prog_t = 0.0;
            mlog(sid, STR("Mined ") + std::to_wstring(inv >= 0 ? inv : m.got) + (c.amount > 0 ? STR("/") + std::to_wstring(c.amount) : std::wstring()) + STR(" ") + item_name(c.resource));
        }
        if (row_done(c.amount, inv >= 0 ? inv : m.got))   // row target = ore in the ship inventory (read, not counted)
        {
            // row target reached: next row (no dock). Last row without repeat = queue done -> go home, unload, stop.
            bool wrapped = false;
            const std::wstring from = item_name(c.resource);
            item_advance(sid, wrapped);
            m.got = 0;
            if (wrapped && !c.repeat) { m.fin = true; begin_collect(ship, sid, m, 1, c.has_items ? STR("queue done") : STR("amount reached")); return; }
            if (wrapped) { begin_collect(ship, sid, m, 1, STR("queue done, repeat")); return; }   // repeat loop = mine, dock + unload, mine again
            if (c.has_items) mlog(sid, STR("Row done: ") + from + STR(" -> ") + item_name(eff_cfg(sid).resource));
            begin_collect(ship, sid, m, 0, STR("row done, next item"));
            return;
        }
        if (full)
        {
            // ore still floating: remember the rock, the next seek returns to it after the unload
            if (ore_near(ship, COLLECT_RANGE_M) > 0) { m.col_rock = m.rock; mlog(sid, STR("Cargo full, ore left: returning after unload")); }
            go_home(ship, sid, m, STR("cargo full"));
            return;
        }
        // no gain = rock spent/destroyed, not a reason to dock: look for another rock (SEEK goes home itself when none is in range)
        if (c.idle_seconds > 0 && m.idle_t >= c.idle_seconds) { begin_collect(ship, sid, m, 0, STR("no ore gained for ") + std::to_wstring(c.idle_seconds) + STR(" s, seeking next rock")); return; }
        if (m.rock > 0 && !actor_ok(rock_actor_by_id(m.rock))) { begin_collect(ship, sid, m, 0, STR("rock gone, seeking next rock")); return; }
        return;
    }
    // WHAT: MS_COLLECT: ship holds still with the tractor on until no floating ore is within COLLECT_RANGE_M, then seeks the next rock (col_next 0) or goes home (1).
    // WHY: ore the laser dropped is lost if the loop leaves now. Caps: COLLECT_MAX_S total, COLLECT_STALL_S with no drop in the count.
    void st_collect(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        if (chunk_hold_settling(sid)) return;
        std::wstring why;
        UObject* item = item_c(c.resource, why);
        const int64_t inv = alive(item) ? inv_total(ship, item) : -1;
        if (inv >= 0) { if (m.last_ore >= 0 && inv > m.last_ore) m.got += got_delta(inv, m.last_ore); m.last_ore = inv; }
        m.col_t += dt; m.col_stall += dt; m.col_scan_t += dt;
        if (m.col_scan_t < COLLECT_SCAN_S) return;
        m.col_scan_t = 0.0;
        const int n = ore_near(ship, COLLECT_RANGE_M);
        if (n < 0) m.col_last = 0;
        else { if (m.col_last < 0 || n < m.col_last) m.col_stall = 0.0; m.col_last = n; }
        const int cnt = n < 0 ? 0 : n;
        const CollectVerdict v = collect_verdict(cnt, m.col_t, m.col_stall);
        if (v == COL_WAIT) return;
        mine_fire(sid, false);
        log_line(STR("COLLECT ship=") + std::to_wstring(sid) + STR(" end=") + (v == COL_GIVEUP ? STR("gave up") : STR("done")) + STR(" left=") + std::to_wstring(cnt) + STR(" t=") + std::to_wstring(m.col_t));
        mlog(sid, v == COL_GIVEUP ? STR("Gave up collecting ") + std::to_wstring(cnt) + STR(" ore (timeout)") : STR("Leftover ore collected"));
        if (m.col_next == 1) { go_home(ship, sid, m, m.col_why); return; }
        enter_seek(m);
        mset(sid, m, m.col_why);
    }

    // WHAT: MS_SWEEP phase change: set the phase, reset the phase timer, log it (phase number, docked state, back flag).
    void sw_phase(UObject* ship, int sid, MineSt& m, int ph)
    {
        m.sw_ph = ph; m.sw_t = 0.0;
        log_line(STR("SWEEP ship=") + std::to_wstring(sid) + STR(" ph=") + std::to_wstring(ph) + STR(" docked=") + (ship_docked(ship) ? STR("1") : STR("0")) +
                 STR(" back=") + (m.sw_back ? STR("1") : STR("0")));
    }

    // WHAT: MS_SWEEP helper: cache the floating ore actors (BP_FloatingOre_C) and their positions (uu) into m.sw_list. Logs found / kept / frame.
    // WHY: does not need the ship frame (a docked ship may not have one): only the nearest-first sort in SW_PICK needs it.
    void sweep_scan(UObject* ship, int sid, MineSt& m)
    {
        ActFrame f;
        m.sw_frame = act_frame(ship, f);
        const OreScan& sc = ore_scan(true);   // the sweep always takes a fresh scan (start, rescans)
        m.sw_list.clear();
        for (const OreEnt& e : sc.list)
        {
            SwOre o;
            o.a = e.a;
            std::copy(e.p, e.p + 3, o.p);
            m.sw_list.push_back(o);
        }
        m.sw_found = sc.found;
        if (g_debug) log_line(STR("SWEEP scan ship=") + std::to_wstring(sid) + STR(" found=") + std::to_wstring(sc.found) + STR(" ok=") + std::to_wstring(m.sw_list.size()) +
                 STR(" frame=") + (m.sw_frame ? STR("1") : STR("0")));
    }

    // WHAT: MS_SWEEP helper: distance (m) from the ship to the saved return point, and whether the ship is in the saved system. false when unreadable.
    bool sw_abs_dist(UObject* ship, const MineSt& m, double& dist_m, bool& same_sys)
    {
        same_sys = sys_id(ship) == m.sw_sys;
        int64_t c[3];
        double l[3];
        if (!get_chunk(ship, c) || !get_loc(ship, l)) return false;
        double s = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            const double dr = (double)(m.sw_ch[i] - c[i]) * 134217728.0 + (m.sw_loc[i] - l[i]);   // raw
            s += dr * dr;
        }
        dist_m = std::sqrt(s) / g_raw_per_m;
        return true;
    }

    // WHAT: MS_SWEEP helper: back at the ore field: rescan, pick from the top.
    void sw_arrived(UObject* ship, int sid, MineSt& m)
    {
        m.sw_back = false;
        chunk_hold_target_clear(sid);
        sweep_scan(ship, sid, m);
        m.sw_rescans = 0;
        mlog(sid, STR("Collect: back at the ore field"));
        sw_phase(ship, sid, m, SW_PICK);
    }

    // WHAT: MS_SWEEP: undock after a dock trip, return to the ore field, pick floating ore nearest first, dock when cargo is full.
    // WHY: the collect action runs until no ore is left: the ship keeps the tractor on and flies to each piece in turn.
    void st_sweep(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        switch (m.sw_ph)
        {
        case SW_UNDOCK:
        {
            for (const DockJob& j : g_jobs) if (j.sid == sid) return;   // undock job running
            if (g_job_res[sid].empty())   // not started yet (sweep_cmd, trip_end and the docked fallback clear the result): start it as st_undock does
            {
                const bool docked = ship_docked(ship);
                cancel_dock_job(ship, sid);
                g_job_res.erase(sid);
                const std::wstring r = job_start(sid, "undock", -1);
                if (g_debug) log_line(STR("SWEEP undock start ship=") + std::to_wstring(sid) + STR(" docked=") + (docked ? STR("1") : STR("0")) + STR(" r=") + r);
                if (r == STR("waiting")) return;
                g_job_res[sid] = r.empty() ? STR("ok") : r;
            }
            const std::wstring r = g_job_res[sid];
            if (g_debug) log_line(STR("SWEEP undock ship=") + std::to_wstring(sid) + STR(" res=") + r + STR(" docked=") + (ship_docked(ship) ? STR("1") : STR("0")));
            if (!ok_res(r))
            {
                m.sweep = false; m.st = MS_HALT; m.err = true;
                mset(sid, m, STR("ERROR: undock failed: ") + r, STR("ERROR: undock failed: ") + r);
                return;
            }
            // undocked (or was not docked)
            sw_phase(ship, sid, m, m.sw_back ? SW_RETURN : SW_PICK);
            run_cmd(-1, sid, "tractor_on");
            run_cmd(-1, sid, "laser_off");
            if (!m.sw_back && m.sw_list.empty())
            {
                sweep_scan(ship, sid, m);
                mlog(sid, STR("Collect: ") + std::to_wstring(m.sw_list.size()) + STR(" ore found (of ") + std::to_wstring(m.sw_found) + STR(")"));
            }
            return;
        }
        case SW_RETURN:
        {
            double d = 0.0;
            bool same = false;
            if (!m.sw_nofly && sw_abs_dist(ship, m, d, same) && sweep_fly_back(same, d))
            {
                PtTarget t;
                t.sys = m.sw_sys;
                std::copy(m.sw_ch, m.sw_ch + 3, t.ch);
                std::copy(m.sw_loc, m.sw_loc + 3, t.loc);
                const std::wstring r = move_start(sid, "approach_rock", -1, nullptr, &t);
                if (r == STR("waiting")) { sw_phase(ship, sid, m, SW_FLY); mlog(sid, STR("Collect: flying back")); return; }
            }
            chunk_hold_target(sid, m.sw_sys, m.sw_ch);
            const int64_t arr[3] = {(int64_t)m.sw_loc[0], (int64_t)m.sw_loc[1], (int64_t)m.sw_loc[2]};
            std::wstring e;
            if (!warp_send(sid, m.sw_sys, m.sw_ch, arr, false, e))
            {
                m.sweep = false; m.st = MS_HALT; m.err = true;
                mset(sid, m, STR("ERROR: return warp failed: ") + e, STR("ERROR: return warp failed: ") + e);
                return;
            }
            sw_phase(ship, sid, m, SW_WARP);
            mlog(sid, STR("Collect: warping back"));
            return;
        }
        case SW_FLY:
        {
            m.sw_t += dt;
            if (mv_active(sid) && m.sw_t < SWEEP_FLY_TIMEOUT_S) return;
            if (mv_active(sid))
            {
                mv_cancel(sid); zero_all(ship);
                m.sw_nofly = true;
                sw_phase(ship, sid, m, SW_RETURN);
                return;
            }
            sw_arrived(ship, sid, m);
            return;
        }
        case SW_WARP:
        {
            m.sw_t += dt;
            double d = 0.0;
            bool same = false;
            if (sw_abs_dist(ship, m, d, same) && same && d <= 100.0) { sw_arrived(ship, sid, m); return; }
            if (m.sw_t >= SWEEP_WARP_WAIT_S)
            {
                m.sweep = false; m.st = MS_HALT; m.err = true;
                mset(sid, m, STR("ERROR: return warp did not arrive"), STR("ERROR: return warp did not arrive"));
            }
            return;
        }
        case SW_PICK:
        {
            m.sw_t += dt;
            if (chunk_hold_settling(sid)) return;
            std::wstring w;
            UObject* item = item_c(c.resource, w);
            const bool full = (alive(item) && cargo_full(ship, item)) || m.cargo_pct >= 100;
            if (full)
            {
                if (unl_guard_fire(ship, sid, m, item))
                {
                    m.sweep = false; m.st = MS_HALT; m.err = true;
                    mset(sid, m, STR("ERROR: cargo still full after unload (does the ore have an unload dock?)"), STR("ERROR: cargo still full after unload (does the ore have an unload dock?)"));
                    return;
                }
                int sys = 0;
                int64_t ch[3];
                double l[3];
                if (ship_pos(ship, sys, ch) && get_loc(ship, l))
                {
                    m.sw_sys = sys;
                    std::copy(ch, ch + 3, m.sw_ch);
                    std::copy(l, l + 3, m.sw_loc);
                    m.sw_back = true;
                }
                mine_stop_work(ship, sid);
                trip_begin(ship, sid, m, STR("cargo full"), "dock_mining");
                mlog(sid, STR("Collect: cargo full, unloading"));
                return;
            }
            m.unl_chk = 0;   // room in cargo: the unload worked, the retry counter restarts
            ActFrame f;
            if (!act_frame(ship, f))
            {
                // the distance sort needs the ship position: a docked ship is undocked first (guarded), else stop with an error
                if (ship_docked(ship) && m.sw_undock_n < 2)
                {
                    ++m.sw_undock_n;
                    g_job_res.erase(sid);
                    mlog(sid, STR("Collect: position unreadable while docked, undocking"));
                    sw_phase(ship, sid, m, SW_UNDOCK);
                    return;
                }
                m.sweep = false; m.st = MS_HALT; m.err = true;
                mlog(sid, STR("ERROR: cannot read ship position"));
                mset(sid, m, STR("ERROR: cannot read ship position"), STR("ERROR: cannot read ship position"));
                return;
            }
            std::vector<double> d(m.sw_list.size(), 0.0);
            std::vector<char> done(m.sw_list.size(), 0);
            for (size_t i = 0; i < m.sw_list.size(); ++i)
            {
                const SwOre& o = m.sw_list[i];
                const double dv[3] = {o.p[0] - f.loc[0], o.p[1] - f.loc[1], o.p[2] - f.loc[2]};
                d[i] = norm3(dv);
                done[i] = o.done;   // alive_seh only on the chosen entry below (per-entry checks on 9000 actors every pick = lag)
            }
            int i = sweep_nearest(d, done);
            while (i >= 0 && !alive_seh(m.sw_list[(size_t)i].a))   // picked entry is gone: mark it, take the next nearest
            {
                m.sw_list[(size_t)i].done = true; done[(size_t)i] = 1;
                i = sweep_nearest(d, done);
            }
            if (i < 0)
            {
                if (m.sw_rescans < SWEEP_RESCANS)
                {
                    if (m.sw_t < SWEEP_RESCAN_S) return;   // wait before the rescan
                    ++m.sw_rescans; m.sw_t = 0.0;
                    sweep_scan(ship, sid, m);
                    return;
                }
                mine_stop_work(ship, sid);
                ore_scan(true);   // sweep finished: refresh the shared count (Collect button)
                m.sweep = false; m.st = MS_HALT;
                if (m.sw_found == 0)
                {
                    mset(sid, m, STR("Collect done"), STR("Collect done"));
                    return;
                }
                m.err = true;
                mset(sid, m, STR("ERROR: ") + std::to_wstring(m.sw_found) + STR(" ore still floating, none reachable"), STR("ERROR: ore still floating, none reachable"));
                return;
            }
            m.sw_ore = m.sw_list[(size_t)i].a;
            const std::wstring r = move_start(sid, "approach_rock", -1, m.sw_ore, nullptr);
            if (r == STR("waiting"))
            {
                MoveJob& mv = g_mvs[sid];   // fly to the cached scan point; aim_calc_ore re-reads the ore when near it
                std::copy(m.sw_list[(size_t)i].p, m.sw_list[(size_t)i].p + 3, mv.ore_loc);
                mv.ore_fresh = false; mv.ore_t = 0.0;
                sw_phase(ship, sid, m, SW_APPROACH);
            }
            else m.sw_list[(size_t)i].done = true;
            return;
        }
        case SW_APPROACH:
        {
            if (mv_active(sid)) return;   // any result: a rock lost here means the tractor already took it
            sw_phase(ship, sid, m, SW_PULL);
            run_cmd(-1, sid, "tractor_on");
            run_cmd(-1, sid, "laser_off");
            return;
        }
        case SW_PULL:
        {
            m.sw_t += dt;
            const bool gone = !alive_seh(m.sw_ore);
            std::wstring w;
            UObject* item = item_c(c.resource, w);
            const bool full = (alive(item) && cargo_full(ship, item)) || m.cargo_pct >= 100;
            if (gone || full || m.sw_t >= SWEEP_PICK_S)
            {
                for (SwOre& o : m.sw_list) if (o.a == m.sw_ore) o.done = true;
                if (gone) mlog(sid, STR("Collect: ore picked up"));
                m.sw_ore = nullptr;
                sw_phase(ship, sid, m, SW_PICK);
            }
            return;
        }
        default:
            return;
        }
    }

    // WHAT: holding slot helpers (D4). A chunk is 2048 m = 134217728 raw. abs = chunk * 2048 + loc / g_raw_per_m (m, same frame for all docks).
    // WHY: slot points of different docks must be compared in one frame (net_line_dir_fix).
    constexpr double DOCK_CHUNK_M = 2048.0;
    constexpr int DOCK_LINE_SLOTS = 6;        // slots checked when a dock line is set (net_line_dir_fix)
    constexpr double DOCK_SLOT_ARRIVE_M = 100.0;   // a warp to a slot is arrived within this distance
    constexpr double DOCK_SLOT_WARP_WAIT_S = 15.0; // a slot warp not arrived after this long is tried again
    constexpr double DOCK_SLOT_RETRY_S = 5.0;      // a failed slot flight/warp start waits this long before the next try

    void dock_abs_m(const int64_t ch[3], const double loc[3], double out[3])
    {
        for (int i = 0; i < 3; ++i) out[i] = static_cast<double>(ch[i]) * DOCK_CHUNK_M + loc[i] / g_raw_per_m;
    }

    // WHAT: chunk + raw loc moved by off_m metres (carry over the chunk border, net_offset_pos in mb_logic.hpp).
    void dock_add_m(const int64_t ch0[3], const double loc0[3], const double off_m[3], int64_t ch[3], double loc[3])
    {
        net_offset_pos(ch0, loc0, off_m, g_raw_per_m, ch, loc);
    }

    // WHAT: flight or warp to the slot is over: stop, hold (hold_ph 2), phase holding, Control lines.
    void slot_arrive(UObject* ship, int sid, MineSt& m, NetShip& s, int d)
    {
        zero_all(ship);
        if (m.hold_warp) chunk_hold_target_clear(sid);
        m.hold_warp = false; m.hold_t = 0.0;
        m.hold_ph = 2; s.phase = 2;
        const std::wstring slot_no = std::to_wstring(s.slot);
        mlog(sid, STR("Control: ") + widen(net_ship_label(sid)) + STR(" holding at slot ") + slot_no);
        log_line(STR("CONTROL ship=") + std::to_wstring(sid) + STR(" holding at slot ") + slot_no + STR(" dock=") + std::to_wstring(d));
    }

    // WHAT: waiting ship sid goes to its holding slot near the dock and holds there. Returns false only when the dock position is unknown.
    // WHAT: the dock's line is set on its first slot: direction from the dock toward this ship (+X when the ship is in another system), turned by net_line_dir_fix.
    // WHAT: close in the same system (sweep_fly_back) = point flight (approach_rock with PtTarget); else warp_send. Holding = zero_all, hold_ph 2.
    // WHY: waiting ships keep out of the dock; the grant (st_trip) takes them from the slot. A docked ship is not a waiter (no slot).
    bool slot_flight(UObject* ship, int sid, MineSt& m, double dt)
    {
        const auto gi = g_net_ships.find(sid);
        if (gi == g_net_ships.end() || gi->second.waits_dock == 0) return true;
        if (ship_docked(ship)) return true;
        NetShip& s = gi->second;
        const int d = s.waits_dock;
        const auto pi = g_net_dockpos.find(d);
        const auto di = g_net_docks.find(d);
        if (pi == g_net_dockpos.end() || !pi->second.known || di == g_net_docks.end()) return false;
        DockPos& dp = pi->second;
        NetDock& nd = di->second;
        if (s.slot == 0) s.slot = net_slot_assign(nd, sid);
        const int sys = sys_id(ship);
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        const bool readable = sys > 0 && get_chunk(ship, ch) && get_loc(ship, loc);
        const bool same = readable && sys == dp.sys;
        double dk[3];
        dock_abs_m(dp.ch, dp.loc, dk);
        if (!dp.dir_known)
        {
            double dir[3] = {1.0, 0.0, 0.0};   // other system: +X
            if (same)
            {
                double a[3];
                dock_abs_m(ch, loc, a);
                const double v[3] = {a[0] - dk[0], a[1] - dk[1], a[2] - dk[2]};
                const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (len > 0.0) { dir[0] = v[0] / len; dir[1] = v[1] / len; dir[2] = v[2] / len; }
            }
            std::vector<std::array<double, 3>> others;   // slot points of the other docks in this system
            for (const auto& kv : g_net_dockpos)
            {
                const auto od = g_net_docks.find(kv.first);
                if (kv.first == d || !kv.second.known || !kv.second.dir_known || kv.second.sys != dp.sys || od == g_net_docks.end()) continue;
                double ob[3];
                dock_abs_m(kv.second.ch, kv.second.loc, ob);
                for (int k = 1; k <= od->second.line_slots; ++k)
                {
                    double o[3];
                    net_slot_pos(kv.second.dir, k, o);
                    others.push_back({ob[0] + o[0], ob[1] + o[1], ob[2] + o[2]});
                }
            }
            if (!net_line_dir_fix(dk, dir, DOCK_LINE_SLOTS, others))
                log_line(STR("CONTROL dock=") + std::to_wstring(d) + STR(" line not clear of the other docks, using the first direction"));
            for (int i = 0; i < 3; ++i) dp.dir[i] = dir[i];
            dp.dir_known = true;
        }
        const std::wstring slot_no = std::to_wstring(s.slot);
        const std::wstring dname = widen(net_dock_label(d));
        const std::wstring who = widen(net_ship_label(sid));
        if (m.hold_ph == 2) return true;   // holding
        if (m.hold_ph == 1)
        {
            m.hold_t += dt;
            if (m.hold_warp)
            {
                if (same)
                {
                    double a[3], t[3];
                    dock_abs_m(ch, loc, a);
                    double off[3], tch_loc[3];
                    int64_t tch[3];
                    net_slot_pos(dp.dir, s.slot, off);
                    dock_add_m(dp.ch, dp.loc, off, tch, tch_loc);
                    dock_abs_m(tch, tch_loc, t);
                    const double dist = std::sqrt((a[0] - t[0]) * (a[0] - t[0]) + (a[1] - t[1]) * (a[1] - t[1]) + (a[2] - t[2]) * (a[2] - t[2]));
                    if (dist <= DOCK_SLOT_ARRIVE_M) { slot_arrive(ship, sid, m, s, d); return true; }
                }
                if (m.hold_t < DOCK_SLOT_WARP_WAIT_S) return true;
                m.hold_ph = 0; m.hold_t = DOCK_SLOT_RETRY_S / 2.0;   // not arrived in time: warp again after the retry wait
                s.phase = 0;
                return true;
            }
            if (mv_active(sid) && m.hold_t < SWEEP_FLY_TIMEOUT_S) return true;
            if (mv_active(sid)) mv_cancel(sid);   // flight timed out: hold where it is
            slot_arrive(ship, sid, m, s, d);
            return true;
        }
        // hold_ph 0: start the flight or warp (retry wait after a failure)
        if (m.hold_t > 0.0)
        {
            m.hold_t += dt;
            if (m.hold_t < DOCK_SLOT_RETRY_S) return true;
            m.hold_t = 0.0;
        }
        if (!readable) return true;
        {
            double off[3], tch_loc[3];
            int64_t tch[3];
            net_slot_pos(dp.dir, s.slot, off);
            dock_add_m(dp.ch, dp.loc, off, tch, tch_loc);
            double a[3], t[3];
            dock_abs_m(ch, loc, a);
            dock_abs_m(tch, tch_loc, t);
            const double dist = std::sqrt((a[0] - t[0]) * (a[0] - t[0]) + (a[1] - t[1]) * (a[1] - t[1]) + (a[2] - t[2]) * (a[2] - t[2]));
            if (sweep_fly_back(same, dist))
            {
                PtTarget pt;
                pt.sys = dp.sys;
                std::copy(tch, tch + 3, pt.ch);
                std::copy(tch_loc, tch_loc + 3, pt.loc);
                if (move_start(sid, "approach_rock", -1, nullptr, &pt) == STR("waiting"))
                {
                    m.hold_ph = 1; m.hold_warp = false; m.hold_t = 0.0; s.phase = 1;
                    mlog(sid, STR("Control: ") + who + STR(" flying to holding slot ") + slot_no + STR(" near ") + dname);
                    log_line(STR("CONTROL ship=") + std::to_wstring(sid) + STR(" flying to holding slot ") + slot_no + STR(" dock=") + std::to_wstring(d));
                    return true;
                }
            }
            chunk_hold_target(sid, dp.sys, tch);
            const int64_t arr[3] = {static_cast<int64_t>(tch_loc[0]), static_cast<int64_t>(tch_loc[1]), static_cast<int64_t>(tch_loc[2])};
            std::wstring e;
            if (warp_send(sid, dp.sys, tch, arr, false, e))
            {
                m.hold_ph = 1; m.hold_warp = true; m.hold_t = 0.0; s.phase = 1;
                mlog(sid, STR("Control: ") + who + STR(" flying to holding slot ") + slot_no + STR(" near ") + dname);
                log_line(STR("CONTROL ship=") + std::to_wstring(sid) + STR(" warping to holding slot ") + slot_no + STR(" dock=") + std::to_wstring(d));
            }
            else
            {
                m.hold_t = dt > 0.0 ? dt : 1.0;   // retry after DOCK_SLOT_RETRY_S
                log_line(STR("CONTROL ship=") + std::to_wstring(sid) + STR(" holding slot warp failed: ") + e);
            }
            return true;
        }
    }

    constexpr double WARP_TRIP_STANDOFF_M = 300.0;   // a far dock trip warps to this distance from the dock

    // WHAT: far dock trip (dock job stage 2, once per job): a ship more than warp_trip_km from its dock warps to 300 m out from the dock when it has warp fuel. Else it keeps flying. Returns true when the teleport was sent.
    // WHY: a slow flight over a long distance hit the dock timeout (live case: 10 km). The warp shortens the trip; the timeout clock resets on progress (jobs_tick).
    bool trip_warp_try(UObject* ship, int sid, int dock)
    {
        const std::wstring who = STR("TRIPWARP ship=") + std::to_wstring(sid) + STR(" skip: ");
        const auto pi = g_net_dockpos.find(dock);
        if (pi == g_net_dockpos.end() || !pi->second.known) { log_line(who + STR("dock position unknown")); return false; }
        DockPos& dp = pi->second;
        const int sys = sys_id(ship);
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        if (!(sys > 0 && get_chunk(ship, ch) && get_loc(ship, loc))) { log_line(who + STR("ship position unreadable")); return false; }
        if (sys != dp.sys) { log_line(who + STR("other system")); return false; }
        if (ship_docked(ship)) { log_line(who + STR("ship docked")); return false; }
        double a[3], dk[3];
        dock_abs_m(ch, loc, a);
        dock_abs_m(dp.ch, dp.loc, dk);
        const double v[3] = {a[0] - dk[0], a[1] - dk[1], a[2] - dk[2]};
        const double dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        const Config c = eff_cfg(sid);
        if (!trip_warp_wanted(dist, c.warp_trip_km)) return false;
        int32_t drive = 0;
        if (!call_int(ship, STR("GetAvailableJumpFuel"), drive) || !warp_fuel_ok(true, dist / DOCK_CHUNK_M, 0, drive))
        {
            log_line(who + STR("no warp fuel, flying drive=") + std::to_wstring(drive) +
                     STR(" need=") + std::to_wstring(warp_fuel_need(dist / DOCK_CHUNK_M)));
            return false;
        }
        double off[3], tch_loc[3];
        int64_t tch[3];
        for (int i = 0; i < 3; ++i) off[i] = v[i] / dist * WARP_TRIP_STANDOFF_M;
        dock_add_m(dp.ch, dp.loc, off, tch, tch_loc);
        chunk_hold_target(sid, dp.sys, tch);
        const int64_t arr[3] = {static_cast<int64_t>(tch_loc[0]), static_cast<int64_t>(tch_loc[1]), static_cast<int64_t>(tch_loc[2])};
        std::wstring e;
        if (!warp_send(sid, dp.sys, tch, arr, false, e))
        {
            chunk_hold_target_clear(sid);
            log_line(who + STR("warp failed: ") + e);
            return false;
        }
        log_line(STR("TRIPWARP ship=") + std::to_wstring(sid) + STR(" dock=") + std::to_wstring(dock) +
                 STR(" dist_m=") + std::to_wstring(dist) + STR(" km_limit=") + std::to_wstring(c.warp_trip_km));
        mlog(sid, STR("Far trip: warping to dock"));
        return true;
    }

    // WHAT: ship to dock distance in m (chunk + local position vs the dock's cached position). -1 when the dock position or the ship position is unknown, or another system.
    // WHY: the far trip warp settle (jobs_tick stage 4) waits until the ship reads near the dock.
    double trip_dock_dist_m(UObject* ship, int dock)
    {
        const auto pi = g_net_dockpos.find(dock);
        if (pi == g_net_dockpos.end() || !pi->second.known) return -1.0;
        const DockPos& dp = pi->second;
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        if (!(sys_id(ship) == dp.sys && get_chunk(ship, ch) && get_loc(ship, loc))) return -1.0;
        double a[3], dk[3];
        dock_abs_m(ch, loc, a);
        dock_abs_m(dp.ch, dp.loc, dk);
        return std::sqrt((a[0] - dk[0]) * (a[0] - dk[0]) + (a[1] - dk[1]) * (a[1] - dk[1]) + (a[2] - dk[2]) * (a[2] - dk[2]));
    }

    // WHAT: MS_TRIP: dock trip (job result).
    void st_trip(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        // dock busy: no job runs; the ship waits in the dock queue (mb_net.hpp). The grant starts the job; a 60 s poll is the safety net.
        if (m.dwait)
        {
            if (m.leave && !m.btn) { net_leave_slot(sid); m.dwait = false; m.st = MS_HALT; mset(sid, m, STR("stopped while waiting for dock")); return; }
            m.dwait_t += dt;
            const auto gi = g_net_ships.find(sid);
            const bool granted = gi != g_net_ships.end() && gi->second.granted;
            if (!granted && m.dwait_t < 60.0)
            {
                // holding slot: fly / warp there and hold (every tick, cheap); the wait text once a second: dock, holder, queue place, elapsed time
                const bool slot_ok = slot_flight(ship, sid, m, dt);
                if (m.flee && m.hold_ph == 2)   // a fleeing ship that reached its holding spot: the flee ends here (no dock slot taken)
                {
                    net_leave_slot(sid);
                    m.dwait = false; m.flee = false; m.fin = false; m.err = true; g_flee_pick.erase(sid);
                    m.st = MS_HALT;
                    mset(sid, m, STR("ERROR: hull damaged, fled, holding near dock, stopped"), STR("ERROR: ship was attacked, fled, holding near dock, stopped"));
                    return;
                }
                if (m.dwait_shown < 0.0 || m.dwait_t - m.dwait_shown >= 1.0)
                {
                    m.dwait_shown = m.dwait_t;
                    dock_wait_text(sid, m);
                    if (!slot_ok) m.result += STR(" (no holding position yet)");
                }
                return;
            }
            m.dwait_t = 0.0; m.dwait_shown = -1.0;
            // granted: the ship leaves its holding spot (flight cancelled, stopped) and the reserved dock job starts
            std::wstring cleared_dock;
            if (granted)
            {
                for (const auto& kv : g_net_docks) if (kv.second.resv == sid) cleared_dock = widen(net_dock_label(kv.first));
                mv_cancel(sid);
                zero_all(ship);
                slot_clear(sid);
                const auto gs = g_net_ships.find(sid);
                if (gs != g_net_ships.end()) gs->second.phase = 3;   // cleared: the job is starting
            }
            g_job_res.erase(sid);
            const std::wstring r = job_start(sid, m.trip, -1);
            if (r == STR("waiting dock")) { dock_wait_text(sid, m); return; }
            m.dwait = false;
            m.wait_gate = LogGate(); m.q_gate = LogGate();
            if (r == STR("nothing to unload")) { g_flee_pick.erase(sid); m.st = MS_HALT; mset(sid, m, STR("Nothing to unload")); return; }
            if (net_real_error(r)) { g_flee_pick.erase(sid); m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: ") + r.substr(7), STR("ERROR: ") + r.substr(7)); return; }
            if (r != STR("waiting")) { g_flee_pick.erase(sid); m.st = MS_HALT; mset(sid, m, STR("dock trip failed: ") + r); return; }
            g_flee_pick.erase(sid);
            if (cleared_dock.empty()) for (const auto& kv : g_net_docks) if (kv.second.resv == sid) cleared_dock = widen(net_dock_label(kv.first));
            const std::wstring what = is_refuel(m) ? STR("Refuel") : STR("Unload");
            mset(sid, m, STR("Cleared for ") + what + STR(" ") + cleared_dock, STR("Control: ") + widen(net_ship_label(sid)) + STR(" cleared for ") + what + STR(" ") + cleared_dock);
            return;
        }
        for (const DockJob& j : g_jobs) if (j.sid == sid) return;
        const std::wstring r = g_job_res[sid];
        // the dock job found the dock busy: leave the ship (autopilot off, stop) and wait with the same retry
        if (r == STR("waiting dock"))
        {
            g_ob.erase(sid); g_pf.erase(sid);
            std::wstring e;
            set_autopilot(sid, false, e);
            zero_all(ship);
            m.dwait = true; m.dwait_t = 60.0; m.dwait_shown = -1.0;   // 60 s = the next tick asks the dock network again (joins the queue)
            dock_wait_text(sid, m);
            return;
        }
        if (ok_res(r)) { m.st = MS_HOLD; m.t = 0.0; m.hold_idle = 0.0; m.hold_last = -1; m.chk_t = MINE_HOLD_CHECK_S; m.hold_seen = false; m.left_last.clear(); mset(sid, m, STR("docked, waiting for orders to finish"), is_refuel(m) ? STR("Docked, refueling") : STR("Docked, unloading")); }
        else if (r.find(STR("seems stuck")) != std::wstring::npos)
        {
            m.err = true; m.st = MS_HALT;
            mset(sid, m, STR("ERROR: Cannot move: seems stuck (docked?)"), STR("Cannot move: ") + widen(net_ship_label(sid)) + STR(" seems stuck (docked?)"));
        }
        else { m.st = MS_HALT; mset(sid, m, STR("dock trip failed: ") + r); }
        return;
    }
    // WHAT: true when ore i is unloaded on this ship's current dock trip: in g_unl_ores[sid]. No entry for the ship = every ore.
    // WHY: the other ores stay in cargo for the next trip, so MS_HOLD must not wait for them.
    bool unl_ore(int sid, int i, const Config& c)
    {
        auto it = g_unl_ores.find(sid);
        if (it == g_unl_ores.end()) return true;
        return std::find(it->second.begin(), it->second.end(), i) != it->second.end();
    }

    // WHAT: MS_HOLD: at dock, refuel/unload progress, dock time limit, finish.
    void st_hold(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        m.chk_t += dt;
        if (m.chk_t < MINE_HOLD_CHECK_S) return;
        m.chk_t = 0.0;
        bool done = true;
        std::wstring left;
        if (is_refuel(m))
        {
            int64_t sum = 0;
            for (int i = 0; i < N_FUEL; ++i)
            {
                if (!c.fuel[i].on) continue;
                std::wstring w;
                UObject* it = item_c(FUEL_ITEM[i], w);
                const int64_t have = alive(it) ? inv_total(ship, it) : -1;
                if (have > 0) sum += have;
                if (have < c.fuel[i].amount) { done = false; left += widen(FUEL_KEY[i]) + STR(" ") + i64w(have) + STR("/") + std::to_wstring(c.fuel[i].amount) + STR(" "); }
            }
            // stall: fuel cargo not growing for idle_seconds after wait_seconds -> HALT (idle_seconds 0 = off)
            if (sum > m.hold_last) m.hold_idle = 0.0; else m.hold_idle += MINE_HOLD_CHECK_S;
            m.hold_last = sum;
            if (refuel_stalled(done, c.idle_seconds, m.t, c.wait_seconds, m.hold_idle))
            { m.st = MS_HALT; mset(sid, m, STR("refuel stalled, not delivered: ") + left); return; }
        }
        else
        {
            int64_t sum = 0;   // ore of this trip still in cargo
            for (int i = 0; i < N_ORE; ++i)
            {
                if (!unl_ore(sid, i, c)) continue;
                std::wstring w;
                UObject* it = item_c(ORE_ITEM[i], w);
                const int64_t have = alive(it) ? inv_total(ship, it) : -1;
                if (have > 0) { done = false; sum += have; left += widen(ORE_KEY[i]) + STR(" ") + i64w(have) + STR(" "); }
            }
            // stall: the ore cargo not shrinking (idle_seconds after wait_seconds, 0 = off) -> dock full. A shrink proves the dock takes ore.
            const bool shrank = m.hold_last >= 0 && sum < m.hold_last;
            if (shrank) { m.hold_idle = 0.0; net_full_clear(dock_of(ship)); }
            else if (m.hold_last >= 0) m.hold_idle += MINE_HOLD_CHECK_S;
            m.hold_last = sum;
            if (unload_stalled(done, c.idle_seconds, m.t, c.wait_seconds, m.hold_idle)) { dock_full_leave(ship, sid, m); return; }
        }
        // dock time limit (not an error): items still left after dock_hold_limit s -> undock, wait MINE_DOCK_RETRY_WAIT_S, dock again (same trip).
        // After MINE_DOCK_RETRIES rounds the next limit hit undocks and goes idle. 0 = no limit.
        if (!done && c.dock_hold_limit > 0 && m.t >= c.dock_hold_limit)
        {
            const std::wstring N = std::to_wstring(c.dock_hold_limit);
            cancel_dock_job(ship, sid);
            g_job_res.erase(sid);
            if (dock_limit_giveup(m.dl_n))
            {
                const std::wstring ur = job_start(sid, "undock", -1);
                const std::wstring idle = STR("Dock time limit reached ") + std::to_wstring(MINE_DOCK_RETRIES) + STR(" times, undocked, idle");
                m.dl_n = 0; m.fin = false; m.st = MS_HALT;
                mset(sid, m, idle, idle);
                if (undock_failed(ur)) mlog(sid, STR("undock after dock time limit failed: ") + ur);
                return;
            }
            ++m.dl_n;
            const std::wstring ur = job_start(sid, "undock", -1);
            if (undock_failed(ur)) { m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: undock failed: ") + ur, STR("ERROR: undock failed: ") + ur); return; }
            const std::wstring n = std::to_wstring(m.dl_n) + STR("/") + std::to_wstring(MINE_DOCK_RETRIES);
            m.st = MS_DLWAIT; m.dl_wait_t = 0.0; m.left_last.clear();
            mset(sid, m, STR("Dock time limit, waiting ") + std::to_wstring(static_cast<int>(MINE_DOCK_RETRY_WAIT_S)) + STR(" s (") + n + STR(")"),
                 STR("Dock time limit (") + N + STR(" s), undocking to free the dock, retry ") + n);
            mlog(sid, STR("Waiting ") + std::to_wstring(static_cast<int>(MINE_DOCK_RETRY_WAIT_S)) + STR(" s before docking again"));
            return;
        }
        if (!done && left != m.left_last) { m.left_last = left; mlog(sid, std::wstring(is_refuel(m) ? STR("Refueling") : STR("Unloading")) + STR(", left: ") + left); }
        if (done && !m.hold_seen) { m.hold_seen = true; mlog(sid, std::wstring(is_refuel(m) ? STR("Refuel") : STR("Unload")) + STR(" done, waiting ") + std::to_wstring(c.wait_seconds) + STR(" s")); }
        if (done && m.t >= c.wait_seconds) { m.dl_n = 0; trip_end(ship, sid, m, c); return; }
        mset(sid, m, STR("at dock, waiting: ") + (done ? std::wstring(STR("wait time")) : left));
        return;
    }
    // WHAT: MS_WARP: belt warp, poll for arrival sector, timeout.
    void st_warp(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        // belt warp: poll once a second for the arrival sector; MINE_WARP_TIMEOUT_S without arrival = HALT
        m.warp_t += dt; m.chk_t += dt;
        if (g_wr_wait.count(sid)) m.warp_t = 0.0;   // docked ship still undocking before the warp (warp_wait_tick): no timeout yet
        if (m.chk_t < 1.0) return;
        m.chk_t = 0.0;
        if (cur_sector_id(ship) == m.warp_to)
        {
            m.warp_to = 0;
            ActFrame f;
            if (act_frame(ship, f)) std::copy(f.loc, f.loc + 3, m.anchor);
            enter_seek(m);
            chunk_hold_update(sid, ship);   // chunk hold at once: the rock list (fallback pass) sees the belt rocks
            mset(sid, m, STR("arrived, seeking rock"), STR("Arrived, seeking rock"));
            return;
        }
        if (m.warp_t >= MINE_WARP_TIMEOUT_S) { m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: warp to sector ") + std::to_wstring(m.warp_to) + STR(" timed out"), STR("ERROR: warp timed out")); }
        return;
    }
    // WHAT: MS_DLWAIT: dock time limit wait, then dock trip again.
    void st_dlwait(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        // dock time limit retry: the undock job runs first, then MINE_DOCK_RETRY_WAIT_S, then the same dock trip again
        for (const DockJob& j : g_jobs) if (j.sid == sid) return;
        const std::wstring ur = g_job_res[sid];
        if (!ur.empty() && !ok_res(ur)) { m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: undock failed: ") + ur, STR("ERROR: undock failed: ") + ur); return; }
        m.dl_wait_t += dt;
        if (m.dl_wait_t < MINE_DOCK_RETRY_WAIT_S) return;
        const std::string kind = m.trip;
        mlog(sid, STR("Docking again"));
        trip_begin(ship, sid, m, STR("retry ") + std::to_wstring(m.dl_n) + STR("/") + std::to_wstring(MINE_DOCK_RETRIES), kind);
        return;
    }
    // WHAT: one tick of the mining state machine for bench ship sid: chunk hold bookkeeping, step_pre, then the state function.
    void mine_step(int sid, MineSt& m, double dt)
    {
        if (!g_compat.ok(F_MINING) && m.run) { m.st = MS_HALT; m.run = false; m.result = STR("COMPAT: mining disabled"); return; }
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) { chunk_hold_release(sid); net_leave_slot(sid); m.st = MS_OFF; mset(sid, m, STR("ship not found")); return; }
        const Config c = eff_cfg(sid);
        const bool on = m.run && g_cfg.enabled == 1 && c.enabled == 1;
        m.t += dt;
        // chunk hold: SEEK/MINE keep the cube of the ship's chunk (update also drops it when the player flies the ship). OFF, CHECK and UNDOCK
        // keep what is there (CHECK sets it for an undocked ship with nothing in range); the other states release it. Forced rock passes: chunk_hold_tick.
        const bool on2 = on || m.sweep;   // sweep runs with the Start flag off: it has its own on switch (sweep_cmd / mine_halt)
        if (m.st == MS_SWEEP || ((m.st == MS_SEEK || m.st == MS_MINE || m.st == MS_COLLECT) && on2)) chunk_hold_update(sid, ship);
        else if (m.st != MS_OFF && m.st != MS_CHECK && m.st != MS_UNDOCK) chunk_hold_release(sid);
        chunk_hold_tick(sid);
        if (step_pre(ship, sid, m, dt, on2)) return;
        switch (m.st)
        {
        case MS_OFF: st_off(ship, sid, m, dt, c); return;
        case MS_CHECK: st_check(ship, sid, m, dt, c); return;
        case MS_UNDOCK: st_undock(ship, sid, m, dt, c); return;
        case MS_SEEK: st_seek(ship, sid, m, dt, c); return;
        case MS_MINE: st_mine(ship, sid, m, dt, c); return;
        case MS_COLLECT: st_collect(ship, sid, m, dt, c); return;
        case MS_SWEEP: st_sweep(ship, sid, m, dt, c); return;
        case MS_TRIP: st_trip(ship, sid, m, dt, c); return;
        case MS_HOLD: st_hold(ship, sid, m, dt, c); return;
        case MS_WARP: st_warp(ship, sid, m, dt, c); return;
        case MS_DLWAIT: st_dlwait(ship, sid, m, dt, c); return;
        default:
            return;   // MS_HALT: wait for toggle off/on
        }
    }

    // WHAT: simple status for the Lua Loop tab. UNDOCK/SEEK/MINE -> mining, TRIP -> traveling, HOLD -> refueling or unloading, else idle.
    const char* mine_simple(const MineSt& m)
    {
        if (m.st == MS_HALT && m.err) return "error";
        if (m.st == MS_TRIP && m.dwait) return "waiting";
        switch (m.st)
        {
        case MS_UNDOCK: case MS_SEEK: case MS_MINE: case MS_COLLECT: return "mining";
        case MS_SWEEP: return "collecting";
        case MS_TRIP: case MS_WARP: return "traveling";
        case MS_DLWAIT: return "waiting";
        case MS_HOLD: return is_refuel(m) ? "refueling" : "unloading";
        default: return "idle";
        }
    }

    void mine_status_write()
    {
        std::string s;
        for (int sid : g_bench_ids)
        {
            const MineSt& m = g_mine[sid];
            s += std::to_string(sid) + "|" + u8(g_bench_names[sid]) + "|" + mine_simple(m) + "|" + u8(std::wstring(mine_state_name(m.st)) + STR(": ") + m.result) + "\n";
        }
        static std::string last = "\x01";
        if (s == last) return;
        last = s;
        try { write_atomic(get_dll_dir() / "MiningStatus.txt", s); } catch (...) {}
    }

    // WHAT: next state label for MiningStatusX.txt. '|' is the field separator, so "Dock (unload/refuel)" uses '/'.
    const char* mine_next(const MineSt& m)
    {
        switch (m.st)
        {
        case MS_CHECK: return "Undock";
        case MS_UNDOCK: return "Seek rock";
        case MS_SEEK: return "Mine";
        case MS_MINE: return "Dock (unload/refuel)";
        case MS_TRIP: return "Wait at dock";
        case MS_HOLD: return "Check/Stop";
        case MS_WARP: return "Seek rock";
        case MS_COLLECT: return "Seek rock";
        case MS_SWEEP: return "Collect ore";
        case MS_DLWAIT: return "Dock again";
        default: return "-";
        }
    }

    // WHAT: true when name contains sub, ASCII case-insensitive. WHY: property names of the cargo probe and capacity lookup.
    bool name_has(const std::wstring& name, const TCHAR* sub)
    {
        std::wstring l = name;
        for (wchar_t& ch : l) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + 32);
        return l.find(sub) != std::wstring::npos;
    }

    // WHAT: cargo fill % (0..100) of a ship, -1 when unknown. Walks the real inventory slots: Inventory.ItemSlots, one ItemStack {Item, Amount}
    //       per slot, slot count = ship InventorySize. A slot is occupied when Amount > 0. Fuel slots (Item is one of FUEL_ITEM) are skipped
    //       entirely: they are not cargo and they leave the denominator.
    //       pct = non-fuel occupied slots / (InventorySize - fuel occupied slots) * 100, clamped 0..100. 0 when no non-fuel slot is occupied.
    //       fuel_s / other_s = occupied fuel / non-fuel slots (also for the debug line). -1 when a slot item is unreadable.
    // WHY: the old slot-count guess (LastOccupiedSlot) counted a fuel stack as one slot and broke when fuel spanned several or scattered slots.
    int cargo_pct(UObject* ship, int& slots, int& fuel_s, int& other_s)
    {
        slots = fuel_s = other_s = 0;
        UObject* inv = get_obj_prop(ship, STR("Inventory"));
        int32_t isz = 0;
        if (!alive(inv) || !get_int_prop(ship, STR("InventorySize"), isz) || isz <= 0) return -1;
        FProperty* tp = inv->GetPropertyByNameInChain(STR("ItemSlots"));
        auto* ap = tp ? CastField<FArrayProperty>(tp) : nullptr;
        auto* arr = ap ? inv->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("ItemSlots")) : nullptr;
        if (!arr) return -1;
        FProperty* inner = ap->GetInner();
        auto* isp = inner ? CastField<FStructProperty>(inner) : nullptr;
        if (!isp || !isp->GetStruct()) return -1;
        FProperty* fi = isp->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add));
        FProperty* fa = isp->GetStruct()->FindProperty(FName(STR("Amount"), FNAME_Add));
        const int32_t stride = inner->GetElementSize();
        if (!fi || !fa || stride <= 0) return -1;
        UObject* fuel[N_FUEL] = {};
        for (int i = 0; i < N_FUEL; ++i) { std::wstring w; fuel[i] = item_c(FUEL_ITEM[i], w); }
        slots = isz;
        for (int32_t i = 0; i < arr->Num() && i < isz; ++i)
        {
            const uint8_t* e = &(*arr)[0] + static_cast<size_t>(i) * static_cast<size_t>(stride);
            if (*reinterpret_cast<const int32_t*>(e + fa->GetOffset_Internal()) <= 0) continue;
            UObject* it = *reinterpret_cast<UObject* const*>(e + fi->GetOffset_Internal());
            if (!alive(it)) return -1;
            bool is_fuel = false;
            for (UObject* f : fuel) if (f && it == f) is_fuel = true;
            if (is_fuel) ++fuel_s; else ++other_s;
        }
        return cargo_pct_calc(isz, fuel_s, other_s);
    }

    // WHAT: logs one line "MINE cargo probe ship=<id> [class] name=value ..." for the ship and its Inventory: properties whose name has
    //       capacity, size, slot, max, volume, occupied or weight. true when logged (ship alive).
    // WHY: the user sends this line back so the real capacity property can be used for cargo %.
    bool cargo_probe(int sid)
    {
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return false;
        std::wstring s;
        UObject* parts[2] = {ship, get_obj_prop(ship, STR("Inventory"))};
        for (UObject* o : parts)
        {
            if (!alive(o) || !o->GetClassPrivate()) { s += STR(" [none]"); continue; }
            s += STR(" [") + o->GetClassPrivate()->GetName() + STR("]");
            for (FProperty* p : o->GetClassPrivate()->ForEachProperty())
            {
                const std::wstring n = p->GetName();
                if (!name_has(n, STR("capacity")) && !name_has(n, STR("size")) && !name_has(n, STR("slot")) && !name_has(n, STR("max")) &&
                    !name_has(n, STR("volume")) && !name_has(n, STR("occupied")) && !name_has(n, STR("weight"))) continue;
                s += STR(" ") + n + STR("=") + any_prop(o, n.c_str());
            }
        }
        log_line(STR("MINE cargo probe ship=") + std::to_wstring(sid) + s);
        return true;
    }

    // WHAT: items still to move while MS_HOLD (same targets as the MS_HOLD check): refuel = fuel deficit to amount, unload = ore items left in cargo.
    //       -1 when an item object is not alive.
    int64_t hold_left(UObject* ship, int sid, const Config& c, const std::string& trip)
    {
        int64_t left = 0;
        const bool refuel = trip == "dock_refuel";
        for (int i = 0; i < (refuel ? N_FUEL : N_ORE); ++i)
        {
            if (!(refuel ? c.fuel[i].on : unl_ore(sid, i, c))) continue;
            std::wstring w;
            UObject* it = item_c(refuel ? FUEL_ITEM[i] : ORE_ITEM[i], w);
            const int64_t have = alive(it) ? inv_total(ship, it) : -1;
            if (have < 0) return -1;
            if (refuel) { if (have < c.fuel[i].amount) left += c.fuel[i].amount - have; }
            else left += have;
        }
        return left;
    }

    // WHAT: once per status second per bench ship: cargo probe (first time only), position and speed (m/s) from the actor location delta
    //       (uu = cm: delta / el / 100), cargo fill %. Read-only: no game writes. el = seconds since the last sample.
    // ponytail: no glitch filter here (spd_update has one for the speed monitor); add it if floating-origin jumps show as speed spikes.
    void mine_sample(double el)
    {
        for (int sid : g_bench_ids)
        {
            MineSt& m = g_mine[sid];
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(ship)) { m.pos_ok = false; m.spd = 0.0; m.cargo_pct = -1; m.eta_left = -1; continue; }
            if (!m.probed) m.probed = cargo_probe(sid);
            if (!m.wprobed) { m.wprobed = true; log_line(STR("MINE weapons ship=") + std::to_wstring(sid) + STR(" classes=") + prop_classes(ship, STR("Weapons")) + STR(" primary=") + prop_classes(ship, STR("PrimaryFireGroup")) + STR(" secondary=") + prop_classes(ship, STR("SecondaryFireGroup"))); }
            ActFrame f;
            if (!act_frame(ship, f)) { m.pos_ok = false; m.spd = 0.0; }
            else
            {
                if (m.pos_ok && el > 0.0) m.spd = dist3(f.loc, m.pos) / el / 100.0;
                std::copy(f.loc, f.loc + 3, m.pos);
                m.pos_ok = true;
            }
            const Config c = eff_cfg(sid);
            int slots = 0, fuel_s = 0, other_s = 0;
            m.cargo_pct = cargo_pct(ship, slots, fuel_s, other_s);
            m.clog_t += el;
            if (m.clog_t >= 30.0)
            {
                m.clog_t = 0.0;
                log_line(STR("MINE cargo ship=") + std::to_wstring(sid) + STR(" slots=") + std::to_wstring(slots) + STR(" fuel_slots=") + std::to_wstring(fuel_s) +
                         STR(" other_slots=") + std::to_wstring(other_s) + STR(" pct=") + std::to_wstring(m.cargo_pct));
            }
            m.eta_left = (m.st == MS_HOLD) ? hold_left(ship, sid, c, m.trip) : -1;
        }
    }

    // WHAT: job column text for a dock/undock job as sent: dock_mining -> unload, dock_refuel -> refuel, dock_auto -> dock, undock -> undock.
    // WHY: the Lua action buttons light up while job is not "none" (Lua maps dock/unload/refuel/undock only).
    const char* job_kind_text(const std::string& kind)
    {
        if (kind == "dock_mining") return "unload";
        if (kind == "dock_refuel") return "refuel";
        if (kind == "dock_auto") return "dock";
        return "undock";
    }

    // WHAT: jobphase text from the dock job's internal stage: undock -> undocking, stage 0/1 -> ordering, 2 -> docking, 3 -> preparing.
    const char* job_phase_text(const DockJob& j)
    {
        if (j.cmd == "undock") return "undocking";
        if (j.stage <= 1) return "ordering";
        if (j.stage == 2) return "docking";
        return "preparing";
    }

    // WHAT: MiningStatusX.txt, one line per bench ship: "<id>|<run 0/1>|<cargo %>|<speed m/s>|<next state>|<left items or -1>|<job>|<jobphase>|<resource or ->|<sys>,<chx>,<chy>,<chz> or ->".
    //       field 9 = current queue row item (eff_cfg resource), "-" when empty. field 10 = ship system and chunk (Sector Selector km), "-" when unreadable.
    //       job = none, or the kind of a running command-started (seq >= 0) dock/undock job or move job of the ship (dock jobs: job_kind_text;
    //       move jobs: MoveJob kind). jobphase = job_phase_text / aiming (move phase 1) / approaching (move phase 2); none when job is none.
    //       Loop-started jobs (seq -1) stay none: the loop's activity is already in the simple status. Written every call (no unchanged skip).
    void mine_status_x_write()
    {
        std::string s;
        for (int sid : g_bench_ids)
        {
            const MineSt& m = g_mine[sid];
            const std::string res = eff_cfg(sid).resource;
            std::string job = "none", phase = "none";
            for (const DockJob& j : g_jobs)
                if (j.sid == sid && j.seq >= 0) { job = job_kind_text(j.kind); phase = job_phase_text(j); }
            if (mv_active(sid) && g_mvs[sid].seq >= 0) { const MoveJob& mv = g_mvs[sid]; job = mv.kind; phase = mv.phase == 1 ? "aiming" : "approaching"; }
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            int sys = 0;
            int64_t ch[3] = {0, 0, 0};
            std::string have = "-";   // fields 11/12: ore of the current row in the ship inventory / row amount (0 = unlimited)
            if (alive(ship) && !res.empty()) { std::wstring wy; UObject* it = item_c(res, wy); const int64_t v = alive(it) ? inv_total(ship, it) : -1; if (v >= 0) have = std::to_string(v); }
            const std::string where = alive(ship) && ship_pos(ship, sys, ch) ? std::to_string(sys) + "," + std::to_string(ch[0]) + "," + std::to_string(ch[1]) + "," + std::to_string(ch[2]) : std::string("-");
            s += std::to_string(sid) + "|" + (m.run ? "1" : "0") + "|" + std::to_string(m.cargo_pct) + "|" + std::to_string(m.spd) + "|" + mine_next(m) + "|" + std::to_string(m.eta_left) + "|" + job + "|" + phase + "|" + (res.empty() ? std::string("-") : res) + "|" + where + "|" + have + "|" + std::to_string(eff_cfg(sid).amount) + "\n";
        }
        static std::string last;   // unchanged: no file write
        if (s != last) { try { write_atomic(get_dll_dir() / "MiningStatusX.txt", s); last = s; } catch (...) {} }
    }

    // WHAT: mb_net.hpp hooks. Seconds docked in the current hold (MS_HOLD clock). 0 when not holding.
    double net_hold_s(int sid)
    {
        const MineSt& m = g_mine[sid];
        return m.st == MS_HOLD ? m.t : 0.0;
    }

    // WHAT: mb_net.hpp hook. Working = a dock job runs, or the ship is in any state but OFF/HALT (loop, button trip, hold, undock, sweep).
    //       Parked = OFF/HALT (switched off, queue done, error, trip ended). Not tied to m.run: button and Unload ships run with run=false.
    bool net_holder_working(int sid)
    {
        if (job_running(sid)) return true;
        const MineSt& m = g_mine[sid];
        return m.st != MS_OFF && m.st != MS_HALT;
    }

    // WHAT: queue score input of ship sid (mb_net.hpp declares it): cargo fill %, fuel low (auto rule says refuel), hull hit (flee), ore for a dock in cargo.
    // WHY: net_pick ranks waiting ships by these (fullest cargo first, then low fuel, then waiting time). purpose is not used yet.
    NetReq net_req_of(int sid, Purpose purpose)
    {
        (void)purpose;
        NetReq r;
        const auto mi = g_mine.find(sid);
        if (mi != g_mine.end()) r.cargo_pct = std::max(0, mi->second.cargo_pct);
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (alive(ship))
        {
            std::wstring w, nm, nd;
            r.fuel_low = auto_rule(sid, ship, w) == "dock_refuel";
            r.has_dock_ore = unload_pending(ship, eff_cfg(sid), nm, nd) > 0;
        }
        return r;
    }

    // WHAT: MiningLog.txt, one line per Log tab entry: "<ship id>|<hh:mm:ss>|<text>", ships in bench order, oldest first. Written only when changed.
    void mine_log_write()
    {
        std::string s;
        for (int sid : g_bench_ids)
        {
            auto it = g_mine_log.find(sid);
            if (it == g_mine_log.end()) continue;
            for (const MineLogLine& l : it->second) s += std::to_string(sid) + "|" + u8(l.hms) + "|" + u8(l.text) + "\n";
        }
        static std::string last = "\x01";
        if (s == last) return;
        last = s;
        try { write_atomic(get_dll_dir() / "MiningLog.txt", s); } catch (...) {}
    }

    void mine_set_bench(const std::vector<std::pair<UObject*, int>>& bench)
    {
        g_bench_ids.clear();
        for (const auto& p : bench)
            if (p.second > 0)
            {
                g_bench_ids.push_back(p.second);
                std::wstring n = obj_display(p.first);
                if (n.empty()) n = p.first->GetName();
                g_bench_names[p.second] = n;
            }
        std::vector<int> ids;
        for (const auto& p : bench) if (p.second > 0) ids.push_back(p.second);
        chunk_hold_keep_only(ids);   // a ship that left the bench drops its chunk hold
    }

    // STOP ALL: hard stop, latch HALT and run=false; Start loop restarts.
    void mine_halt(int sid)
    {
        warp_cancel(sid);   // before the early return: ships without a MineSt too
        chunk_hold_release(sid);
        net_leave_slot(sid);     // leaves the dock queue (STOP ALL on a waiter)
        g_flee_pick.erase(sid);
        auto it = g_mine.find(sid);
        if (it == g_mine.end()) return;
        it->second.btn = false;
        it->second.flee = false; it->second.warp_to = 0; it->second.col_rock = 0; it->second.sweep = false;
        it->second.st = MS_HALT;
        it->second.run = false;
        it->second.since_stop = 0.0;
        it->second.dl_n = 0;   // also cancels a dock time limit wait (MS_DLWAIT is left by st = MS_HALT)
        it->second.result = STR("stopped by STOP ALL");
        log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" stopped by STOP ALL"));
        mlog(sid, STR("Stopped: loop halted (stop)"));
    }

    // WHAT: Start loop / Stop loop. run is runtime only: a game restart means stopped.
    // Stop is the soft stop = the existing switch-off path in mine_step (flying: stop work and fly home, trip: finish and park). STOP ALL stays the hard stop.
    std::wstring mine_cmd(int sid, bool start)
    {
        if (std::find(g_bench_ids.begin(), g_bench_ids.end(), sid) == g_bench_ids.end()) return STR("error: ship has no MiningBench");
        MineSt& m = g_mine[sid];
        if (start)   // stopping is always allowed
        {
            net_leave_slot(sid);   // a new loop starts with no old queue place
            if (!g_compat.done) return STR("error: COMPAT self-test pending, try again in a few seconds");
            if (!g_compat.ok(F_MINING))
            {
                const std::wstring r = STR("COMPAT: mining disabled after game update (") + widen(g_compat.reason(F_MINING)) + STR("). See UE4SS.log lines COMPAT.");
                mset(sid, m, STR("error"), r);
                mlog(sid, r);
                log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" refused: ") + r);
                return STR("error: ") + r;
            }
            if (!g_compat.ok(F_WARP) && !belt_off(eff_cfg(sid).belt))
            {
                const std::wstring r = STR("COMPAT: belt warp disabled after game update (") + widen(g_compat.reason(F_WARP)) + STR("). Set belt_sector=none or see UE4SS.log lines COMPAT.");
                mset(sid, m, STR("error"), r);
                mlog(sid, r);
                log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" refused: ") + r);
                return STR("error: ") + r;
            }
        }
        m.run = start;
        if (start)
        {
            m.btn = false;             g_item_idx[sid] = 0; m.got = 0; m.fin = false; m.noore = false; m.unl_prev = -1; m.unl_undock = false;   // always: a stale queue position must not survive a Start
            m.sweep = false;
            m.flee = false; m.warp_to = 0; m.tried.clear(); m.hull = -1; m.dl_n = 0;    // a new run starts with no flee, no pending warp, no sector blacklist, no dock time limit rounds
            if (m.st == MS_HALT) m.st = MS_OFF;
            m.chk_t = MINE_ROCK_CHECK_S; m.fails = 0; m.unl_chk = 0; m.leave = false;
            mset(sid, m, STR("starting"), STR("Loop started"));
        }
        else mlog(sid, STR("Loop stop requested"));
        log_line(STR("MINE ship=") + std::to_wstring(sid) + (start ? STR(" loop started") : STR(" loop stop requested")));
        return STR("ok");
    }

    // WHAT: collect ore action (Test command collect_ore): sweep the floating ore of the ship's system, dock when cargo is full, go back.
    std::wstring sweep_cmd(int sid)
    {
        if (std::find(g_bench_ids.begin(), g_bench_ids.end(), sid) == g_bench_ids.end()) return STR("error: ship has no MiningBench");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        if (!g_compat.ok(F_MINING)) return STR("error: COMPAT: mining disabled");
        MineSt& m = g_mine[sid];
        m.run = false; m.leave = false; m.fin = false; m.btn = true; m.err = false; m.unl_prev = -1; m.unl_undock = false;
        m.sweep = true; m.sw_rescans = 0; m.sw_back = false; m.sw_nofly = false; m.sw_ore = nullptr; m.unl_chk = 0;
        m.sw_undock_n = 0; m.sw_found = -1; m.sw_frame = false;
        m.st = MS_SWEEP; m.t = 0.0;
        mv_cancel(sid);   // a leftover move job of the mining loop must not block the sweep's own moves
        g_job_res.erase(sid);
        run_cmd(-1, sid, "tractor_on");
        run_cmd(-1, sid, "laser_off");
        const bool docked = ship_docked(ship);
        sweep_scan(ship, sid, m);
        log_line(STR("SWEEP cmd ship=") + std::to_wstring(sid) + STR(" docked=") + (docked ? STR("1") : STR("0")) + STR(" found=") + std::to_wstring(m.sw_found) +
                 STR(" ok=") + std::to_wstring(m.sw_list.size()) + STR(" frame=") + (m.sw_frame ? STR("1") : STR("0")));
        mlog(sid, STR("Collect: ") + std::to_wstring(m.sw_list.size()) + STR(" ore found (of ") + std::to_wstring(m.sw_found) + STR(")"));
        sw_phase(ship, sid, m, SW_UNDOCK);
        mset(sid, m, STR("collecting ore"), STR("Collecting ore"));
        mlog(sid, STR("Action: collect ore"));
        return STR("ok");
    }

    // WHAT: Start all / Stop all: apply mine_cmd to every bench ship. WHY: MiningCmd.txt carries one command at a time.
    std::wstring mine_cmd_all(bool start)
    {
        int n = 0;
        for (int sid : g_bench_ids) if (mine_cmd(sid, start) == STR("ok")) ++n;
        return STR("ok ") + std::to_wstring(n) + STR(" ships");
    }


    // WHAT: "Warp to belt" button: warps the ship to the sector of its belt setting (AUTO = closest sector with the current row's ore).
    //       Refused while the loop runs. Returns "ok ..." or "error: ...".
    // WHY: lets the user move the ship to the belt without starting the loop; same warp as the loop uses.
    std::wstring warp_belt_cmd(int sid)
    {
        if (std::find(g_bench_ids.begin(), g_bench_ids.end(), sid) == g_bench_ids.end()) return STR("error: ship has no MiningBench");
        if (!g_compat.usable(F_WARPREAL)) return STR("error: COMPAT warp disabled (") + widen(g_compat.done ? g_compat.reason(F_WARPREAL) : std::string("self-test pending")) + STR(")");
        if (g_mine[sid].run) return STR("error: stop the loop first");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        const Config c = eff_cfg(sid);
        if (belt_off(c.belt)) return STR("error: no sector set");
        std::wstring why, nm;
        UObject* item = c.resource.empty() ? nullptr : item_c(c.resource, why);
        const std::set<int> none;
        int cur = 0;
        const int t = belt_target(ship, c.belt, item, none, cur, nm, why);
        if (t == -2) return STR("error: ") + why;
        if (t < 0) return STR("error: sector data unreadable");
        if (t == 0) return STR("error: ") + why;
        if (t == cur) return STR("ok already in sector");
        const std::wstring r = warp_real_cmd(sid, std::to_string(t));
        if (ok_res(r)) mlog(sid, STR("Warping to ") + nm + STR(" (") + std::to_wstring(t) + STR(")"));
        return r;
    }

    // WHAT: once a second, after net_tick: a docked holder that blocks a waiter is sent away when net_bad_holder says so (parked/idle, no ore of
    //       this dock in cargo, or the dock is full): undock job, status "Leaving dock". The waiter keeps its queue place.
    //       No second dock to leave to: waiters show NR_WAIT_PARKED. Holder not a bench ship: waiters show NR_WAIT_UNMANAGED (log once a minute).
    void net_holder_orders()
    {
        static std::map<int, double> unmanaged_log;   // holder id -> time of its last log line
        for (auto& kv : g_net_docks)
        {
            NetDock& d = kv.second;
            if (!d.exists || d.occ == 0 || d.queue.empty()) continue;
            const int h = d.occ;
            if (std::find(g_bench_ids.begin(), g_bench_ids.end(), h) == g_bench_ids.end())
            {
                for (int q : d.queue) { auto it = g_net_ships.find(q); if (it != g_net_ships.end()) { it->second.reason = NR_WAIT_UNMANAGED; it->second.blocked_by = h; } }
                const auto ul = unmanaged_log.find(h);
                if (ul == unmanaged_log.end() || g_now - ul->second >= 60.0)
                {
                    unmanaged_log[h] = g_now;
                    log_line(STR("NET dock=") + std::to_wstring(d.id) + STR(" held by ship ") + std::to_wstring(h) + STR(", not managed"));
                }
                continue;
            }
            if (job_running(h)) continue;   // the holder's own dock job runs: let it finish
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), h);
            if (!alive(ship)) continue;
            const Config c = eff_cfg(h);
            bool has_ore = false;
            for (int i = 0; i < N_ORE && !has_ore; ++i)
            {
                if (std::find(c.ore_docks[i].begin(), c.ore_docks[i].end(), d.id) == c.ore_docks[i].end()) continue;
                std::wstring w;
                UObject* it = item_c(ORE_ITEM[i], w);
                if (alive(it) && inv_total(ship, it) > 0) has_ore = true;
            }
            const HolderState st = net_holder_working(h) ? HS_WORKING : HS_PARKED;
            if (!net_bad_holder(st, has_ore, d.full, static_cast<int>(d.queue.size()), g_now - d.occ_since)) continue;
            const std::wstring ur = job_start(h, "undock", -1);
            if (undock_failed(ur))
            {
                for (int q : d.queue) { auto it = g_net_ships.find(q); if (it != g_net_ships.end()) { it->second.reason = NR_WAIT_PARKED; it->second.blocked_by = h; } }
                continue;
            }
            const std::string why = d.full ? "full" : (st == HS_PARKED ? "parked" : "empty");
            mlog(h, STR("Control: ") + widen(net_ship_label(h)) + STR(" asked to leave ") + widen(net_dock_label(d.id)) + STR(" (") + widen(why) + STR(")"));
            const std::wstring who = STR("Leaving dock for ship ") + widen(net_ship_label(d.queue.front()));
            mset(h, g_mine[h], who, who);
            log_line(STR("NET dock=") + std::to_wstring(d.id) + STR(" UNDOCK holder=") + std::to_wstring(h) + STR(" for=") + std::to_wstring(d.queue.front()));
        }
    }

    void mining_tick(double dt)
    {
        for (int sid : g_bench_ids)
        {
            MineSt& m = g_mine[sid];
            try { mine_step(sid, m, dt); }
            catch (...) { m.st = MS_HALT; m.result = STR("exception in mining step"); log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" exception")); }
        }
        g_mine_status_acc += dt;
        if (g_mine_status_acc >= 1.0)
        {
            const double el = g_mine_status_acc;
            g_mine_status_acc = 0.0;
            mine_sample(el);
            net_tick();
            net_holder_orders();
            mine_status_write();
            mine_status_x_write();
            mine_log_write();
            sectors_file_tick(el);
        }
    }
}
