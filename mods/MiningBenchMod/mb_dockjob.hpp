// FILE: mods/MiningBenchMod/mb_dockjob.hpp
// WHAT: dock and undock jobs (one job per ship, advanced by the command poller): job_start, jobs_tick (stages),
//       undock_tick (leave-dock watch), skip_to (SkipToOrder), job_finish, pnum_raw_text (log helper), g_undock_state.
//       dock_mining/dock_refuel now clear orders and build the refuel/unload list via ob_*/pf_* (stage 3); undock still uses the old reuse path.
//       A dock occupied by another ship (or heading there) gives "waiting dock"; the caller retries (mb_mining.hpp).
// WHY separate: the job flow is its own state machine. It is kept apart from move steering (mb_steer.hpp) and from the command parser.
// DEPENDS ON: mb_common.hpp, mb_config.hpp (Config, eff_cfg, g_raw_per_m), mb_ue.hpp (find_by_id, get_int_prop, get_obj_prop, ...),
//             mb_math.hpp (dist3), mb_orders.hpp (send_update_order_dock, send_new_deliver_order, dock_leg_start, order_by_id,
//             resolve_cmd_dock, set_autopilot, dist_target_m), mb_cmdstate.hpp (DockJob, g_jobs, g_cmd, CMD_* timing),
//             mb_net.hpp (g_net_docks, net_pick, net_drop_wait, net_unreserve; the dock picks of unload/refuel/undock),
//             mb_steer.hpp (zero_all, g_spd, MV_UNDOCK_*, MV_SKIP_*).
// USED BY: mb_testcmd.hpp (run_cmd -> job_start, stop_all erases g_jobs, cmd_tick -> jobs_tick),
//          dllmain.cpp (include only).
// GAME FACTS (fact = seen in game; ASSUMPTION = taken from code/notes; UNVERIFIED = not confirmed):
//   - fact: docking works with the EXISTING DeliverOrder: SetShipAutopilot(ship, true) + SkipToOrder(ship, 0) on that order
//     (the player's own flow, captured in Desktop/order_flow.txt, brief Updates 24 and 26).
//   - fact: a NEW DeliverOrder created by the mod (NewShipOrderInput + UpdateShipOrderInput) did NOT move the ship (brief Update 24).
//   - fact: undock = autopilot to a second configured dock (brief Update 26, works in game).
//   - ASSUMPTION: Dock object null or DockID 0 = the ship is not docked anymore; AutopilotExitDockTimer 0 = exit finished
//     (both read in undock_tick, meaning from code, not from the game docs).
//   - UNVERIFIED: why the autopilot leg needs SkipToOrder ~1 s after SetShipAutopilot (ported as is).
//   - Ship props read here: Dock, DockID, TargetDockID, OrderIDs, Orders, CurrentOrderIndex, bAutopilot, bDockingAutopilot,
//     AutopilotExitDockTimer, UndockingAnimationTimer, UndockingVector, Throttle.

#pragma once

namespace
{
    void mine_status_x_write();   // mb_mining.hpp (same anonymous namespace)
    bool trip_warp_try(UObject* ship, int sid, int dock);   // mb_mining.hpp: warp a far dock trip (true = teleport sent)
    double trip_dock_dist_m(UObject* ship, int dock);       // mb_mining.hpp: ship to dock distance in m (-1 = unknown)
    void chunk_hold_target_clear(int sid);                  // mb_chunkhold.hpp: back to the ship's own chunk

    // WHAT: undock state text for the status file (undock_state=): "n/a", "docked", "undocking t=..", "free", "error".
    // WHY: the Test tab shows where the undock job is without reading the job list.
    std::wstring g_undock_state = STR("n/a");

    // WHAT: end job idx: remove it from g_jobs, log the result, and set the command result when this job is still the last command.
    // WHY: one exit path for dock and undock jobs. The seq check keeps an old job from overwriting a newer command's result.
    std::wstring job_start_run(int sid, const std::string& cmd0, int seq);   // defined below (job_start body)

    void job_finish(size_t idx, const std::wstring& res)
    {
        const DockJob j = g_jobs[idx];
        g_job_res[j.sid] = res;
        g_jobs.erase(g_jobs.begin() + static_cast<std::ptrdiff_t>(idx));
        if (j.cmd != "undock") chunk_hold_target_clear(j.sid);   // a far trip warp set the cube on the dock: the trip is over
        mine_status_x_write();   // job column clears now, not at the next second
        tlog(STR("seq=") + std::to_wstring(j.seq) + STR(" ") + widen(j.cmd) + STR(" ship=") + std::to_wstring(j.sid) + STR(" result: ") + res);
        if (g_cmd.seq == j.seq) g_cmd.result = res;
        // the undock was the first step of a dock trip: the dock job starts now, so the trip sees one job running (no gap with a result)
        const auto pend = g_dock_after_undock.find(j.sid);
        if (j.cmd == "undock" && pend != g_dock_after_undock.end())
        {
            const std::string next = pend->second;
            g_dock_after_undock.erase(pend);
            if (!ok_res(res)) return;
            const std::wstring r = job_start_run(j.sid, next, j.seq);
            if (r == STR("waiting")) g_job_res.erase(j.sid);   // dock job running: its result comes later
            else g_job_res[j.sid] = r;
        }
    }

