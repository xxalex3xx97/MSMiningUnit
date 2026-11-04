// FILE: mods/MiningBenchMod/mb_testcmd.hpp
// WHAT: the command poller (cmd_init, cmd_poll, cmd_tick) and the command dispatch (run_cmd). Dev-only commands (thr_*, rock_sel,
//       face/approach, warp tests) are in mb_devcmd.hpp (dev_cmd); thr_check runs from cmd_tick.
// WHY separate: the live command channel (MiningCmd.txt) is used by the product mining loop as well as the Test tab, so it lives here, apart from the steering and job code (mb_steer.hpp, mb_dockjob.hpp).
// DEPENDS ON: mb_common.hpp, mb_config.hpp (eff_cfg, Config), mb_ue.hpp (find_by_id, get_obj_prop, prop_int, prop_bool, inv_total,
//             call_int, set_int_prop, get_int_prop), mb_docks.hpp (find_item), mb_weapons.hpp (set_all_firing,
//             fire_input), mb_orders.hpp (order_by_id, dist_target_m, set_autopilot), mb_cmdstate.hpp (g_cmd, g_cmd_*_acc, CMD_*),
//             mb_rocks.hpp (rock_list_ensure, act_frame, act_aim, nearest_item_actor, pick_rock, rock_actor_by_id, rock_id_of,
//             rock_kind, actor_ok, g_sel_rock), mb_steer.hpp (move_start, move_tick, spd_update,
//             g_mv, MoveJob, view_zero, zero_all, g_spd, g_last_err, MV_SPEED_ABORT), mb_dockjob.hpp (job_start, jobs_tick, g_jobs,
//             g_undock_state), mb_mining.hpp (mining_tick, mine_halt: M3 loop).
// USED BY: dllmain.cpp (cmd_init at load, cmd_tick from the engine tick hook). The Lua Test tab writes MiningCmd.txt
//          (no status file is read any more).
// GAME FACTS (fact = seen in game; ASSUMPTION = from code/notes; UNVERIFIED = not confirmed):
//   - ASSUMPTION: laser = MiningLaser class in the PRIMARY fire group (group 1); tractor = ItemScooper class in the SECONDARY group (group 2).
//     Taken from the manual-run log (2026-10-27), not re-checked.
//   - fact: the status fields (bAutopilot, DockID, TargetDockID, CurrentOrderIndex, order DockID/bHasUpdated) are native ship/order props.

#pragma once

