// FILE: mods/MiningBenchMod/mb_orders.hpp
// WHAT: order and autopilot helpers: AddInputCommand, new DeliverOrder input, UpdateShipOrderInput (dock), find an order by id, the dock-leg start, SetShipAutopilot, dock-name resolution, and the distance-to-target readback.
// WHY separate: order sending is one unit, reused by the dock job and the test tab.
// DEPENDS ON: mb_common (alive, log_line), mb_config (Config, g_raw_per_m), mb_ue (first_of, put, obj_id, pnum_prop), mb_docks (find_dock_by_name).
// USED BY: mb_dockjob, mb_steer, mb_testcmd.
// GAME FACTS:
//   VERIFIED (brief Update 24): the dock flow the player uses is SetShipAutopilot(true) plus SkipToOrder(0) on the EXISTING order.
//   VERIFIED (brief Update 24): an order created with NewShipOrderInput alone did not move the ship.
//   VERIFIED (brief probe2 dump): NewShipOrderInput.CreateInputNewShipOrder(InClass) and UpdateShipOrderInput.CreateInputUpdateShipOrder(InTargetID, InItem, InComparison, InAmount) exist.
//   ASSUMPTION: an order id is the SaveableObject.ID of the order object (from the code comment).
//   UNVERIFIED: wait conditions on a DeliverOrder. This file leaves InItem, InComparison and InAmount at zero, so no wait condition is attached (brief Update 20/26: attaching them is still open).
#pragma once

namespace
{
    // ---------------- input commands ----------------

    // WHAT: calls DMSPlayerController.AddInputCommand(InReceiverID, InCommand). false with `err` set when the function or a parameter is missing.
    // WHY: this is the game's player input route for ship commands (the same route the player's UI uses, brief Update 23). The mod never calls the order functions directly.
    bool add_input(int receiver, UObject* cmd, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        int ok = g_ms.add_input(receiver, cmd, e, 256);
        if (!ok) err = e;
        return ok != 0;
    }

    // WHAT: makes a new order of class /Script/MainSequence.<cls_name> with NewShipOrderInput.CreateInputNewShipOrder(InClass) and sends it to receiver (ship id for a DeliverOrder, DeliverOrder id for a child).
    // WHY: one factory call for every order kind. The factory is a function on the class default object (CDO), so no instance is needed. The new id is not returned: the caller reads it back.
    bool send_new_order(int receiver, const std::wstring& cls_name, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        return ms_err(g_ms.new_order(receiver, cls_name.c_str(), e, 256), e, err);
    }

    // WHAT: makes a new DeliverOrder and sends it to the ship (InReceiverID = ship id).
    // WHY: an order created this way alone did not move the ship (brief Update 24), so the dock job also updates and skips to the order.
    bool send_new_deliver_order(int sid, std::wstring& err)
    {
        return send_new_order(sid, STR("DeliverOrder"), err);
    }

    // WHAT: UpdateShipOrderInput.CreateInputUpdateShipOrder(InTargetID, InItem, InComparison, InAmount), sent to order oid (InReceiverID = order id). Dock leg: target = dock, item null. Child: target 0, item + cmp + amount.
    // WHY: one builder for the dock leg and the wait/unload children. Each param is written at its real size, so the 1-byte comparison cannot overrun.
    bool send_update_order(int oid, int target, UObject* item, int cmp, int amount, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        return ms_err(g_ms.update_order(oid, target, item, cmp, amount, e, 256), e, err);
    }

    // WHAT: sets the dock of an order: send_update_order with item null, comparison 0, amount 0 (no wait condition).
    // WHY: this is how the dock leg points a DeliverOrder at the chosen dock.
    bool send_update_order_dock(int oid, int dock, std::wstring& err)
    {
        return send_update_order(oid, dock, nullptr, 0, 0, err);
    }

    // ---------------- order lookup ----------------

    // WHAT: returns the element of ship.Orders whose ID equals oid, or nullptr when there is none.
    // WHY: the Orders array is read by reflection. Matching on the ID (not on the index) survives list changes between the send and the readback.
    UObject* order_by_id(UObject* ship, int oid)
    {
        FProperty* p = alive(ship) ? ship->GetPropertyByNameInChain(STR("Orders")) : nullptr;
        auto* arr = (p && CastField<FArrayProperty>(p)) ? ship->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Orders")) : nullptr;
        if (!arr) return nullptr;
        for (int32_t i = 0; i < arr->Num(); ++i)
        {
            UObject* o = (*arr)[i];
            if (alive(o) && obj_id(o) == oid) return o;
        }
        return nullptr;
    }

    // ---------------- autopilot ----------------