    // WHAT: call DMSPlayerController.SkipToOrder(ShipID, NewIndex = idx). Always sent (no CurrentOrderIndex check).
    //       Logs CurrentOrderIndex before and after the call. false with err text when the function or its params are missing.
    // WHY: the player's flow starts a dock leg with SkipToOrder on the existing order (brief Update 24). The log shows whether the index changed.
    bool skip_to(UObject* ship, int idx, std::wstring& err)
    {
        if (!alive(ship)) { err = STR("SkipToOrder missing"); return false; }
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        int before = -1, after = -1;
        if (!ms_err(g_ms.skip_to(obj_id(ship), idx, &before, &after, e, 256), e, err)) return false;
        tlog(STR("SkipToOrder NewIndex=") + std::to_wstring(idx) + STR(" CurrentOrderIndex before=") + std::to_wstring(before) +
             STR(" after=") + std::to_wstring(after));
        return true;
    }

    // WHAT: undock watch for an "undock" job (job i). Logs the dock/autopilot props every poll step.
    //       Steps: 1 read Dock, DockID, AutopilotExitDockTimer, position. 2 "free" = no dock object or DockID 0.
    //       3 free + exit timer 0 + moved more than MV_UNDOCK_CLEAR_M (50 m) from the job start: autopilot off, zero_all, job ok.
    //       4 timeout MV_UNDOCK_TIMEOUT_S (120 s): job error. Returns true when the job ended (erased).
    // WHY: the autopilot does the leave-dock leg; once the ship is clear, this job switches the autopilot off (what happens next is
    //      not decided here). The 50 m value is a chosen threshold: why 50 m is unknown (ported as is).
    bool undock_tick(size_t i, UObject* ship)
    {
        const DockJob j = g_jobs[i];
        UObject* dock_obj = get_obj_prop(ship, STR("Dock"));
        int32_t did = -1, exit_t = -1;
        get_int_prop(ship, STR("DockID"), did);
        get_int_prop(ship, STR("AutopilotExitDockTimer"), exit_t);
        double me[3] = {0, 0, 0};
        get_loc(ship, me);
        const double moved = dist3(me, j.p0);
        const bool free_now = !alive(dock_obj) || did == 0;
        g_undock_state = STR("undocking t=") + std::to_wstring(static_cast<int>(j.t));
        tlog(STR("undock watch t=") + std::to_wstring(j.t) + STR(" Dock=") + (alive(dock_obj) ? std::to_wstring(obj_id(dock_obj)) : std::wstring(STR("null"))) +
             STR(" DockID=") + std::to_wstring(did) + STR(" AutopilotExitDockTimer=") + std::to_wstring(exit_t) +
             STR(" UndockingAnimationTimer=") + prop_int(ship, STR("UndockingAnimationTimer")) + STR(" UndockingVector=") + pvec_text(ship, STR("UndockingVector")) +
             STR(" bDockingAutopilot=") + prop_bool(ship, STR("bDockingAutopilot")) + STR(" Throttle=") + prop_int(ship, STR("Throttle")) +
             STR(" moved_m=") + std::to_wstring(moved / g_raw_per_m) + STR(" speed_m_s=") + std::to_wstring(spd_of(j.sid).v / g_raw_per_m));
        // 3. clear of the dock: autopilot off, stop the drive, job ok
        if (free_now && exit_t == 0 && moved > MV_UNDOCK_CLEAR_M * g_raw_per_m)
        {
            std::wstring err;
            const bool ap = set_autopilot(j.sid, false, err);
            zero_all(ship);
            g_undock_state = STR("free");
            tlog(STR("undock clear, autopilot_off=") + std::wstring(ap ? STR("1") : STR("0")));
            job_finish(i, STR("ok undocked"));
            return true;
        }
        // 4. timeout
        if (j.t >= MV_UNDOCK_TIMEOUT_S)
        {
            g_undock_state = STR("error");
            job_finish(i, STR("error: undock timeout"));
            return true;
        }
        return false;
    }

    // WHAT: cached Item lookup by resource text (find_item does FindAllOf, so callers must not call it per frame). Item data assets live for the whole session; re-found when not alive.
    UObject* item_c(const std::string& res, std::wstring& why)
    {
        static std::map<std::string, UObject*> cache;
        auto it = cache.find(res);
        if (it != cache.end() && alive(it->second)) return it->second;
        UObject* o = find_item(res, why);
        if (o) cache[res] = o; else cache.erase(res);
        return o;
    }

    // WHAT: AUTO RULE. "dock_refuel" when any ENABLED fuel has ship cargo below its threshold, else "dock_mining". why = reason text.
    // WHY: one decision point for the Test button and the mining loop. Unreadable fuel item or cargo (-1) is skipped.
    std::string auto_rule(int sid, UObject* ship, std::wstring& why)
    {
        const Config c = eff_cfg(sid);
        for (int i = 0; i < N_FUEL; ++i)
        {
            if (!c.fuel[i].on) continue;
            std::wstring w;
            UObject* it = item_c(FUEL_ITEM[i], w);
            if (!alive(it)) continue;
            const int64_t have = inv_total(ship, it);
            if (fuel_low(c.fuel[i], have))
            {
                why = widen(FUEL_KEY[i]) + STR(" ") + i64w(have) + STR(" < ") + std::to_wstring(c.fuel[i].threshold);
                return "dock_refuel";
            }
        }
        why = STR("fuel ok");
        return "dock_mining";
    }

