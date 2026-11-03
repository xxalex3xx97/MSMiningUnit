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
    void chunk_hold_tick(int sid);                    // forced rock passes at +1 s and +3 s after a set
    bool chunk_hold_settling(int sid);                // true while the ship's cube was set less than 4 s ago
    void chunk_hold_keep_only(const std::vector<int>& sids);   // release holds of ships no longer on the bench

    constexpr double MINE_ROCK_CHECK_S = 5.0;   // rock scan period (s): the scan walks every rock of the ore, keep it rare
    constexpr double MINE_FUEL_CHECK_S = 5.0;   // fuel check period while flying out / mining (s)
    constexpr double MINE_HOLD_CHECK_S = 2.0;   // at-dock completion check period (s)
    constexpr double MINE_BELT_EMPTY_S = 10.0;  // seek with no rock in range for this long -> warp to the belt sector with the ore (s)
    constexpr double MINE_WARP_TIMEOUT_S = 90.0; // warp not arrived after this long -> HALT (s, real warp: aim + charge)
    constexpr double MINE_FLEE_GRACE_S = 10.0;   // after STOP ALL no flee for this long (s)
    constexpr double MINE_DOCK_RETRY_WAIT_S = 30.0; // dock time limit: wait after the undock before docking again (s)

    enum MineState { MS_OFF = 0, MS_CHECK, MS_UNDOCK, MS_SEEK, MS_MINE, MS_TRIP, MS_HOLD, MS_HALT, MS_WARP, MS_DLWAIT };
    const TCHAR* mine_state_name(int s)
    {
        static const TCHAR* n[] = {STR("idle"), STR("checking"), STR("undocking"), STR("approaching rock"), STR("mining"), STR("flying to dock"), STR("at dock"), STR("stopped"), STR("warping"), STR("waiting to dock again")};
        return n[s];
    }

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
        bool dwait = false, err = false;       // dwait: waiting for a busy dock (retry with backoff) / err: stopped by an error (red ERROR)
        double dwait_t = 0.0;                  // seconds since the last dock retry
        int dwait_n = 0;                       // dock retries so far (backoff step)
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
        double since_stop = 1e9;               // seconds since STOP ALL (mine_halt)
        double scan_t = 0.0;                   // ROCKSCAN diagnostic: seconds since the last SEEK line
        int scan_n = 0;                        // ROCKSCAN diagnostic: SEEK lines logged this arrival (max 8)
        double empty_t = 0.0;                  // CHECK: sum of check periods with no rock in range in a row (belt hop at MINE_BELT_EMPTY_S); reset on rock found / loop start / stop
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
    UObject* rock_in_range(const double anchor[3], UObject* item, double rad_m)
    {
        rock_list_ensure();
        ActFrame f;
        std::copy(anchor, anchor + 3, f.loc);
        ActAim r;
        UObject* a = nearest_item_actor(f, item, r);
        if (!a || !actor_ok(a)) return nullptr;
        if (rad_m > 0.0 && r.dist_m > rad_m) return nullptr;
        return a;
    }

    // DIAGNOSTIC (ROCKSCAN): counts over the same rock list rock_in_range scans. No state change.
    struct RockScan { int total = 0, with_ore = 0; double nearest_any = -1.0, nearest_ore = -1.0; ActFrame f; bool have_pos = false; };

    RockScan rock_scan(UObject* ship, UObject* item)
    {
        rock_list_ensure();
        RockScan s;
        s.have_pos = act_frame(ship, s.f);
        const bool has_item = alive(item);
        for (UObject* a : g_rock_actors)
        {
            if (!actor_ok(a)) continue;
            ++s.total;
            auto it = g_rock_item.find(a);
            const bool ore = has_item && it != g_rock_item.end() && it->second == item;
            if (ore) ++s.with_ore;
            if (!s.have_pos) continue;
            ActAim r;
            if (!act_aim(s.f, a, r)) continue;
            if (s.nearest_any < 0.0 || r.dist_m < s.nearest_any) s.nearest_any = r.dist_m;
            if (ore && (s.nearest_ore < 0.0 || r.dist_m < s.nearest_ore)) s.nearest_ore = r.dist_m;
        }
        return s;
    }

    // DIAGNOSTIC (ROCKSCAN): one line with the rock counts around the ship. arrival = true adds the player pawn distance.
    // Player = DMSPlayerController.Pawn (camera pawn); distance in m = uu / 100.
    void rockscan_log(UObject* ship, int sid, const MineSt& m, const Config& c, UObject* item, bool arrival)
    {
        auto f1 = [](double v) { wchar_t b[32]; swprintf(b, 32, L"%.1f", v); return std::wstring(b); };
        const RockScan s = rock_scan(ship, item);
        int sys = 0;
        int64_t ch[3] = {0, 0, 0};
        const bool have_chunk = ship_pos(ship, sys, ch);
        std::wstring pos = STR("n/a");
        if (s.have_pos)
            pos = (have_chunk ? STR("chunk(") + std::to_wstring(ch[0]) + STR(",") + std::to_wstring(ch[1]) + STR(",") + std::to_wstring(ch[2]) + STR(") ") : std::wstring()) +
                  STR("loc_uu(") + f1(s.f.loc[0]) + STR(",") + f1(s.f.loc[1]) + STR(",") + f1(s.f.loc[2]) + STR(")");
        std::wstring line = STR("ROCKSCAN ship=") + std::to_wstring(sid) + STR(" sector=") + std::to_wstring(cur_sector_id(ship)) +
            STR(" pos_abs=") + pos + STR(" asteroids_total=") + std::to_wstring(s.total) + STR(" with_ore=") + std::to_wstring(s.with_ore) +
            STR(" nearest_any_m=") + f1(s.nearest_any) + STR(" nearest_ore_m=") + f1(s.nearest_ore) + STR(" radius_m=") + f1(mine_radius_m(c)) +
            STR(" t_since_arrival=") + f1(m.t);
        if (arrival)
        {
            double pd = -1.0;
            UObject* ctrl = first_of(STR("DMSPlayerController"));
            UObject* pawn = alive(ctrl) ? obj_prop(ctrl, STR("Pawn")) : nullptr;
            double pl[3] = {0, 0, 0};
            std::wstring why;
            if (alive(pawn) && s.have_pos && actor_vec_call(pawn, STR("K2_GetActorLocation"), pl, why))
            {
                const double dx = pl[0] - s.f.loc[0], dy = pl[1] - s.f.loc[1], dz = pl[2] - s.f.loc[2];
                pd = std::sqrt(dx * dx + dy * dy + dz * dz) / MV_UU_PER_M;
            }
            line += STR(" player_dist_m=") + f1(pd);
        }
        log_line(line);
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

    // WHAT: cancel this ship's running dock/undock job (same steps as stop_all: erase job, order/filter state, autopilot off).
    // WHY: a new dock trip or undock must not fail with "dock job already running for this ship".
    void cancel_dock_job(UObject* ship, int sid)
    {
        warp_cancel(sid);   // a warp waiting for this undock / aiming is cancelled too (a charging one finishes)
        g_unl_ores.erase(sid);
        bool any = false;
        for (size_t i = 0; i < g_jobs.size();)
        {
            if (g_jobs[i].sid != sid) { ++i; continue; }
            tlog(STR("dock job seq=") + std::to_wstring(g_jobs[i].seq) + STR(" cancelled by mining loop"));
            g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(i));
            any = true;
        }
        if (!any) return;
        g_ob.erase(sid); g_pf.erase(sid);
        std::wstring err;
        set_autopilot(sid, false, err);
        zero_all(ship);
    }

    // start a dock trip chosen by the auto rule. Failure -> HALT with the reason.
    void trip_begin(UObject* ship, int sid, MineSt& m, const std::wstring& why, const std::string& force = "")
    {
        cancel_dock_job(ship, sid);
        std::wstring w;
        m.trip = force.empty() ? auto_rule(sid, ship, w) : force;
        g_job_res.erase(sid);
        const std::wstring r = job_start(sid, m.trip, -1);
        m.t = 0.0; m.seek_started = false; m.no_rock = false;
        m.err = false; m.dwait = false; m.warp_to = 0;
        if (r == STR("waiting dock")) { m.st = MS_TRIP; m.dwait = true; m.dwait_t = 0.0; m.dwait_n = 0; mset(sid, m, STR("Waiting for dock"), STR("Waiting for dock")); return; }
        if (r != STR("waiting")) { m.st = MS_HALT; mset(sid, m, STR("dock trip failed: ") + r); return; }
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
        if (m.flee) { m.flee = false; m.fin = false; m.err = true; m.st = MS_HALT; mset(sid, m, STR("ERROR: hull damaged, fled to dock, stopped"), STR("ERROR: ship was attacked, fled to dock, stopped")); return; }
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
            if (m.st == MS_SEEK || m.st == MS_MINE || m.st == MS_WARP) { m.leave = true; go_home(ship, sid, m, STR("mining switched off")); return true; }
            m.leave = true;   // UNDOCK / TRIP / HOLD: let them end, then park
        }
        else m.leave = false;
        // 1b. hull hit while flying / mining / warping: stop work, dock trip (flee), trip_end halts with the red ERROR
        if (m.st == MS_SEEK || m.st == MS_MINE || m.st == MS_WARP)
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
        if (m.st == MS_SEEK || m.st == MS_MINE)
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
        if (cargo_full(ship, item))
        {
            // loop guard: a second unload from CHECK without mining in between means the cargo is not in the unload list
            if (m.unl_chk++ >= 1) { m.st = MS_HALT; mset(sid, m, STR("cargo still full after unload (is the ore enabled for unload?)")); return; }
            trip_begin(ship, sid, m, STR("cargo ready to unload"));
            return;
        }
        ActFrame f;
        if (!act_frame(ship, f)) { m.st = MS_OFF; m.chk_t = 0.0; mset(sid, m, STR("ship actor unreadable")); return; }
        std::copy(f.loc, f.loc + 3, m.anchor);
        // belt: ship not in the target sector -> warp after the undock (after_undock); HALT when no sector has the ore
        if (!belt_off(c.belt) && !rock_in_range(m.anchor, item, mine_radius_m(c)))   // rock in range here -> mine it, warp only when none
        {
            int cur = 0;
            std::wstring nm, why2;
            const int t = belt_target(ship, c.belt, item, m.tried, cur, nm, why2);
            if (t == -2) { m.fin = true; m.noore = true; go_home(ship, sid, m, why2); return; }
            if (t == 0) { m.st = MS_HALT; m.err = true; mset(sid, m, STR("ERROR: ") + why2, STR("ERROR: ") + why2); return; }
            if (t > 0 && t != cur) m.warp_to = t;
        }
        if (m.warp_to == 0 && !rock_in_range(m.anchor, item, mine_radius_m(c)))
        {
            // undocked ship (far sector, Start loop): hold its chunk cube so the next check sees the rocks. The belt counter waits while it settles.
            const bool docked = ship_docked(ship);
            if (!docked) chunk_hold_update(sid, ship);
            if (!chunk_hold_settling(sid)) m.empty_t += MINE_ROCK_CHECK_S;
            // belt, ship not docked: no rock in range for MINE_BELT_EMPTY_S and none of the ore at all (unlimited) -> next sector (same as SEEK)
            // WHY: the HUD list is the PLAYER's scan list and can miss whole ores (silica rocks seen on the HUD were not in it); a docked ship or belt off never asked for the world scan.
            rock_fallback_request();
            if (!belt_off(c.belt) && !docked)
            {
                if (m.empty_t >= MINE_BELT_EMPTY_S && !rock_in_range(m.anchor, item, mine_radius_m(c)) && belt_go(ship, sid, m, c, item, true) != 0) return;
            }
            {   // WHY: "nothing in range" gave no reason; one ROCKSCAN line per check shows list size, rocks with this ore and the nearest distances (m from the ship)
                const RockScan rs = rock_scan(ship, item);
                log_line(STR("ROCKSCAN nothing-in-range ship=") + std::to_wstring(sid) + STR(" listed=") + std::to_wstring(g_rock_actors.size()) + STR(" live=") + std::to_wstring(rs.total) + STR(" with_ore=") + std::to_wstring(rs.with_ore) + STR(" nearest_any_m=") + std::to_wstring(static_cast<long long>(rs.nearest_any)) + STR(" nearest_ore_m=") + std::to_wstring(static_cast<long long>(rs.nearest_ore)) + STR(" radius_m=") + std::to_wstring(static_cast<long long>(mine_radius_m(c))) + STR(" want=") + (alive(item) ? item->GetName() : std::wstring(STR("?"))));
                std::map<std::wstring, int> hist;   // ore item name -> rock count over the listed rocks
                for (UObject* a : g_rock_actors) { auto it = g_rock_item.find(a); hist[it != g_rock_item.end() && alive(it->second) ? it->second->GetName() : std::wstring(STR("<none>"))]++; }
                std::wstring h;
                for (const auto& kv : hist) h += kv.first + STR("=") + std::to_wstring(kv.second) + STR(" ");
                log_line(STR("ROCKSCAN items ship=") + std::to_wstring(sid) + STR(" ") + h);
                {   // PROBE (once a minute): save objects whose ResourceItems holds the wanted ore, vs how many have a live actor in the list
                    static ULONGLONG last_probe = 0;
                    const ULONGLONG nowp = GetTickCount64();
                    if (alive(item) && nowp - last_probe >= 60000)
                    {
                        last_probe = nowp;
                        std::vector<UObject*> saves;
                        UObjectGlobals::FindAllOf(STR("SmallResourceAsteroidObject"), saves);
                        int tot = 0, ore = 0, ore_alive = 0, ore_listed = 0, ore_actor = 0, ore_same_sys = 0;
                        int sys0 = 0; int64_t ch0[3] = {0, 0, 0};
                        const bool have_ship = ship_pos(ship, sys0, ch0);
                        int64_t best_cheb = -1;
                        for (UObject* o2 : saves)
                        {
                            if (!alive(o2)) continue;
                            ++tot;
                            auto* arr = o2->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("ResourceItems"));
                            bool has = false;
                            for (int32_t i = 0; arr && i < arr->Num(); ++i) if ((*arr)[i] == item) has = true;
                            if (!has) continue;
                            ++ore;
                            bool al = true; get_bool_prop(o2, STR("bAlive"), al);
                            if (al) ++ore_alive;
                            if (g_rock_by_id.count(obj_id(o2))) ++ore_listed;
                            if (alive(get_obj_prop(o2, STR("Actor")))) ++ore_actor;
                            int64_t c2[3] = {0, 0, 0};
                            if (have_ship && sys_id(o2) == sys0 && get_chunk(o2, c2)) { ++ore_same_sys; const int64_t d = std::max({std::llabs(c2[0] - ch0[0]), std::llabs(c2[1] - ch0[1]), std::llabs(c2[2] - ch0[2])}); if (best_cheb < 0 || d < best_cheb) best_cheb = d; }
                        }
                        std::wstring cc;   // live actors per asteroid actor class name
                        for (const TCHAR* cn : {STR("BP_SmallAsteroid_Silica_C"), STR("BP_LargeAsteroid_Silica_02_C"), STR("BP_LargeAsteroid_Titanium_02_C"), STR("BP_SmallAsteroid_Titanium_C"), STR("BP_ResourceAsteroid_C"), STR("BP_ResourceAsteroid_02_C"), STR("BP_ResourceAsteroid_03_C"), STR("BP_GenericResourceAsteroid_C"), STR("BP_Asteroid_C")})
                        {
                            std::vector<UObject*> v; UObjectGlobals::FindAllOf(cn, v);
                            int n = 0; for (UObject* x : v) if (alive(x)) ++n;
                            cc += std::wstring(cn) + STR("=") + std::to_wstring(n) + STR(" ");
                        }
                        log_line(STR("ROCKSCAN classes ") + cc);
                        log_line(STR("ROCKSCAN probe saves=") + std::to_wstring(tot) + STR(" with_wanted_ore=") + std::to_wstring(ore) + STR(" alive=") + std::to_wstring(ore_alive) + STR(" has_actor_in_list=") + std::to_wstring(ore_listed) + STR(" actor_loaded=") + std::to_wstring(ore_actor) + STR(" same_system=") + std::to_wstring(ore_same_sys) + STR(" nearest_chunks=") + std::to_wstring(best_cheb));
                    }
                }
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
                m.st = MS_MINE; m.t = 0.0; m.idle_t = 0.0; m.chk_t = 0.0;
                m.last_ore = inv_total(ship, item);
                mine_fire(sid, true);
                mset(sid, m, STR("mining rock ") + std::to_wstring(m.rock), STR("Mining ") + item_name(c.resource) + STR(" (rock ") + std::to_wstring(m.rock) + STR(")"));
                return;
            }
            // rock lost (another ship mined it) = free retry, not a failed attempt
            if (r.find(STR("rock lost")) == std::wstring::npos && ++m.fails >= std::max(1, c.max_attempts)) { mine_stop_work(ship, sid); m.st = MS_HALT; mset(sid, m, STR("approach failed ") + std::to_wstring(m.fails) + STR("x: ") + r); return; }
            mset(sid, m, STR("approach ended: ") + r + STR(", retrying"));
        }
        if (m.no_rock && !chunk_hold_settling(sid)) m.rng_t += dt;   // the belt counter starts 4 s after the hold was set
        m.chk_t += dt;
        if (m.chk_t >= MINE_ROCK_CHECK_S)
        {
            m.chk_t = 0.0;
            UObject* rock = rock_in_range(m.anchor, item, mine_radius_m(c));
            m.no_rock = !rock;
            if (!rock)
            {
                mset(sid, m, STR("nothing in range"));
                rock_fallback_request();   // any seek (belt on or off): the HUD list can miss ores; belt seek: the next rock_in_range may use the FindAllOf fallback
                // belt: no rock in range for MINE_BELT_EMPTY_S and no rock of the ore at all (unlimited search) -> warp to the next sector
                if (!belt_off(c.belt) && m.rng_t >= MINE_BELT_EMPTY_S && !rock_in_range(m.anchor, item, mine_radius_m(c)) && belt_go(ship, sid, m, c, item, true) != 0) return;
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
        // DIAGNOSTIC (ROCKSCAN): belt mode, no rock in range -> one line every 2 s, max 8 per arrival
        if (m.no_rock && !belt_off(c.belt))
        {
            m.scan_t += dt;
            if (g_debug && m.scan_t >= 2.0 && m.scan_n < 8) { m.scan_t = 0.0; ++m.scan_n; rockscan_log(ship, sid, m, c, item, false); }
        }
        if (m.no_rock && c.idle_seconds > 0 && m.rng_t >= c.idle_seconds)
            go_home(ship, sid, m, STR("nothing in range for ") + std::to_wstring(c.idle_seconds) + STR(" s"));
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
        const bool full = cargo_full(ship, item);
        m.chk_t += dt;
        if (m.chk_t >= MINE_ROCK_CHECK_S)
        {
            m.chk_t = 0.0;
            log_line(STR("MINE ship=") + std::to_wstring(sid) + STR(" ore=") + i64w(inv) + STR("/") + std::to_wstring(c.amount) + STR(" full=") + std::to_wstring(full ? 1 : 0) +
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
            if (wrapped && !c.repeat) { m.fin = true; go_home(ship, sid, m, c.has_items ? STR("queue done") : STR("amount reached")); return; }
            if (wrapped) { go_home(ship, sid, m, STR("queue done, repeat")); return; }   // repeat loop = mine, dock + unload, mine again
            mine_fire(sid, false);
            enter_seek(m);
            mset(sid, m, STR("row done, next item"), c.has_items ? STR("Row done: ") + from + STR(" -> ") + item_name(eff_cfg(sid).resource) : std::wstring());
            return;
        }
        if (full) { go_home(ship, sid, m, STR("cargo full")); return; }
        // no gain = rock spent/destroyed, not a reason to dock: look for another rock (SEEK goes home itself when none is in range)
        if (c.idle_seconds > 0 && m.idle_t >= c.idle_seconds) { mine_fire(sid, false); enter_seek(m); mset(sid, m, STR("no ore gained for ") + std::to_wstring(c.idle_seconds) + STR(" s, seeking next rock")); return; }
        if (m.rock > 0 && !actor_ok(rock_actor_by_id(m.rock))) { mine_fire(sid, false); enter_seek(m); mset(sid, m, STR("rock gone, seeking next rock")); return; }
        return;
    }
    // WHAT: MS_TRIP: dock trip (dock busy retry backoff, job result).
    void st_trip(UObject* ship, int sid, MineSt& m, double dt, const Config& c)
    {
        // dock busy: no job runs; retry the same dock with backoff 2,5,10,20,40,60 s (repeats at 60 s) until it frees up
        if (m.dwait)
        {
            if (m.leave && !m.btn) { m.dwait = false; m.st = MS_HALT; mset(sid, m, STR("stopped while waiting for dock")); return; }
            m.dwait_t += dt;
            if (m.dwait_t < dock_backoff_s(m.dwait_n)) return;
            m.dwait_t = 0.0; ++m.dwait_n;
            g_job_res.erase(sid);
            const std::wstring r = job_start(sid, m.trip, -1);
            if (r == STR("waiting dock")) return;
            m.dwait = false;
            if (r != STR("waiting")) { m.st = MS_HALT; mset(sid, m, STR("dock trip failed: ") + r); return; }
            mset(sid, m, STR("dock free, going to dock"), STR("Dock free, going"));
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
            m.dwait = true; m.dwait_t = 0.0; m.dwait_n = 0;
            mset(sid, m, STR("Waiting for dock"), STR("Waiting for dock"));
            return;
        }
        if (ok_res(r)) { m.st = MS_HOLD; m.t = 0.0; m.hold_idle = 0.0; m.hold_last = -1; m.chk_t = MINE_HOLD_CHECK_S; m.hold_seen = false; m.left_last.clear(); mset(sid, m, STR("docked, waiting for orders to finish"), is_refuel(m) ? STR("Docked, refueling") : STR("Docked, unloading")); }
        else { m.st = MS_HALT; mset(sid, m, STR("dock trip failed: ") + r); }
        return;
    }
    // WHAT: true when ore i is unloaded on this ship's current dock trip: enabled and in g_unl_ores[sid]. No entry for the ship = every enabled ore.
    // WHY: the other ores stay in cargo for the next trip, so MS_HOLD must not wait for them.
    bool unl_ore(int sid, int i, const Config& c)
    {
        if (!c.ore_on[i]) return false;
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
            for (int i = 0; i < N_ORE; ++i)
            {
                if (!unl_ore(sid, i, c)) continue;
                std::wstring w;
                UObject* it = item_c(ORE_ITEM[i], w);
                const int64_t have = alive(it) ? inv_total(ship, it) : -1;
                if (have > 0) { done = false; left += widen(ORE_KEY[i]) + STR(" ") + i64w(have) + STR(" "); }
            }
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
            m.scan_t = 0.0; m.scan_n = 0;
            chunk_hold_update(sid, ship);   // chunk hold at once: the rock list (fallback pass) sees the belt rocks
            mset(sid, m, STR("arrived, seeking rock"), STR("Arrived, seeking rock"));
            std::wstring iwhy;
            if (g_debug) rockscan_log(ship, sid, m, c, item_c(c.resource, iwhy), true);   // DIAGNOSTIC (ROCKSCAN) arrival line
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
        if (!alive(ship)) { chunk_hold_release(sid); m.st = MS_OFF; mset(sid, m, STR("ship not found")); return; }
        const Config c = eff_cfg(sid);
        const bool on = m.run && g_cfg.enabled == 1 && c.enabled == 1;
        m.t += dt;
        // chunk hold: SEEK/MINE keep the cube of the ship's chunk (update also drops it when the player flies the ship). OFF, CHECK and UNDOCK
        // keep what is there (CHECK sets it for an undocked ship with nothing in range); the other states release it. Forced rock passes: chunk_hold_tick.
        if ((m.st == MS_SEEK || m.st == MS_MINE) && on) chunk_hold_update(sid, ship);
        else if (m.st != MS_OFF && m.st != MS_CHECK && m.st != MS_UNDOCK) chunk_hold_release(sid);
        chunk_hold_tick(sid);
        if (step_pre(ship, sid, m, dt, on)) return;
        switch (m.st)
        {
        case MS_OFF: st_off(ship, sid, m, dt, c); return;
        case MS_CHECK: st_check(ship, sid, m, dt, c); return;
        case MS_UNDOCK: st_undock(ship, sid, m, dt, c); return;
        case MS_SEEK: st_seek(ship, sid, m, dt, c); return;
        case MS_MINE: st_mine(ship, sid, m, dt, c); return;
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
        case MS_UNDOCK: case MS_SEEK: case MS_MINE: return "mining";
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
        dock_occ_write();
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
        auto it = g_mine.find(sid);
        if (it == g_mine.end()) return;
        it->second.btn = false;
        it->second.flee = false; it->second.warp_to = 0;
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
            m.btn = false;             g_item_idx[sid] = 0; m.got = 0; m.fin = false; m.noore = false;   // always: a stale queue position must not survive a Start
            m.flee = false; m.warp_to = 0; m.tried.clear(); m.hull = -1; m.dl_n = 0;    // a new run starts with no flee, no pending warp, no sector blacklist, no dock time limit rounds
            if (m.st == MS_HALT) m.st = MS_OFF;
            m.chk_t = MINE_ROCK_CHECK_S; m.fails = 0; m.unl_chk = 0; m.leave = false;
            mset(sid, m, STR("starting"), STR("Loop started"));
        }
        else mlog(sid, STR("Loop stop requested"));
        log_line(STR("MINE ship=") + std::to_wstring(sid) + (start ? STR(" loop started") : STR(" loop stop requested")));
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
            mine_status_write();
            mine_status_x_write();
            mine_log_write();
            sectors_file_tick(el);
        }
    }
}