    // WHAT: calls SetShipAutopilot(ship, on) through MSCore. false with `err` set when MSCore or the function is missing.
    // WHY: the native autopilot does the dock legs. It is switched on for a dock leg and off by the cleanup and test-stop paths.
    bool set_autopilot(int sid, bool on, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        return ms_err(g_ms.autopilot(sid, on ? 1 : 0, e, 256), e, err);
    }

    // WHAT: starts the dock leg: SetShipAutopilot(ship, true), then logs the call. The SkipToOrder comes later from the job (jobs_tick stage 2, in mb_dockjob.hpp), after MV_SKIP_WAIT_S.
    // WHY: the player's own dock flow is autopilot on first, then SkipToOrder 0 on the existing order (brief Update 24). The separate function keeps the explicit log line the dock job relies on.
    bool dock_leg_start(UObject* ship, const std::wstring& pfx, std::wstring& err)
    {
        const int sid = obj_id(ship);
        if (!set_autopilot(sid, true, err)) return false;
        log_line(pfx + STR("SetShipAutopilot(1) called for ship ") + std::to_wstring(sid));
        return true;
    }

    // ---------------- dock choice and readback ----------------

    // WHAT: the ship's DistanceToTarget (raw PNum) divided by g_raw_per_m, as meters text. "n/a" when the property cannot be read.
    // WHY: shows the native autopilot's distance to its dock in the status line. Meters are right only if the raw-per-meter value (65536, see mb_config) is right.
    std::wstring dist_target_m(UObject* ship)
    {
        int64_t raw = 0;
        return pnum_prop(ship, STR("DistanceToTarget"), raw) ? std::to_wstring(static_cast<double>(raw) / g_raw_per_m)
                                                             : std::wstring(STR("n/a"));
    }

    // WHAT: returns the dock id to use for a dock job. refuel = false -> mining/unload dock, refuel = true -> refuel dock. The id is used first; otherwise the in-game name. Returns 0 with `why` set when nothing resolves.
    // WHY: the settings keep two separate dock lists, one for unloading and one for refuelling (brief Update 19). A set id wins over a name, because the id is exact.
    int resolve_cmd_dock(const Config& c, bool refuel, std::wstring& why)
    {
        const int id = refuel ? c.refuel_dock_id : c.dock_id;
        if (id > 0) return id;
        const std::string nm = refuel ? c.refuel_dock_name : (!c.dock_name_filter.empty() ? c.dock_name_filter : c.dock_id_name);
        if (nm.empty()) { why = STR("no dock configured"); return 0; }
        const int r = find_dock_by_name(nm, why);
        return r > 0 ? r : 0;
    }

    // WHAT: appends d to v when d > 0 and not already there.
    // WHY: candidate lists are built from several settings; no 0 and no duplicates.
    void add_unique(std::vector<int>& v, int d)
    {
        if (d > 0 && std::find(v.begin(), v.end(), d) == v.end()) v.push_back(d);
    }

    // WHAT: refuel dock candidates in try order: the single refuel dock (id or name), then refuel_docks. `why` set when the single dock does not resolve.
    // WHY: refuel takes the first free candidate.
    std::vector<int> refuel_candidates(const Config& c, std::wstring& why)
    {
        std::vector<int> out;
        add_unique(out, resolve_cmd_dock(c, true, why));
        for (int d : c.refuel_docks) add_unique(out, d);
        return out;
    }

    // ---------------- order list helpers ----------------

    // WHAT: the TArray<UObject*> behind array property `prop` of o (e.g. "Orders", "WaitConditions"), or nullptr.
    // WHY: order lists are read by reflection; one helper holds the property checks.
    TArray<UObject*>* obj_arr(UObject* o, const TCHAR* prop)
    {
        FProperty* p = alive(o) ? o->GetPropertyByNameInChain(prop) : nullptr;
        return (p && CastField<FArrayProperty>(p)) ? o->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(prop) : nullptr;
    }

    // WHAT: deletes one top-level order: DMSPlayerController.DeleteOrder(ShipID, OrderID). If that function does not exist, sends ConfigureShipOrdersInput (RemoveOrderID = oid, SetAutopilotEnabled = -1, SkipToIndex = -1) to the ship. false with err on failure.
    // WHY: the probe did not capture a top-level delete, so DeleteOrder is the direct route and the input is the fallback. Param names are not guessed: missing params fail with a log line.
    bool send_delete_order(int sid, int oid, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        return ms_err(g_ms.delete_order(sid, oid, e, 256), e, err);
    }