namespace
{
    // WHAT: dispatch one command line to its handler and return the result text ("ok ..." / "waiting" / "error: ...").
    //       Steps: 1 dock_mining, dock_refuel, undock -> job_start (multi-step, result set later by the job).
    //       2 thr_up, thr_down, thr_zero -> thr_cmd. 3 rock_sel ... -> rock_sel. 4 face_rock, approach_rock -> move_start (multi-step).
    //       5 laser_on/off, tractor_on/off: SetFiring on every instance of the class (set_all_firing) plus the fire group input
    //         (fire_input: laser = group 1, tractor = group 2). set_all_firing writes the weapon actor's SetFiring and the bFiring flag.
    //         No instance -> error. 6 stop_all: lasers and scooper off, group input off,
    //       dock job and move job of this ship cancelled (rotation aim zeroed), autopilot off, zero_all.
    //         Mining loop (step 0, step 1b): mine_start_all / mine_stop_all (sid 0, all bench ships), mine_start / mine_stop (one ship): mb_mining.hpp mine_cmd.
    //         Collect ore action (collect_ore): sweep_cmd (mb_mining.hpp), MS_SWEEP.
    //         8 anything else -> unknown error.
    // WHY: one place lists every Test tab command. The multi-step commands return "waiting" and finish later through their job.
    std::wstring run_cmd(int seq, int sid, const std::string& cmd)
    {
        // dock pick is cleared by stop, loop start/stop and the all-ship commands
        if (cmd == "stop_all" || cmd == "mine_start" || cmd == "mine_stop") g_dock_pick.erase(sid);
        if (cmd == "mine_start_all" || cmd == "mine_stop_all") g_dock_pick.clear();
        // 0. Start all / Stop all (sid 0): every bench ship, before any ship lookup
        if (cmd == "mine_start_all" || cmd == "mine_stop_all") return mine_cmd_all(cmd == "mine_start_all");
        // 0b. Emergency stop, all bench ships: stop_all for each bench ship inside this one command (one tick, no 400 ms fan-out)
        if (cmd == "stop_all_ships")
        {
            const std::vector<int> ids = g_bench_ids;   // copy: the list is not changed by stop_all, kept safe anyway
            int bad = 0;
            for (int s : ids) if (run_cmd(-1, s, "stop_all").rfind(STR("error"), 0) == 0) ++bad;
            tlog(STR("stop_all_ships ships=") + std::to_wstring(ids.size()) + STR(" errors=") + std::to_wstring(bad));
            return bad == 0 ? STR("ok ships=") + std::to_wstring(ids.size()) : STR("error: stop_all_ships failed for ") + std::to_wstring(bad);
        }
        // Dock picker: dock_to:<id> = the Dock button flow (trip/hold states) with the chosen dock; ini untouched
        if (cmd.rfind("dock_to:", 0) == 0)
        {
            const int dk = std::atoi(cmd.c_str() + 8);
            if (dk <= 0) return STR("error: bad dock id");
            UObject* bship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(bship)) return STR("error: ship not found");
            mlog(sid, STR("Action: dock at ") + std::to_wstring(dk));
            if (!g_mine.count(sid)) return STR("error: not a bench ship");
            g_dock_pick[sid] = dk;
            MineSt& bm = g_mine[sid];
            bm.run = false; bm.leave = true; bm.fin = false; bm.btn = true;
            bm.unl_prev = -1; bm.unl_undock = false;
            trip_begin(bship, sid, bm, STR("button"), "");
            return STR("ok");
        }
        // 1. dock and undock jobs
        if (cmd == "dock_mining" || cmd == "dock_refuel" || cmd == "dock_auto" || cmd == "undock")
        {
            g_dock_pick.erase(sid);
            mlog(sid, STR("Action: ") + std::wstring(cmd.begin(), cmd.end()));   // user button; the stop before it only halts the loop
            // bench ship: dock/unload/refuel button runs the same trip/hold states as the loop (status, log, wait + dock time limit), then parks
            UObject* bship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (cmd != "undock" && alive(bship) && g_mine.count(sid))
            {
                MineSt& bm = g_mine[sid];
                bm.run = false; bm.leave = true; bm.fin = false; bm.btn = true;
                bm.unl_prev = -1; bm.unl_undock = false;
                trip_begin(bship, sid, bm, STR("button"), cmd == "dock_auto" ? "" : cmd);
                return STR("ok");
            }
            return job_start(sid, cmd, seq);
        }
        // 1b. Start loop / Stop loop (one bench ship)
        if (cmd == "mine_start" || cmd == "mine_stop") return mine_cmd(sid, cmd == "mine_start");
        // 2. laser and tractor on/off
        const bool is_laser = cmd.rfind("laser_", 0) == 0;
        const bool is_tractor = cmd.rfind("tractor_", 0) == 0;
        if (is_laser || is_tractor)
        {
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(ship)) return STR("error: ship not found");
            const bool on = cmd.size() >= 2 && cmd.compare(cmd.size() - 2, 2, "on") == 0;
            const std::wstring sub = is_laser ? std::wstring(STR("MiningLaser")) : std::wstring(STR("ItemScooper"));
            const int group = is_laser ? 1 : 2;   // laser = primary group, scooper = secondary (manual run log)
            int found = 0;
            const int ok = set_all_firing(ship, on, found, sub);
            // laser ON goes only to the MiningLaser weapon: the primary fire group also holds the cannon on these ships. OFF still clears the group.
            const bool gi = (is_laser && on) ? false : fire_input(ship, on, group);
            tlog(widen(cmd) + STR(" found=") + std::to_wstring(found) + STR(" ok=") + std::to_wstring(ok) +
                 STR(" group_input=") + (gi ? STR("1") : STR("0")));
            if (found == 0) return STR("error: no ") + sub + STR(" on ship");
            return ok > 0 ? std::wstring(STR("ok")) : std::wstring(STR("error: SetFiring/bFiring write failed"));
        }
        // 6. stop everything for this ship
        if (cmd == "stop_all")
        {
            mine_halt(sid);
            net_leave_slot(sid);             // queue place and dock reservation go (mb_net.hpp)
            g_unl_ores.erase(sid);
            g_dock_pick.erase(sid);     // a Dock picker choice is for the next job only
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(ship)) return STR("error: ship not found");
            int f1 = 0, f2 = 0;
            const int o1 = set_all_firing(ship, false, f1, STR("MiningLaser"));
            const int o2 = set_all_firing(ship, false, f2, STR("ItemScooper"));
            int f3 = 0;
            set_all_firing(ship, false, f3, STR("*"));   // every weapon incl. the cannon
            const bool gi = fire_input(ship, false, 0);
            for (size_t i = 0; i < g_jobs.size();)
            {
                if (g_jobs[i].sid != sid) { ++i; continue; }
                tlog(STR("dock job seq=") + std::to_wstring(g_jobs[i].seq) + STR(" cancelled by stop_all"));
                if (g_jobs[i].cmd != "undock") chunk_hold_target_clear(sid);   // same rule as job_finish: a far trip's cube target ends with the job
                g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(i));
            }
            g_ob.erase(sid); g_pf.erase(sid);
            if (mv_active(sid))
            {
                const MoveJob& mv = g_mvs[sid];
                tlog(STR("move job seq=") + std::to_wstring(mv.seq) + STR(" ") + widen(mv.kind) + STR(" cancelled by stop_all"));
                mv_cancel(sid);   // stops the aim offset too (cid 0 = none)
            }
            mine_status_x_write();   // job column clears now
            std::wstring err;
            const bool ap = set_autopilot(sid, false, err);
            zero_all(ship);
            tlog(STR("stop_all lasers=") + std::to_wstring(f1 + f2) + STR(" ok=") + std::to_wstring(o1 + o2) +
                 STR(" group_input_off=") + (gi ? STR("1") : STR("0")) + STR(" autopilot_off=") + (ap ? STR("1") : STR("0")));
            return ap ? std::wstring(STR("ok")) : STR("error: ") + err;
        }
        // 7. Operation tab "Warp to belt": the ship's belt sector (refused while the loop runs)
        if (cmd == "collect_ore") return sweep_cmd(sid);   // collect ore action: MS_SWEEP (mb_mining.hpp)
        if (cmd == "warp_belt") return warp_belt_cmd(sid);
        // 7b. probe avoidance: probe_avoid[:<ship>] dump, probe_avoid_watch:<ship>:<seconds> sampler, probe_collision* (mb_devcmd.hpp)
        if (cmd.rfind("probe_avoid", 0) == 0 || cmd.rfind("probe_collision", 0) == 0) return probe_avoid_cmd(sid, cmd);
        // 8. dev commands (Dev Menu / Test tab only): mb_devcmd.hpp
        bool dev_handled = false;
        const std::wstring dev = dev_cmd(seq, sid, cmd, dev_handled);
        if (dev_handled) return dev;
        // 9. unknown command
        return STR("error: unknown cmd '") + widen(cmd) + STR("'");
    }

    // WHAT: read the first line of MiningCmd.txt ("<seq>|<ship>|<cmd>"). Runs the command when seq is above the last executed seq.
    //       Malformed lines are logged once. Writes the status file after every command.
    // WHY: the Lua side writes a new line with a higher seq for each button press; the seq check stops a command from running twice.
    void cmd_poll()
    {
        std::string line;
        if (!read_cmd_line(line) || line.empty()) return;
        int seq = 0, sid = 0;
        std::string cmd;
        if (!parse_cmd_line(line, seq, sid, cmd))
        {
            if (line != g_cmd.bad_line) { g_cmd.bad_line = line; tlog(STR("malformed line ignored: '") + widen(line) + STR("'")); }
            return;
        }
        if (!cmd_seq_is_new(seq, g_cmd.seq)) return;   // WHY ==: a "<=" test silently dropped commands after the 9-digit seq wrapped (every ~11.5 days)
        g_cmd.seq = seq;
        g_cmd.ship = sid;
        g_cmd.last_cmd = cmd;
        g_cmd.order_id = 0;
        g_cmd.result = STR("waiting");
        tlog(STR("seq=") + std::to_wstring(seq) + STR(" ship=") + std::to_wstring(sid) + STR(" cmd=") + widen(cmd));
        std::wstring res;
        try { res = run_cmd(seq, sid, cmd); }
        catch (...) { res = STR("error: exception in command"); }
        g_cmd.result = res;
        tlog(STR("seq=") + std::to_wstring(seq) + STR(" result: ") + res);
    }

    // WHAT: at load, remember the seq already in MiningCmd.txt (without running it).
    // WHY: so a restart of the game does not replay the last command from the file.
    void cmd_init()
    {
        std::string line, cmd;
        int seq = 0, sid = 0;
        if (read_cmd_line(line) && parse_cmd_line(line, seq, sid, cmd)) g_cmd.seq = seq;
        tlog(STR("poller ready, last seq=") + std::to_wstring(g_cmd.seq));
        // command capabilities for the Lua side: one name per line. The emergency stop uses stop_all_ships only when it is listed (else the old per-ship sequence).
        try { write_atomic(get_dll_dir() / "MiningCaps.txt", "stop_all_ships\n"); } catch (...) {}
    }

    // WHAT: registry inputs of this tick: mining ships (position from MineSt::anchor, system from sys_id every 2 s per ship) and their rocks.
    // WHY: the registry (mb_registry.hpp) is fed here because g_mine is declared after it.
    void registry_frame(double dt)
    {
        static std::map<int, std::pair<ULONGLONG, int>> sys_cache;   // sid -> (GetTickCount64 of the read, sys)
        std::vector<RegShip> ships;
        std::vector<int> targets;
        const ULONGLONG now = GetTickCount64();
        for (const auto& kv : g_mine)
        {
            if (kv.second.st == MS_OFF) continue;
            auto& c = sys_cache[kv.first];
            if (c.first == 0 || now - c.first >= REG_SYS_MS) { UObject* sh = find_by_id(STR("DMSPlayerShipObject"), kv.first); c = {now, sh ? sys_id(sh) : 0}; }
            if (c.second <= 0) continue;
            RegShip r;
            r.sid = kv.first;
            r.sys = c.second;
            for (int k = 0; k < 3; ++k) r.p[k] = kv.second.anchor[k] / MV_UU_PER_M;
            ships.push_back(r);
            if (kv.second.rock > 0) targets.push_back(kv.second.rock);
        }
        for (const auto& kv : g_mvs) if (kv.second.rock_id > 0) targets.push_back(kv.second.rock_id);
        // approach segments: ship -> target rock centre (radius reads first, see reg_radius_stage)
        for (RegShip& r : ships)
        {
            auto mv = g_mvs.find(r.sid);
            if (mv == g_mvs.end() || mv->second.kind != "approach_rock" || mv->second.rock_id <= 0) continue;
            const RegRock* tr = reg_st().tab.get(mv->second.rock_id);
            if (!tr) continue;
            r.has_seg = true;
            std::copy(tr->c, tr->c + 3, r.q);
        }
        registry_tick(dt, ships, targets);
    }

    // WHAT: engine tick driver for the Test tab. Steps: 1 bump the actor_ok() frame (set rebuilt once per tick).
    //       2 move job, every tick. 3 accumulate the poll timer. 4 every CMD_POLL_S (0.25 s): thr readback,
    //       speed monitor, command poll, dock/undock jobs.
    // WHY: work is spread over different periods so no single tick does everything (perf lessons: heavy reads caused freezes).
    void cmd_tick(double dt)
    {
        // WHAT: heartbeat for the freeze watchdog (mb_watchdog.hpp): one beat per game-thread frame, plus this thread's id.
        g_heartbeat.fetch_add(1, std::memory_order_relaxed);
        g_game_tid.store(static_cast<std::uint32_t>(GetCurrentThreadId()), std::memory_order_relaxed);
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        // 1. every line sets g_last_step first: a fault line (compat_run) names the part that ran
        set_last_step(STR("rock_drain_dead"));
        rock_drain_dead();   // deleted actors queued by the engine listener are removed every tick, also while no ship asks for rocks
        ++g_actor_frame;   // invalidates the actor_ok() set: it is rebuilt at most once per engine tick
        path_tick_begin();   // path check: one periodic check per tick (mb_mining.hpp)
        // 2.
        set_last_step(STR("move_tick"));
        move_tick(dt);   // move jobs run every engine tick
        // 3.
        g_cmd_poll_acc += dt;
        // 4.
        if (g_cmd_poll_acc >= CMD_POLL_S)
        {
            const double step = g_cmd_poll_acc;
            g_cmd_poll_acc = 0.0;
            set_last_step(STR("thr_check"));
            thr_check();
            set_last_step(STR("spd_update"));
            spd_update(step);
            set_last_step(STR("pin_hook_ensure"));
            pin_hook_ensure();   // retried until the AddInputCommand hook is armed (MapPin id, mb_pin.hpp)
            set_last_step(STR("cmd_poll"));
            cmd_poll();
            set_last_step(STR("warp_check"));
            warp_check(step);
            set_last_step(STR("warp_real_tick"));
            warp_real_tick(step);
            set_last_step(STR("pin_file_tick"));
            pin_file_tick();
            set_last_step(STR("jobs_tick"));
            jobs_tick(step);
            set_last_step(STR("warp_wait_tick"));
            warp_wait_tick();
            set_last_step(STR("fast_watch_tick"));   // hull/shield samples every 0.25 s for ships near rock (mb_mining.hpp)
            fast_watch_tick();
            set_last_step(STR("probe_avoid_tick"));
            probe_avoid_tick(step);   // MiningProbeAvoid.txt sampler, idle unless a watch runs (mb_devcmd.hpp)
            set_last_step(STR("mining_tick"));   // per-ship labels are set inside (mine ship=N state)
            mining_tick(step);
            set_last_step(STR("registry_frame"));   // rock registry (mb_registry.hpp): one updater per 0.25 s
            registry_frame(step);
            set_last_step(STR("bubble_tick"));   // bubble reflex for approaching / mining ships (mb_bubble.hpp)
            bubble_tick(step);
            set_last_step(STR("station_destroy_tick"));
            station_destroy_tick(step);
        }
        timing_log(STR("cmd_tick"), perf_ms_since(t0), g_rock_actors.size());
    }
}