    // WHAT: true when another ship sits in dock dk or has a dock job heading there. WHY: never send a ship to an occupied dock.
    bool dock_busy(int dk, int my_sid)
    {
        for (const DockJob& o : g_jobs)
            if (o.sid != my_sid && o.cmd != "undock" && o.dock == dk) return true;
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* s : ships)
        {
            if (!alive(s) || obj_id(s) == my_sid) continue;
            int32_t did = 0;
            get_int_prop(s, STR("DockID"), did);
            UObject* d = get_obj_prop(s, STR("Dock"));
            if (did == dk || (alive(d) && obj_id(d) == dk)) return true;
        }
        return false;
    }

    // WHAT: unload dock for a dock_mining trip. fixed > 0 = the Dock picker's dock; else the first free dock in list order of the ores IN CARGO that have
    //       an unload dock (unload_cand). ores = the ores unloaded at the chosen dock (ORE indices).
    //       Returns 0 with busy=false when no ore has a dock list, or none in cargo has one (caller: "no unload dock set"); busy=true when the dock is taken or every cargo ore's docks are busy.
    // WHY: one trip unloads at one dock (ob_plan_unload takes one dock). An ore not in cargo must not pick the dock: the ship docks, unloads nothing, the cargo check fails.
    //      The Dock picker path is as before: cargo ores first, then the other ores that list the dock.
    NetVerdict unload_pick(int sid, UObject* ship, const Config& c, int fixed, std::vector<int>& ores, std::string& subject)
    {
        ores.clear();
        std::vector<bool> in_cargo(N_ORE, false), has_docks(N_ORE, false);
        for (int i = 0; i < N_ORE; ++i)
        {
            has_docks[i] = !c.ore_docks[i].empty();
            std::wstring w;
            UObject* it = item_c(ORE_ITEM[i], w);
            in_cargo[i] = alive(it) && inv_total(ship, it) > 0;
        }
        std::vector<int> cands;
        if (fixed > 0) cands = {fixed};
        else for (int i : unload_cand(in_cargo, has_docks)) for (int d : c.ore_docks[i]) add_unique(cands, d);
        if (cands.empty())
        {
            // nothing to unload here: a cargo ore with no dock list is a real error (NOT_CONFIGURED), else the cargo has nothing to unload
            NetVerdict v;
            v.k = NetVerdict::ERR; v.reason = NR_NOTHING_TO_UNLOAD;
            for (int i = 0; i < N_ORE; ++i) if (in_cargo[i] && !has_docks[i]) { v.reason = NR_NOT_CONFIGURED; subject = ORE_KEY[i]; break; }
            return v;
        }
        const Purpose pur = g_flee_pick.count(sid) ? P_FLEE : P_UNLOAD;
        const NetVerdict v = net_pick(g_net_docks, g_net_ships, g_net_ticket, sid, pur, net_req_of(sid, pur), cands, 0, g_now);
        if (v.k == NetVerdict::ERR) { subject = net_dock_label(cands.front()); return v; }
        // ores unloaded at the chosen dock: cargo ores first, then (picker dock only) the other ores that list it
        std::vector<int> pool;
        for (int i = 0; i < N_ORE; ++i) if (has_docks[i] && in_cargo[i]) pool.push_back(i);
        if (fixed > 0) for (int i = 0; i < N_ORE; ++i) if (has_docks[i] && !in_cargo[i]) pool.push_back(i);
        for (int i : pool)
            if (std::find(c.ore_docks[i].begin(), c.ore_docks[i].end(), v.dock) != c.ore_docks[i].end()) ores.push_back(i);
        return v;
    }

    // WHAT: text of a net refusal for job_start: real errors get the "error: " prefix; NOTHING_TO_UNLOAD returns "nothing to unload" (no error).
    std::wstring net_refusal(const NetVerdict& v, const std::string& subject)
    {
        const std::wstring t = widen(net_ship_text(v.reason, subject, std::string(), 0.0, 0, 0));
        return v.reason == NR_NOTHING_TO_UNLOAD ? STR("nothing to unload") : STR("error: ") + t;
    }

    // WHAT: a job_start result that is a real dock error (NOT_CONFIGURED / DOCK_GONE): red ERROR in the trip, not a plain failure.
    bool net_real_error(const std::wstring& r)
    {
        return r.find(STR("No unload dock configured")) != std::wstring::npos || r.find(STR("no longer exists")) != std::wstring::npos;
    }

    // WHAT: true when a dock job runs for ship sid.
    bool job_running(int sid)
    {
        for (const DockJob& j : g_jobs) if (j.sid == sid) return true;
        return false;
    }

    // WHAT: the ship sits at a dock other than this job's target: the pick is dropped, the undock starts now, and the dock command runs when the undock ends ok (job_finish).
    // WHAT: returns "" when no undock is needed (the caller goes on), else the result of the undock start ("waiting" = started).
    // WHY: a dock order from a dock does not move the ship (live log 16:03: stuck retries, Stop All + Undock was the only fix).
    std::wstring undock_first(int sid, UObject* ship, int target, const std::string& cmd, int seq)
    {
        const int at = net_docked_at(sid, ship);
        if (!dock_undock_first(at, target)) return STR("");
        net_release_ship(g_net_docks, g_net_ships, sid);   // the pick's reservation and queue place go
        tlog(STR("undock first: ship ") + std::to_wstring(sid) + STR(" docked at ") + std::to_wstring(at) + STR(", target ") + std::to_wstring(target));
        g_dock_after_undock[sid] = cmd;
        const std::wstring r = job_start_run(sid, "undock", seq);
        if (r == STR("waiting")) return r;
        g_dock_after_undock.erase(sid);
        if (r == STR("ok already free")) return job_start_run(sid, cmd, seq);   // not docked after all: the dock command runs now
        return r;
    }

    // WHAT: the order part of a dock job start (job_start_run step 5b-7, also run by jobs_tick stage 4 after a far trip warp).
    //       4 order list readable; 5 job base = order count now (growth = new order), start log line.
    //       5b dock_mining / dock_refuel: clear the orders and build the refuel or unload list (stage 3 waits for it).
    //       6 reuse the ship's existing DeliverOrder (point it at the dock, stage 1). 7 otherwise a new DeliverOrder (stage 0).
    //       undock: step 6 or 7 too (it leaves the dock by a dock order to another dock).
    // WHAT: returns "" when started (j.stage set; the caller stores j), else the error text.
    // WHY: the far trip warp runs before this, so no dock order is active during the teleport. One unit, so both paths share it.
    std::wstring dock_order_issue(DockJob& j, UObject* ship)
    {
        const int sid = j.sid, dk = j.dock;
        const std::string cmd = j.cmd;
        TArray<int32_t>* ids = int_array(ship, STR("OrderIDs"));
        if (!ids) return STR("error: OrderIDs unreadable");
        j.base = ids->Num();
        tlog(STR("start ") + widen(cmd) + STR(" ship=") + std::to_wstring(sid) + STR(" dock=") + std::to_wstring(dk) +
             STR(" base_orders=") + std::to_wstring(j.base));
        if (cmd != "undock")
        {
            const Config c = eff_cfg(sid);
            const bool refuel = cmd == "dock_refuel";
            std::vector<OBStep> st;
            if (refuel)
            {
                std::vector<std::pair<std::string, int>> fuels;
                for (int i = 0; i < N_FUEL; ++i) if (c.fuel[i].on) fuels.push_back({FUEL_ITEM[i], c.fuel[i].amount});
                if (fuels.empty()) return STR("error: no fuel enabled");
                std::wstring ferr;
                if (!pf_start(sid, fuels, ferr)) return STR("error: ") + ferr;
                st = ob_plan_refuel(dk, fuels, c.wait_seconds);
            }
            else
            {
                g_unl_ores[sid] = j.unl;
                std::vector<std::string> ores;
                for (int i : j.unl) ores.push_back(ORE_ITEM[i]);
                st = ob_plan_unload(dk, ores, c.wait_seconds);
            }
            ob_start(sid, st);
            j.stage = 3;
            return STR("");
        }
        // 6. reuse the ship's existing DeliverOrder (player flow: SetShipAutopilot + SkipToOrder on the existing order)
        FProperty* op = ship->GetPropertyByNameInChain(STR("Orders"));
        auto* orders = (op && CastField<FArrayProperty>(op)) ? ship->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Orders")) : nullptr;
        int reuse_idx = -1;
        for (int32_t i = 0; orders && i < orders->Num(); ++i)
        {
            UObject* o = (*orders)[i];
            if (alive(o) && std::wstring(o->GetClassPrivate()->GetName()).find(STR("DeliverOrder")) != std::wstring::npos) { reuse_idx = i; break; }
        }
        if (reuse_idx >= 0)
        {
            j.new_order = obj_id((*orders)[reuse_idx]);
            j.new_index = reuse_idx;
            j.stage = 1;
            std::wstring err;
            if (!send_update_order_dock(j.new_order, dk, err)) return STR("error: ") + err;
            tlog(STR("reuse order id=") + std::to_wstring(j.new_order) + STR(" index=") + std::to_wstring(j.new_index) + STR(" DockID=") + std::to_wstring(dk));
            if (sid == g_cmd.ship) g_cmd.order_id = j.new_order;
            return STR("");
        }
        // 7. no DeliverOrder yet: create one (stage 0 waits for it to appear in OrderIDs)
        std::wstring err;
        if (!send_new_deliver_order(sid, err)) return STR("error: ") + err;
        j.stage = 0;
        return STR("");
    }

    // WHAT: start a dock_mining, dock_refuel or undock job for ship sid. Returns "waiting" when started, else the error text.
    //       dock_auto picks dock_refuel or dock_mining with auto_rule.
    //       Steps: 1 refuse if a dock job already runs for this ship. 2 find the ship. 3 pick the target dock:
    //       undock = first free refuel candidate, then first free unload dock, never the current dock;
    //       dock_refuel = first free refuel candidate; dock_mining = unload_pick (ores and dock). Dock picker overrides apply. 4 OrderIDs must be readable.
    //       5 record the job (base order count, start position). 6 reuse the ship's existing DeliverOrder if there is one:
    //       point it at the dock (send_update_order_dock) and go to stage 1. 7 otherwise create a new DeliverOrder (stage 0).
    // WHY: reusing the existing DeliverOrder is the flow that worked in game (brief Update 24, 25): the new-order path did not move the ship.
    //      The new-order path is kept for the case with no DeliverOrder. undock reuses the same path and simply targets another dock.
    // WHAT: job_start body (see above). job_start wraps it to publish the job column at once.
    std::wstring job_start_run(int sid, const std::string& cmd0, int seq)
    {
        // 1. one dock job per ship
        for (const DockJob& j : g_jobs) if (j.sid == sid) return STR("error: dock job already running for this ship");
        // 2. ship
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        std::string cmd = cmd0;
        if (cmd == "dock_auto") { std::wstring aw; cmd = auto_rule(sid, ship, aw); tlog(STR("auto rule -> ") + widen(cmd) + STR(" (") + aw + STR(")")); }
        std::wstring why;
        int dk = 0;
        std::vector<int> unl;   // dock_mining: ore indices unloaded at dk (used in 5b)
        // 3. target dock
        if (cmd == "undock")
        {
            // leave the current dock: second dock = first free refuel candidate, then first free unload dock of any ore; never the current dock
            UObject* cur = get_obj_prop(ship, STR("Dock"));
            if (!alive(cur)) { g_undock_state = STR("free"); return STR("ok already free"); }
            int32_t cur_id = 0;
            if (!get_int_prop(ship, STR("DockID"), cur_id) || cur_id <= 0) cur_id = obj_id(cur);
            const Config c = eff_cfg(sid);
            std::vector<int> cands = refuel_candidates(c, why);
            for (int i = 0; i < N_ORE; ++i) for (int d : c.ore_docks[i]) add_unique(cands, d);
            cands.erase(std::remove(cands.begin(), cands.end(), cur_id), cands.end());
            if (cands.empty()) return STR("error: need a second configured dock to leave");
            // an undock never waits: when every candidate is taken, take the first one (old behaviour); it is not a hold
            const NetVerdict v = net_pick(g_net_docks, g_net_ships, g_net_ticket, sid, P_UNDOCK_TARGET, net_req_of(sid, P_UNDOCK_TARGET), cands, 0, g_now);
            if (v.k == NetVerdict::ERR) return net_refusal(v, net_dock_label(cands.front()));
            if (v.k == NetVerdict::WAIT) net_drop_wait(g_net_docks, g_net_ships[sid]);
            dk = v.dock;
            g_undock_state = STR("docked");
        }
        else if (cmd == "dock_refuel")
        {
            // first free candidate: the Dock picker's dock, else refuel_dock_id / refuel_dock_name, then refuel_docks
            auto pk = g_dock_pick.find(sid);
            const Config c = eff_cfg(sid);
            const std::vector<int> cands = (pk != g_dock_pick.end() && pk->second > 0) ? std::vector<int>{pk->second} : refuel_candidates(c, why);
            if (cands.empty()) return why == STR("no dock configured") ? why : STR("error: ") + why;
            const NetVerdict v = net_pick(g_net_docks, g_net_ships, g_net_ticket, sid, P_REFUEL, net_req_of(sid, P_REFUEL), cands, 0, g_now);
            if (v.k == NetVerdict::ERR) return net_refusal(v, net_dock_label(cands.front()));
            { const std::wstring u = undock_first(sid, ship, v.dock, cmd, seq); if (!u.empty()) return u; }
            if (v.k == NetVerdict::WAIT) { tlog(STR("refuel docks busy, ship ") + std::to_wstring(sid) + STR(" waits")); return STR("waiting dock"); }
            dk = v.dock;
        }
        else
        {
            // unload: Dock picker's dock, else the first free unload dock (see unload_pick); the ores are set in 5b
            auto pk = g_dock_pick.find(sid);
            const Config c = eff_cfg(sid);
            const int fixed = (pk != g_dock_pick.end() && pk->second > 0) ? pk->second : 0;
            std::string subject;
            const NetVerdict v = unload_pick(sid, ship, c, fixed, unl, subject);
            if (v.k == NetVerdict::ERR) return net_refusal(v, subject);
            { const std::wstring u = undock_first(sid, ship, v.dock, cmd, seq); if (!u.empty()) return u; }
            if (v.k == NetVerdict::WAIT) { tlog(STR("unload dock busy, ship ") + std::to_wstring(sid) + STR(" waits")); return STR("waiting dock"); }
            dk = v.dock;
            if (unl.empty()) return STR("error: no ore lists this dock");
        }
        // 4b. checks dock_order_issue would fail on: a job that would fail must not warp
        if (!int_array(ship, STR("OrderIDs"))) return STR("error: OrderIDs unreadable");
        if (cmd == "dock_refuel")
        {
            const Config c = eff_cfg(sid);
            bool any_fuel = false;
            for (int i = 0; i < N_FUEL; ++i) if (c.fuel[i].on) any_fuel = true;
            if (!any_fuel) return STR("error: no fuel enabled");
        }
        // 5. job record: start position (undock clear check)
        DockJob j;
        j.sid = sid; j.dock = dk; j.seq = seq; j.cmd = cmd; j.kind = cmd0; j.unl = unl;
        get_loc(ship, j.p0);
        // 5a. far dock trip (not undock): the warp comes first, the ship is undocked here. The dock order is issued after the settle (jobs_tick stage 4)
        if (cmd != "undock")
        {
            if (trip_warp_try(ship, sid, dk)) { j.stage = 4; g_jobs.push_back(j); return STR("waiting"); }
        }
        // 5b. the order part (steps 4-7 in dock_order_issue)
        const std::wstring oe = dock_order_issue(j, ship);
        if (!oe.empty()) return oe;
        g_jobs.push_back(j);
        return STR("waiting");
    }

    // WHAT: start a dock/undock job and publish MiningStatusX at once (job column). Returns the job_start_run result.
    std::wstring job_start(int sid, const std::string& cmd0, int seq)
    {
        const std::wstring r = job_start_run(sid, cmd0, seq);
        // a failed start gives its dock reservation back; a waiting ship keeps its queue place, a running job keeps its dock
        if (r != STR("waiting") && r != STR("waiting dock") && !job_running(sid)) net_unreserve(sid);
        mine_status_x_write();
        return r;
    }

    // WHAT: a PNum property as its raw value text, "n/a" when unreadable.
    // WHY: log lines show raw values next to the metre values, for the raw_per_meter calibration.
    std::wstring pnum_raw_text(UObject* o, const TCHAR* prop)
    {
        int64_t v = 0;
        return pnum_prop(o, prop, v) ? std::to_wstring(v) : std::wstring(STR("n/a"));
    }

    // WHAT: advance every dock/undock job by one poll step (CMD_POLL_S = 0.25 s). The ship is re-resolved by id each step, never kept.
    //       Steps: 1 time and log timers; ship gone -> job error. 2 stage 0 (new-order path only): wait until OrderIDs grows
    //       past base (CMD_ORDER_TIMEOUT_S = 5 s), then UpdateShipOrderInput(dock) for the newest order -> stage 1.
    //       3 stage 1 (apply): read the order's DockID every step; when it equals the target or CMD_DOCK_APPLY_S (3 s) has passed,
    //       dock_leg_start (SetShipAutopilot(1) first) -> stage 2, SkipToOrder due after MV_SKIP_WAIT_S (1 s).
    //       4 stage 2: SkipToOrder(new_index) once when due; remember the position. 5 fallback: 3 s after SkipToOrder, if the ship
    //       moved less than 1 m, SkipToOrder once more. 6 undock job -> undock_tick and stop here.
    //       7 dock job: done when DockID or Dock equals the target ("ok"); a log line every CMD_WATCH_LOG_S (1 s); CMD_WATCH_S (180 s) -> timeout error.
    // WHY: the code waits for the order to show the target dock (stage 1, up to 3 s), then turns the autopilot on and sends SkipToOrder
    //      (brief Updates 23-26). Why the wait is needed is unknown (ported as is). Work is split over poll steps so the engine tick stays short.
    constexpr int DOCK_STALL_MAX = 5;   // stall retries per dock job before it ends with "error: cannot move"

    void jobs_tick(double dt)
    {
        ob_tick(dt);   // order builders (mb_orders.hpp) and filter syncs (mb_filters.hpp) run one send per call
        pf_tick(dt);
        for (size_t i = 0; i < g_jobs.size();)
        {
            DockJob& j = g_jobs[i];
            // 1.
            j.t += dt;
            j.log_acc += dt;
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), j.sid);
            if (!alive(ship)) { job_finish(i, STR("error: ship not found during dock leg")); continue; }
            // 2. stage 0: wait for the new DeliverOrder to show up, then point it at the dock
            if (j.stage == 0)
            {
                TArray<int32_t>* ids = int_array(ship, STR("OrderIDs"));
                if (!ids) { job_finish(i, STR("error: OrderIDs unreadable")); continue; }
                const int n = ids->Num();
                if (n <= j.base)
                {
                    if (j.t >= CMD_ORDER_TIMEOUT_S) { job_finish(i, STR("error: order not created")); continue; }
                    ++i;
                    continue;
                }
                j.new_order = (*ids)[n - 1];
                j.new_index = n - 1;
                tlog(STR("new order id=") + std::to_wstring(j.new_order) + STR(" index=") + std::to_wstring(j.new_index) +
                     STR(" after ") + std::to_wstring(j.t) + STR(" s"));
                if (j.sid == g_cmd.ship) g_cmd.order_id = j.new_order;
                std::wstring err;
                if (!send_update_order_dock(j.new_order, j.dock, err)) { job_finish(i, STR("error: ") + err); continue; }
                tlog(STR("UpdateShipOrderInput sent OrderID=") + std::to_wstring(j.new_order) + STR(" DockID=") + std::to_wstring(j.dock));
                j.stage = 1;
                j.t = 0.0;
                j.log_acc = 0.0;
                ++i;
                continue;
            }
            // 2b. stage 3: wait for the order builder (and, for refuel, the filter sync), then watch the dock leg (stage 2)
            if (j.stage == 3)
            {
                const int a = ob_state(j.sid);
                const int b = j.cmd == "dock_refuel" ? pf_state(j.sid) : 0;   // unload jobs never run the filter sync
                if (a == 3 || b == 3) { job_finish(i, STR("error: ") + (a == 3 ? ob_result(j.sid) : pf_result(j.sid))); continue; }
                if (a == 1 || b == 1)
                {
                    if (j.t >= 90.0) { job_finish(i, STR("error: order build timeout")); continue; }
                    ++i;
                    continue;
                }
                j.new_order = ob_first_deliver(j.sid);
                j.new_index = 0;
                if (j.sid == g_cmd.ship) g_cmd.order_id = j.new_order;
                tlog(STR("orders built, watching dock leg first_order=") + std::to_wstring(j.new_order));
                j.stage = 2;
                j.skip_wait = -1.0;
                j.skip_t = -1.0;
                j.fb_done = true;
                j.t = 0.0;
                j.log_acc = 0.0;
                ++i;
                continue;
            }
            // 3. stage 1: wait until the order shows the target dock (or the window ends), then start the dock leg
            if (j.stage == 1)
            {
                UObject* ord = order_by_id(ship, j.new_order);
                int32_t odk = -1;
                const bool have = alive(ord) && get_int_prop(ord, STR("DockID"), odk);
                const bool applied = have && odk == j.dock;
                tlog(STR("dock apply t=") + std::to_wstring(j.t) + STR(" OrderID=") + std::to_wstring(j.new_order) +
                     STR(" order DockID=") + (have ? std::to_wstring(odk) : std::wstring(STR("n/a"))) +
                     STR(" bHasUpdated=") + prop_bool(ord, STR("bHasUpdated")) + STR(" bCurrent=") + prop_bool(ord, STR("bCurrent")) +
                     STR(" target=") + std::to_wstring(j.dock));
                if (applied || j.t >= CMD_DOCK_APPLY_S)
                {
                    if (applied) tlog(STR("dock applied at t=") + std::to_wstring(j.t));
                    else tlog(STR("dock not applied after ") + std::to_wstring(j.t) + STR(" s, continuing with dock leg"));
                    std::wstring err;
                    if (!dock_leg_start(ship, STR("test cmd: "), err)) { job_finish(i, STR("error: ") + err); continue; }
                    j.stage = 2;
                    j.skip_wait = MV_SKIP_WAIT_S;
                    j.t = 0.0;
                    j.log_acc = 0.0;
                }
                ++i;
                continue;
            }
            // 3b. stage 4: far trip warp sent (trip_warp_try). Wait until the ship reads near the dock (warp_settle_done), then issue the dock order (dock_order_issue)
            if (j.stage == 4)
            {
                if (!warp_settle_done(j.t, trip_dock_dist_m(ship, j.dock))) { ++i; continue; }
                tlog(STR("warp settled after ") + std::to_wstring(j.t) + STR(" s, dock order issued"));
                j.t = 0.0;
                j.log_acc = 0.0;
                const std::wstring oe = dock_order_issue(j, ship);
                if (!oe.empty()) { job_finish(i, oe); continue; }
                ++i;
                continue;
            }
            // stage 2: SkipToOrder MV_SKIP_WAIT_S after start (+ one fallback), then watch (dock_mining: until docked; undock: see undock_tick)
            // 4. the SkipToOrder that starts the leg
            if (j.skip_wait >= 0.0 && j.t >= j.skip_wait)
            {
                std::wstring err;
                if (!skip_to(ship, j.new_index, err)) { job_finish(i, STR("error: ") + err); continue; }
                j.skip_wait = -1.0;
                j.skip_t = j.t;
                get_loc(ship, j.p_skip);
            }
            // 5. fallback: the ship did not move 1 m within MV_SKIP_FB_S of the SkipToOrder -> send it once more
            if (j.skip_t >= 0.0 && !j.fb_done && j.t - j.skip_t >= MV_SKIP_FB_S)
            {
                j.fb_done = true;
                double me[3] = {0, 0, 0};
                get_loc(ship, me);
                if (dist3(me, j.p_skip) < g_raw_per_m)
                {
                    std::wstring err;
                    tlog(STR("not moving 3 s after SkipToOrder, fallback SkipToOrder"));
                    skip_to(ship, j.new_index, err);
                }
            }
            // 6. undock jobs have their own watch
            if (j.cmd == "undock") { if (!undock_tick(i, ship)) ++i; continue; }
            // 7. dock job: target reached? log, timeout
            int32_t dk = 0;
            const bool okd = get_int_prop(ship, STR("DockID"), dk);
            UObject* dock_obj = get_obj_prop(ship, STR("Dock"));
            const bool at_target = (okd && dk == j.dock) || (alive(dock_obj) && obj_id(dock_obj) == j.dock);
            { int64_t draw = 0; if (pnum_prop(ship, STR("DistanceToTarget"), draw) && trip_progress(j.prog_best, static_cast<double>(draw) / g_raw_per_m)) j.prog_t = j.t; }
            if (j.log_acc >= CMD_WATCH_LOG_S)
            {
                j.log_acc = 0.0;
                UObject* ord = order_by_id(ship, j.new_order);
                tlog(STR("watch t=") + std::to_wstring(j.t) + STR(" bAutopilot=") + prop_bool(ship, STR("bAutopilot")) +
                     STR(" bDockingAutopilot=") + prop_bool(ship, STR("bDockingAutopilot")) +
                     STR(" Dock=") + std::to_wstring(alive(dock_obj) ? obj_id(dock_obj) : 0) + STR(" DockID=") + prop_int(ship, STR("DockID")) +
                     STR(" TargetDockID=") + prop_int(ship, STR("TargetDockID")) +
                     STR(" DistanceToTarget_raw=") + pnum_raw_text(ship, STR("DistanceToTarget")) +
                     STR(" DistanceToTarget_m=") + dist_target_m(ship) +
                     STR(" AutopilotImpulse=") + pvec_text(ship, STR("AutopilotImpulse")) +
                     STR(" Throttle=") + prop_int(ship, STR("Throttle")) +
                     STR(" ImpulseInput=") + pvec_text(ship, STR("ImpulseInput")) +
                     STR(" CurrentOrderIndex=") + prop_int(ship, STR("CurrentOrderIndex")) +
                     STR(" order bCurrent=") + prop_bool(ord, STR("bCurrent")) + STR(" bHasUpdated=") + prop_bool(ord, STR("bHasUpdated")) +
                     STR(" order DockID=") + prop_int(ord, STR("DockID")));
            }
            // dock busy (another ship there or heading there) after 30 s of not arriving: end with "waiting dock", the caller retries
            // a dock this ship holds (reserved for it, or docked there) is never busy: ending the job here restarted the same dock trip every 34 s (live log 15:16-15:18)
            if (!at_target && j.t >= 30.0 && dock_busy(j.dock, j.sid) && !net_owns(j.dock, j.sid)) { job_finish(i, STR("waiting dock")); continue; }
            if (at_target) { tlog(STR("docked at t=") + std::to_wstring(j.t)); job_finish(i, STR("ok")); continue; }
            if (j.t - j.prog_t >= CMD_WATCH_S) { job_finish(i, STR("error: dock timeout (no progress for 180 s)")); continue; }
            // stall retry: autopilot on but the ship does not move for 10 s (Miner 3 case: thrust 0) -> autopilot off/on + SkipToOrder again, same job and orders
            if (j.stage == 2 && j.skip_wait < 0.0)
            {
                double me[3] = {0, 0, 0};
                get_loc(ship, me);
                if (j.stall_t < 0.0 || dist3(me, j.p_stall) >= g_raw_per_m) { j.stall_t = 0.0; std::copy(me, me + 3, j.p_stall); }
                else if ((j.stall_t += dt) >= 10.0)
                {
                    if (j.retries >= DOCK_STALL_MAX)   // retries used up: no more silent loop, the trip ends with a red error
                    {
                        log_line(STR("DOCK stuck ship=") + std::to_wstring(j.sid) + STR(": cannot move after ") + std::to_wstring(j.retries) + STR(" retries, job ended"));
                        job_finish(i, STR("error: cannot move: seems stuck (docked?)"));
                        continue;
                    }
                    ++j.retries; j.stall_t = 0.0;
                    if (j.retries >= 2 && net_docked_at(j.sid, ship) > 0)
                    {
                        // docked: a repeated dock order does not move the ship, so undock first; the dock command runs again after the undock (job_finish)
                        const std::string cmd = j.cmd;
                        const int sid = j.sid, seq = j.seq;
                        log_line(STR("DOCK retry ") + std::to_wstring(j.retries) + STR(" ship=") + std::to_wstring(sid) + STR(": still docked, undock first, then the dock job again"));
                        g_dock_after_undock[sid] = cmd;
                        job_finish(i, STR("undock first"));
                        const std::wstring r = job_start_run(sid, "undock", seq);
                        if (r == STR("waiting")) g_job_res.erase(sid);
                        else { g_dock_after_undock.erase(sid); g_job_res[sid] = r; }
                        continue;
                    }
                    std::wstring err;
                    log_line(STR("DOCK retry ") + std::to_wstring(j.retries) + STR(" ship=") + std::to_wstring(j.sid) + STR(": not moving for 10 s, repeat job, autopilot off/on, repeat job"));
                    skip_to(ship, j.new_index, err);                  // 1 repeat the job: back to the dock order
                    set_autopilot(j.sid, false, err);                 // 2 autopilot off, then on
                    if (!dock_leg_start(ship, STR("test cmd: "), err) || !skip_to(ship, j.new_index, err)) tlog(STR("dock retry failed: ") + err);   // 3 repeat the job again
                }
            }
            ++i;
        }
    }
}