    // WHAT: SkipToOrder(ShipID, NewIndex = 0) on the ship; logs CurrentOrderIndex before and after. false with err when the function or params are missing.
    // WHY: copy of skip_to (mb_dockjob) for index 0. mb_dockjob is included after this file, so the call lives here and logs with log_line.
    bool ob_skip0(int sid, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        int before = -1, after = -1;
        if (!ms_err(g_ms.skip_to(sid, 0, &before, &after, e, 256), e, err)) return false;
        log_line(STR("ORDERBUILD SkipToOrder NewIndex=0 CurrentOrderIndex before=") + std::to_wstring(before) + STR(" after=") + std::to_wstring(after));
        return true;
    }

    // ---------------- order build state machine ----------------

    // WHAT: one step of a build. kind 0 = clear all orders; 1 = DeliverOrder to dock; 2 = child of the last DeliverOrder (cls, item, cmp, amount).
    // WHY: fresh orders with wait conditions. Steps run one send at a time, and each send is confirmed by a readback before the next.
    struct OBStep { int kind = 0; std::wstring cls; std::string item; int cmp = 0; int amount = 0; int dock = 0; };
    struct OrderBuild { std::vector<OBStep> steps; size_t i = 0; int phase = 0; double t = 0; int before = 0; int deliver_id = 0, first_deliver = 0; int state = 1; /*1 running, 2 ok, 3 failed*/ std::wstring result; };
    std::map<int, OrderBuild> g_ob;
    constexpr double OB_TIMEOUT_S = 5.0, OB_SETTLE_S = 0.5;

    int ob_state(int sid) { auto it = g_ob.find(sid); return it == g_ob.end() ? 0 : it->second.state; }
    std::wstring ob_result(int sid) { auto it = g_ob.find(sid); return it == g_ob.end() ? std::wstring() : it->second.result; }
    int ob_first_deliver(int sid) { auto it = g_ob.find(sid); return it == g_ob.end() ? 0 : it->second.first_deliver; }

    // WHAT: queues the steps for ship sid and marks the build running. Replaces an earlier build of the same ship.
    // WHY: ob_tick runs them one send per call; nothing starts a build yet (the dock job does, slice B3).
    void ob_start(int sid, const std::vector<OBStep>& steps)
    {
        OrderBuild b;
        b.steps = steps;
        g_ob[sid] = b;
        log_line(STR("ORDERBUILD start ship=") + std::to_wstring(sid) + STR(" steps=") + std::to_wstring(steps.size()));
    }

    void ob_fail(OrderBuild& b, const std::wstring& msg)
    {
        b.state = 3;
        b.result = msg;
        log_line(STR("ORDERBUILD FAIL ") + msg);
    }

    void ob_next(OrderBuild& b)
    {
        ++b.i;
        b.phase = 0;
        b.t = 0;
    }

    // WHAT: runs one phase of the current step. Sends at most one command per call.
    // WHY: a send is only confirmed by a later readback (server sync is delayed), so each phase waits for the readback before it moves on.
    void ob_run_step(int sid, UObject* ship, OrderBuild& b, double dt)
    {
        const OBStep& s = b.steps[b.i];
        std::wstring err;
        if (s.kind == 0)
        {
            TArray<UObject*>* arr = obj_arr(ship, STR("Orders"));
            const int n = arr ? arr->Num() : 0;
            if (b.phase == 0)
            {
                if (n == 0) { ob_next(b); return; }
                const int oid = obj_id((*arr)[0]);
                b.before = n;
                if (!send_delete_order(sid, oid, err)) { ob_fail(b, STR("delete order: ") + err); return; }
                log_line(STR("ORDERBUILD delete order id=") + std::to_wstring(oid));
                b.phase = 1; b.t = 0;
                return;
            }
            b.t += dt;
            if (n < b.before) { b.phase = 0; b.t = 0; }
            else if (b.t >= OB_TIMEOUT_S) ob_fail(b, STR("clear timeout"));
            return;
        }
        if (s.kind == 1)
        {
            TArray<UObject*>* arr = obj_arr(ship, STR("Orders"));
            const int n = arr ? arr->Num() : 0;
            if (b.phase == 0)
            {
                b.before = n;
                if (!send_new_deliver_order(sid, err)) { ob_fail(b, STR("new deliver order: ") + err); return; }
                b.phase = 1; b.t = 0;
                return;
            }
            if (b.phase == 1)
            {
                b.t += dt;
                if (n > b.before)
                {
                    b.deliver_id = obj_id((*arr)[n - 1]);
                    if (b.first_deliver == 0) b.first_deliver = b.deliver_id;
                    if (!send_update_order_dock(b.deliver_id, s.dock, err)) { ob_fail(b, STR("deliver dock: ") + err); return; }
                    log_line(STR("ORDERBUILD deliver id=") + std::to_wstring(b.deliver_id) + STR(" dock=") + std::to_wstring(s.dock));
                    b.phase = 2; b.t = 0;
                }
                else if (b.t >= OB_TIMEOUT_S) ob_fail(b, STR("deliver order not seen"));
                return;
            }
            b.t += dt;
            if (b.t >= OB_SETTLE_S) ob_next(b);
            return;
        }
        // kind 2: child of the last DeliverOrder
        UObject* d = order_by_id(ship, b.deliver_id);
        if (!alive(d)) { ob_fail(b, STR("deliver order gone")); return; }
        TArray<UObject*>* wc = obj_arr(d, STR("WaitConditions"));
        const int n = wc ? wc->Num() : 0;
        if (b.phase == 0)
        {
            b.before = n;
            if (!send_new_order(b.deliver_id, s.cls, err)) { ob_fail(b, STR("child order: ") + err); return; }
            b.phase = 1; b.t = 0;
            return;
        }
        if (b.phase == 1)
        {
            b.t += dt;
            if (n > b.before)
            {
                const int cid = obj_id((*wc)[n - 1]);
                UObject* it = nullptr;
                if (!s.item.empty())
                {
                    std::wstring why;
                    it = find_item(s.item, why);
                    if (!it) { ob_fail(b, why); return; }
                }
                if (!send_update_order(cid, 0, it, s.cmp, s.amount, err)) { ob_fail(b, STR("child update: ") + err); return; }
                log_line(STR("ORDERBUILD child cls=") + s.cls + STR(" id=") + std::to_wstring(cid) + STR(" item=") + std::wstring(s.item.begin(), s.item.end()) +
                         STR(" cmp=") + std::to_wstring(s.cmp) + STR(" amount=") + std::to_wstring(s.amount));
                b.phase = 2; b.t = 0;
            }
            else if (b.t >= OB_TIMEOUT_S) ob_fail(b, STR("child order not seen"));
            return;
        }
        b.t += dt;
        if (b.t >= OB_SETTLE_S) ob_next(b);
    }

    // WHAT: after the last step: SkipToOrder 0, autopilot on, log the result. Sets the build ok, or failed on error.
    // WHY: same end as the dock leg (SkipToOrder then autopilot, brief Update 24).
    void ob_finish(int sid, OrderBuild& b)
    {
        std::wstring err;
        if (!ob_skip0(sid, err)) { ob_fail(b, STR("skip: ") + err); return; }
        if (!set_autopilot(sid, true, err)) { ob_fail(b, STR("autopilot: ") + err); return; }
        TArray<UObject*>* arr = obj_arr(find_by_id(STR("DMSPlayerShipObject"), sid), STR("Orders"));
        log_line(STR("ORDERBUILD done orders=") + std::to_wstring(arr ? arr->Num() : 0) + STR(" first=") + std::to_wstring(b.first_deliver));
        b.state = 2;
    }

    // WHAT: advances every running build by one call. A ship that is gone fails its build.
    // WHY: called from jobs_tick (slice B3) with the poll step dt.
    void ob_tick(double dt)
    {
        for (auto& e : g_ob)
        {
            OrderBuild& b = e.second;
            if (b.state != 1) continue;
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), e.first);
            if (!alive(ship)) { ob_fail(b, STR("ship gone")); continue; }
            if (b.i >= b.steps.size()) ob_finish(e.first, b);
            else ob_run_step(e.first, ship, b, dt);
        }
    }

    // ---------------- planners (no Config read here) ----------------

    // WHAT: steps for a refuel trip: clear orders, DeliverOrder to dock, one WaitOrder (item >= amount) per fuel, then WaitForTimeOrder(wait_s).
    std::vector<OBStep> ob_plan_refuel(int dock, const std::vector<std::pair<std::string, int>>& fuels, int wait_s)
    {
        std::vector<OBStep> s;
        s.push_back({0});
        s.push_back({1, STR(""), std::string(), 0, 0, dock});
        for (const auto& f : fuels) s.push_back({2, STR("WaitOrder"), f.first, 3, f.second, 0});
        s.push_back({2, STR("WaitForTimeOrder"), std::string(), 0, wait_s, 0});
        return s;
    }

    // WHAT: steps for an unload trip: clear orders, DeliverOrder to dock, one UnloadItemsOrder per ore, then WaitForTimeOrder(wait_s).
    std::vector<OBStep> ob_plan_unload(int dock, const std::vector<std::string>& ores, int wait_s)
    {
        std::vector<OBStep> s;
        s.push_back({0});
        s.push_back({1, STR(""), std::string(), 0, 0, dock});
        for (const auto& o : ores) s.push_back({2, STR("UnloadItemsOrder"), o, 0, 0, 0});
        s.push_back({2, STR("WaitForTimeOrder"), std::string(), 0, wait_s, 0});
        return s;
    }
}
