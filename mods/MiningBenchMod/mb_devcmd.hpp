// FILE: mods/MiningBenchMod/mb_devcmd.hpp
// DEV ONLY: cheat commands reachable via MiningCmd.txt; not used by normal gameplay.
// WHAT: dev-only commands of the command bus: throttle steps (thr_cmd, thr_check), rock selection (rock_sel), move jobs
//       (face_rock, approach_rock), warp tests (warp_sector, warp_by_name, warp_cur), chunk hold release, debug log switch,
//       Dev Menu Stations tab (dev_stations, dev_destroy_station).
//       dev_cmd() is the one dispatch entry; run_cmd (mb_testcmd.hpp) calls it after the product commands.
// WHY separate: these are Dev Menu / Test tab tools, not what the shipped loop needs. Keeping them in one file makes them easy to find and to drop.
// DEPENDS ON (exact): mb_common.hpp (g_debug, tlog), mb_ue.hpp (find_by_id, get_int_prop, set_int_prop, call_int, prop_int),
//                     mb_steer.hpp (spd_of, zero_all, move_start, g_sel_rock state, MV_SPEED_ABORT), mb_rocks.hpp (rock_list_ensure,
//                     rock_actor_by_id, rock_kind, g_sel_rocks), mb_warp.hpp (warp_sector_cmd, warp_by_name_cmd),
//                     mb_warpreal.hpp (warp_real_cmd), mb_chunkhold.hpp (chunk_hold_release_all),
//                     mb_common (int_array, write_atomic, get_dll_dir, u8), mb_ue (obj_id, obj_display, find_by_id, first_of, put, type_text).
// USED BY: mb_testcmd.hpp (run_cmd -> dev_cmd, cmd_tick -> thr_check).
// GAME FACTS: none. Throttle and rock_sel behaviour: see the WHAT/WHY lines of each function.
#pragma once

namespace
{
    // WHAT: throttle readback state: pending = a thr command was written and its readback is due on the next poll step;
    //       sid = ship; want = the Throttle step written.
    // WHY: the readback one poll step later shows whether the write stuck (logged as "thr readback").
    struct ThrCheck { bool pending = false; int sid = 0, want = 0; };

    // WHAT: the throttle readback state (one instance).
    ThrCheck g_thr_chk;

    // WHAT: "rock_sel <id>" selects a rock by save id for ship sid (g_sel_rocks); "rock_sel 0" = auto (nearest rock of the INI resource).
    //       Returns "ok ..." or an error text (bad argument, no rock with that id in the loaded list).
    // WHY: face/approach and the status target use the selected rock while it is alive, ignoring the INI item filter.
    std::wstring rock_sel(int sid, const std::string& cmd)
    {
        const std::string arg = trim(cmd.substr(8));   // after "rock_sel"
        if (arg.empty() || arg.find_first_not_of("0123456789") != std::string::npos) return STR("error: usage rock_sel <id|0>");
        const int id = to_int(arg, -1);
        if (id <= 0)
        {
            g_sel_rocks.erase(sid);
            tlog(STR("rock_sel auto"));
            return STR("ok rock_sel=auto");
        }
        rock_list_ensure();   // rock ids are actor ObjectIds from the loaded list
        UObject* o = rock_actor_by_id(id);
        if (!alive(o)) return STR("error: no rock actor with id ") + std::to_wstring(id);
        g_sel_rocks[sid] = id;
        const std::wstring kind = rock_kind(o);
        tlog(STR("rock_sel rock=") + std::to_wstring(id) + STR(" kind=") + kind);
        return STR("ok rock_sel=") + std::to_wstring(id) + STR(" kind=") + kind;
    }

    // WHAT: thr_up / thr_down / thr_zero: one Throttle step (or 0). Clamped to 0..GetThrottleSteps; the readback happens one poll step later.
    //       Refused (and Throttle zeroed) when the speed is above MV_SPEED_ABORT x the limit.
    // WHY: normal-speed test buttons for the Throttle (step 1 gave about 5.7 m/s, brief Update 22). The speed refusal is the user's
    //      safety rule (no speed override). thr_down never goes below 0 (no reverse here).
    std::wstring thr_cmd(int sid, const std::string& cmd)
    {
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        int32_t steps = 0, cur = -1;
        if (!call_int(ship, STR("GetThrottleSteps"), steps)) return STR("error: GetThrottleSteps unreadable");
        if (!get_int_prop(ship, STR("Throttle"), cur)) return STR("error: Throttle unreadable");
        const int n = cmd == "thr_up" ? std::min(cur + 1, steps) : (cmd == "thr_down" ? std::max(cur - 1, 0) : 0);
        const SpeedMon& sm = spd_of(sid);
        if (sm.have && sm.lim > 0.0 && sm.v / sm.lim > MV_SPEED_ABORT)
        {
            zero_all(ship);
            return STR("error: speed above 1.1x limit, throttle zeroed");
        }
        const bool wrote = set_int_prop(ship, STR("Throttle"), n);
        g_thr_chk = ThrCheck{true, sid, n};
        tlog(STR("thr cmd=") + widen(cmd) + STR(" Throttle was=") + std::to_wstring(cur) + STR(" steps=") + std::to_wstring(steps) +
             STR(" wrote=") + std::to_wstring(n) + STR(" ok=") + (wrote ? STR("1") : STR("0")));
        return STR("ok throttle=") + std::to_wstring(n) + STR("/") + std::to_wstring(steps);
    }

    // WHAT: throttle readback: logs the Throttle value read one poll step after a thr command (once).
    // WHY: shows whether the write stuck (the sim can overwrite values, see brief Update 22).
    void thr_check()
    {
        if (!g_thr_chk.pending) return;
        g_thr_chk.pending = false;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), g_thr_chk.sid);
        int32_t rb = -1;
        const bool ok = alive(ship) && get_int_prop(ship, STR("Throttle"), rb);
        tlog(STR("thr readback=") + (ok ? std::to_wstring(rb) : std::wstring(STR("n/a"))) + STR(" wrote=") + std::to_wstring(g_thr_chk.want));
    }

    // ---------------- Dev Menu Stations tab ----------------
    // WHAT: state of the running destroy (one at a time). t = seconds since start, next = seconds until the next pass.
    struct StDestroy { bool active = false; int id = 0; double t = 0.0, next = 0.0; bool diag = false; };
    StDestroy g_sd;

    // WHAT: ids of stations this session destroyed. stations_write skips them.
    // WHY: the destroyed StationObject can stay findable (not yet garbage collected) and would keep showing in the Dev Menu list.
    std::set<int> g_gone;

    // WHAT: writes DevStationResult.txt = "<id>|<state>|<text>" (state running/done/error). The Lua tab shows the text.
    void station_result(int id, const char* state, const std::wstring& text)
    {
        try { write_atomic(get_dll_dir() / "DevStationResult.txt", std::to_string(id) + "|" + state + "|" + u8(text) + "\n"); } catch (...) {}
    }

    // WHAT: logs the property and function names of o's class chain (stops at Object) once per class name.
    // WHY: the first in-game run must show the real layout (PlatformIDs, AttachedMachineIDs, Faction...) for the next planner round.
    void station_class_dump(UObject* o)
    {
        static std::set<std::wstring> done;
        if (!alive(o)) return;
        UClass* c = o->GetClassPrivate();
        if (!done.insert(c->GetName()).second) return;
        std::wstring props, funcs;
        for (UStruct* s = c; s && s->GetName() != STR("Object"); s = s->GetSuperStruct())
        {
            for (FProperty* p : s->ForEachProperty()) props += p->GetName() + STR(":") + type_text(p) + STR(" ");
            if (UClass* sc = Cast<UClass>(s)) for (UFunction* f : sc->ForEachFunction()) funcs += f->GetName() + STR(" ");
        }
        log_line(STR("DEVSTATION class ") + c->GetName() + STR(" props: ") + props);
        log_line(STR("DEVSTATION class ") + c->GetName() + STR(" funcs: ") + funcs);
    }

    // WHAT: ids of a station int array (MachineIDs, ConstructionObjectIDs, PlatformIDs); empty when the property is missing.
    std::vector<int> station_ids(UObject* st, const TCHAR* prop)
    {
        std::vector<int> r;
        TArray<int32_t>* a = int_array(st, prop);
        for (int i = 0; a && i < a->Num(); ++i) r.push_back((*a)[i]);
        return r;
    }

    // WHAT: writes DevStations.txt (next to docks.txt): one row per StationObject: id|name|class|platforms|destroyable. Returns the row count.
    // WHY: the Lua Stations tab reads it. Every station is written (destroyable 0/1) so the log shows how each was classified.
    int stations_write()
    {
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(STR("StationObject"), v);
        std::string out;
        int n = 0;
        for (UObject* o : v)
        {
            if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) continue;
            const int id = obj_id(o);
            if (id <= 0 || g_gone.count(id)) continue;
            const std::wstring cls = o->GetClassPrivate()->GetName();
            std::wstring name = obj_display(o);
            if (name.empty()) name = cls;
            const int np = static_cast<int>(station_ids(o, STR("MachineIDs")).size() + station_ids(o, STR("ConstructionObjectIDs")).size());
            const std::string row = station_row(id, u8(name), u8(cls), np);
            out += row + "\n";
            ++n;
            log_line(STR("DEVSTATION row ") + widen(row));
            station_class_dump(o);
        }
        try { write_atomic(get_dll_dir() / "DevStations.txt", out); } catch (...) { log_line(STR("DEVSTATION list write failed")); }
        return n;
    }

    // WHAT: DMSPlayerController.AddDisassembleCommand(DisassembleTarget, bMassDisassemble=false): the call the game's build UI makes.
    // WHY: it queues a real input command (co-op lockstep safe). false with err set when the function or a parameter is missing.
    bool station_disassemble(int target, std::wstring& err, bool mass)
    {
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* f = alive(ctrl) ? ctrl->GetFunctionByNameInChain(STR("AddDisassembleCommand")) : nullptr;
        if (!f) { err = STR("AddDisassembleCommand not found"); return false; }
        std::vector<uint8_t> b(f->GetParmsSize() + 16, 0);
        const uint8_t m = mass ? 1 : 0;
        if (!put(f, b, STR("DisassembleTarget"), &target, sizeof(target)) || !put(f, b, STR("bMassDisassemble"), &m, 1))
        { err = STR("AddDisassembleCommand params not found"); return false; }
        ctrl->ProcessEvent(f, b.data());
        return true;
    }

    // WHAT: one ItemStack kind seen in the station: raw struct bytes (Item pointer + Amount) summed by Item.
    struct StashItem { std::vector<uint8_t> raw; uint64_t item = 0; int64_t amount = 0; int off_amount = 0; };

    // WHAT: reads ItemStack Item pointer / Amount (int32) of one ItemTotals element. false when the layout is not the expected one.
    bool stack_read(UStruct* st, const uint8_t* e, uint64_t& item, int32_t& amount, int& off_amount)
    {
        FProperty* pi = st->FindProperty(FName(STR("Item"), FNAME_Add));
        FProperty* pa = st->FindProperty(FName(STR("Amount"), FNAME_Add));
        if (!pi || !pa || !CastField<FIntProperty>(pa) || !CastField<FObjectProperty>(pi)) return false;
        item = *reinterpret_cast<const uint64_t*>(e + pi->GetOffset_Internal());
        off_amount = pa->GetOffset_Internal();
        amount = *reinterpret_cast<const int32_t*>(e + off_amount);
        return true;
    }

    // WHAT: removes every item of an Inventory through the game's own Inventory.RemoveItems (keeps ItemTotals / bEmpty consistent) and adds
    //       what it held to `acc` (summed per Item). Returns the number of stacks removed.
    // WHY: DEV CHEAT, local sim write (not an input command, so NOT co-op safe). The first attempt (zeroing the arrays) left the crates in place.
    int station_drain_inventory(UObject* inv, std::vector<StashItem>& acc)
    {
        if (!alive(inv)) return 0;
        FProperty* p = inv->GetPropertyByNameInChain(STR("ItemTotals"));
        auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
        auto* ta = ap ? inv->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("ItemTotals")) : nullptr;
        auto* sp = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
        UFunction* rm = inv->GetFunctionByNameInChain(STR("RemoveItems"));
        if (!ta || !sp || !rm) return 0;
        const int es = ap->GetInner()->GetElementSize();
        std::vector<std::vector<uint8_t>> snap;                      // copy first: RemoveItems edits the array under us
        for (int i = 0; i < ta->Num(); ++i)
        {
            const uint8_t* e = reinterpret_cast<uint8_t*>(ta->GetData()) + static_cast<size_t>(i) * es;
            snap.emplace_back(e, e + es);
        }
        int n = 0;
        for (auto& e : snap)
        {
            uint64_t item = 0; int32_t amt = 0; int off = 0;
            if (!stack_read(sp->GetStruct(), e.data(), item, amt, off) || amt <= 0 || item == 0) continue;
            std::vector<uint8_t> b(rm->GetParmsSize() + 16, 0);
            if (!put(rm, b, STR("ItemsToRemove"), e.data(), e.size())) continue;
            inv->ProcessEvent(rm, b.data());
            ++n;
            bool found = false;
            for (auto& s2 : acc) if (s2.item == item) { s2.amount += amt; found = true; break; }
            if (!found) { StashItem si; si.raw = e; si.item = item; si.amount = amt; si.off_amount = off; acc.push_back(si); }
        }
        return n;
    }

    // WHAT: sends SelfDestructInput (the game's own station self destruct) to the station: CreateInputSelfDestruct on the class default object,
    //       then AddInputCommand(station id, cmd). false with err set when a function is missing.
    // WHY: once the machines are removed (refunded) the game itself deletes the station. Factory params are zero-filled; their names are logged once.
    bool station_self_destruct(int id, std::wstring& err)
    {
        auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.SelfDestructInput"));
        UObject* cdo = cls ? cls->GetClassDefaultObject() : nullptr;
        UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(STR("CreateInputSelfDestruct")) : nullptr;
        if (!fn) { err = STR("CreateInputSelfDestruct not found"); return false; }
        std::wstring sig;
        for (FProperty* pp : fn->ForEachProperty()) sig += pp->GetName() + STR(":") + type_text(pp) + STR(" ");
        log_line(STR("DEVSTATION CreateInputSelfDestruct params: ") + sig);
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        cdo->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        UObject* cmd = rp ? *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal()) : nullptr;
        if (!alive(cmd)) { err = STR("CreateInputSelfDestruct returned no command"); return false; }
        return add_input(id, cmd, err);
    }

    // WHAT: one destroy pass. Phase 1: remove the machines one by one with AddDisassembleCommand (the game's deconstruct, it refunds the cost and
    //       the contents into the player inventory); bPlayerDisassembly is forced true first (the only refusal gate), docks go last.
    //       Phase 2 (MachineIDs empty): self destruct the station once (station_self_destruct). Returns true when the station object is gone.
    // WHY: user request 2026-11-14: remove machines for the refund, then use the game's self destruct instead of mass disassemble / item stashing.
    bool station_step()
    {
        UObject* st = find_by_id(STR("StationObject"), g_sd.id);
        if (!alive(st)) return true;
        const std::vector<int> machines = station_ids(st, STR("MachineIDs"));
        std::wstring err;
        if (machines.empty())
        {
            if (g_sd.diag) return false;   // self destruct already sent, waiting for the station to vanish
            g_sd.diag = true;
            const bool ok = station_self_destruct(g_sd.id, err);
            log_line(STR("DEVSTATION self destruct station=") + std::to_wstring(g_sd.id) + (ok ? STR(" sent") : STR(" failed: ") + err));
            if (!ok) { g_sd.active = false; station_result(g_sd.id, "error", err); }
            else station_result(g_sd.id, "running", STR("machines removed, self destruct sent"));
            return false;
        }
        std::set<int> want(machines.begin(), machines.end());
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("MachineObject"), all);
        std::vector<UObject*> plain, docks;
        for (UObject* o : all)
        {
            if (!alive(o) || !want.count(obj_id(o))) continue;
            (o->GetClassPrivate()->GetName().find(STR("Dock")) != std::wstring::npos ? docks : plain).push_back(o);
        }
        const std::vector<UObject*>& pick = plain.empty() ? docks : plain;
        int sent = 0, fail = 0;
        for (UObject* o : pick)
        {
            bool can = false;
            get_bool_prop(o, STR("bPlayerDisassembly"), can);
            if (!can) set_bool_prop(o, STR("bPlayerDisassembly"), true);
            if (station_disassemble(obj_id(o), err, false)) ++sent; else ++fail;
        }
        log_line(STR("DEVSTATION step station=") + std::to_wstring(g_sd.id) + STR(" t=") + std::to_wstring(static_cast<int>(g_sd.t)) +
                 STR("s machines=") + std::to_wstring(machines.size()) + STR(" sent=") + std::to_wstring(sent) + STR(" failed=") + std::to_wstring(fail) +
                 (err.empty() ? std::wstring() : STR(" err=") + err));
        if (fail > 0 && sent == 0) { g_sd.active = false; station_result(g_sd.id, "error", err); }
        return false;
    }

    // WHAT: runs from cmd_tick every 0.25 s (dt = poll step). Every 2 s it does a pass until the station is gone or 60 s passed. Rewrites the list file at the end.
    void station_destroy_tick(double dt)
    {
        if (!g_sd.active) return;
        g_sd.t += dt;
        g_sd.next -= dt;
        if (g_sd.next > 0.0) return;
        g_sd.next = 2.0;
        if (g_sd.t > 60.0)
        {
            g_sd.active = false;
            log_line(STR("DEVSTATION timeout station=") + std::to_wstring(g_sd.id) + STR(" still present after 60 s"));
            {
                UObject* left = find_by_id(STR("StationObject"), g_sd.id);
                const size_t n = alive(left) ? station_ids(left, STR("MachineIDs")).size() : 0;
                station_result(g_sd.id, "error", STR("60 s: ") + std::to_wstring(n) + STR(" machines still stand - remove them by hand"));
            }
            stations_write();
            return;
        }
        if (station_step())
        {
            g_sd.active = false;
            log_line(STR("DEVSTATION done station=") + std::to_wstring(g_sd.id) + STR(" gone after ") + std::to_wstring(static_cast<int>(g_sd.t)) + STR(" s"));
            g_gone.insert(g_sd.id);
            station_result(g_sd.id, "done", STR("station destroyed"));
            stations_write();
        }
    }

    // WHAT: "dev_destroy_station:<id>". Refuses a bad id, a missing station, a protected class, or a second run. Starts the retry loop.
    // WHY: the class check lives here too (not only in Lua), so a protected station can never be destroyed from this command.
    std::wstring station_destroy_start(const std::string& arg)
    {
        const int id = parse_station_id(arg);
        if (id <= 0) return STR("error: usage dev_destroy_station:<id>");
        if (g_sd.active) return STR("error: a destroy is already running");
        UObject* st = find_by_id(STR("StationObject"), id);
        if (!alive(st)) { station_result(id, "error", STR("no station with that id")); return STR("error: no station with id ") + std::to_wstring(id); }
        const std::wstring cls = st->GetClassPrivate()->GetName();
        if (!station_destroyable(to_utf8(cls)))
        {
            log_line(STR("DEVSTATION refused station=") + std::to_wstring(id) + STR(" class=") + cls);
            station_result(id, "error", STR("refused: ") + cls + STR(" is not a player-built station"));
            return STR("error: refused, protected station class ") + cls;
        }
        g_sd = StDestroy{true, id, 0.0, 0.0, false};
        log_line(STR("DEVSTATION start station=") + std::to_wstring(id) + STR(" name='") + obj_display(st) + STR("' class=") + cls);
        station_result(id, "running", STR("destroying..."));
        return STR("ok destroy started station=") + std::to_wstring(id);
    }

    // WHAT: "dev_clear_inv": removes every item from the player's own inventory (DMSPlayerObject.Inventory) through the game's RemoveItems
    //       (station_drain_inventory). Station and ship cargo are untouched. Refuses when the inventory or RemoveItems is unreadable.
    // WHY: Dev Menu Cheats "Clear my inventory". DEV CHEAT, local sim write like the drain (not an input command, not co-op safe).
    std::wstring dev_clear_inv()
    {
        UObject* po = first_of(STR("DMSPlayerObject"));
        UObject* inv = alive(po) ? get_obj_prop(po, STR("Inventory")) : nullptr;
        if (!alive(inv)) { log_line(STR("DEVCLEAR refused: player inventory not found")); return STR("error: player inventory not found"); }
        std::vector<StashItem> acc;
        const int n = station_drain_inventory(inv, acc);
        int64_t units = 0;
        for (auto& si : acc) units += si.amount;
        log_line(STR("DEVCLEAR player inventory: stacks_removed=") + std::to_wstring(n) + STR(" item_kinds=") + std::to_wstring(acc.size()) + STR(" units=") + std::to_wstring(units));
        return STR("ok cleared stacks=") + std::to_wstring(n);
    }

    // WHAT: dispatch one dev command. handled = true when the command is a dev command (result in the return value);
    //       false = not a dev command, the caller goes on with its own list.
    // WHY: keeps the dev commands out of run_cmd; the lines below are the former run_cmd lines, unchanged.
    std::wstring dev_cmd(int seq, int sid, const std::string& cmd, bool& handled)
    {
        handled = true;
        if (cmd == "debug_on" || cmd == "debug_off") { g_debug = (cmd == "debug_on"); return g_debug ? STR("ok debug log on") : STR("ok debug log off"); }
        // 2. throttle steps
        if (cmd == "thr_up" || cmd == "thr_down" || cmd == "thr_zero") return thr_cmd(sid, cmd);
        // 3. rock selection
        if (cmd.rfind("rock_sel", 0) == 0) return rock_sel(sid, cmd);
        // 4. face / approach (move jobs)
        static const char* const kMoveCmds[] = {"face_rock", "approach_rock"};
        for (const char* mc : kMoveCmds) if (cmd == mc) return move_start(sid, cmd, seq);
        if (cmd.rfind("warp_sector:", 0) == 0) return warp_sector_cmd(sid, cmd.substr(12));
        if (cmd.rfind("warp_by_name:", 0) == 0) return warp_by_name_cmd(cmd.substr(13));
        if (cmd.rfind("warp_cur:", 0) == 0) return warp_real_cmd(0, cmd.substr(9));   // Dev Menu cheat: piloted ship, real warp (variant e)
        if (cmd == "chunkhold:release_all") { chunk_hold_release_all(); return STR("ok chunk holds released (see CHUNKHOLD log)"); }   // Dev Menu "Release all chunk holds" (mb_chunkhold.hpp)
        if (cmd == "reg_dump" || cmd.rfind("reg_query:", 0) == 0 || cmd.rfind("reg_check:", 0) == 0) return registry_dev(sid, cmd);   // rock registry (mb_registry.hpp)
        if (cmd.rfind("bubble_status:", 0) == 0 || cmd.rfind("bubble_sim:", 0) == 0) return bubble_dev(cmd);   // bubble reflex, read-only (mb_bubble.hpp)
        if (cmd == "dev_clear_inv") return dev_clear_inv();
        if (cmd == "dev_stations") return STR("ok stations=") + std::to_wstring(stations_write());
        if (cmd.rfind("dev_destroy_station:", 0) == 0) return station_destroy_start(cmd.substr(20));
        handled = false;
        return std::wstring();
    }

    // ---------------- probe avoidance (MiningProbeAvoid.txt) ----------------
    // WHAT: probe_avoid[:<ship>] writes one dump of the ship's flight model (named values, every property, class chain and the
    //       avoidance / impulse names) plus the ship state. probe_avoid_watch:<ship>:<seconds> samples the ship every poll tick
    //       (0.25 s, probe_avoid_tick) into the same file: position, speed, Throttle, bAutopilot, DistanceToTarget, ImpulseInput,
    //       WorldImpulse and the nearest rock (existing rock list, distance from the cached rock location).
    // WHY: compare impulse behaviour near a rock with game autopilot on and off (avoidance tuning). Read-only: no game state is written.
    // GAME FACTS: none. Field names are candidates; a missing one prints "absent" / "n/a".
    // SAFETY: every game read runs under seh_run (a fault is logged and reported, the game goes on). Game thread only (cmd_poll, cmd_tick).

    // WHAT: the flight model names the probe looks for (value from the flight model object, else the ship, else "absent").
    const TCHAR* const kProbeNames[] = {
        STR("AvoidanceQuerySize"), STR("AvoidanceQueryDist"), STR("AvoidanceQueryHeightOffset"), STR("AvoidanceTime"),
        STR("AutopilotMinThrottle"), STR("AutopilotRotationImpulseForce"), STR("AutopilotExitDockTime"), STR("AutopilotBankFactor"),
        STR("AutopilotImpulse"), STR("AutopilotImpulseMultiplier"), STR("MaxSteeringDegrees"), STR("DeadZoneDegrees"),
        STR("SteeringResponse"), STR("GlobalMaxSpeed"), STR("ThrustMultiplier"), STR("AccelerationMultiplier"),
        STR("MaxImpulse"), STR("ImpulseMultiplier"), STR("RotSpeed"), STR("MaxRotationImpulse"), STR("RotationImpulseForce"),
        STR("AlignmentTolerance"), STR("SteeringInterpStep"), STR("FinalApproachProximity")};

    // WHAT: sampler state (one watch at a time). lines counts the file lines written (header included).
    // boxw / bmode: probe_collision_boxwatch (box samples, probe_boxwatch_fn) with a PROBE_BOX_* mode.
    struct ProbeWatch { bool active = false; int sid = 0; double elapsed = 0.0, total = 0.0; size_t lines = 0; bool collide = false; bool boxw = false; int bmode = 0; };
    ProbeWatch g_pav;

    std::filesystem::path probe_path() { return get_dll_dir() / "MiningProbeAvoid.txt"; }

    // WHAT: "12.34"-style text with 2 decimals.
    std::wstring probe_f2(double v) { wchar_t b[48]; swprintf(b, 48, L"%.2f", v); return std::wstring(b); }

    // WHAT: "(x,y,z)" of raw units as whole numbers.
    std::wstring probe_v3(const double v[3])
    {
        return STR("(") + std::to_wstring(static_cast<long long>(v[0])) + STR(",") + std::to_wstring(static_cast<long long>(v[1])) +
               STR(",") + std::to_wstring(static_cast<long long>(v[2])) + STR(")");
    }

    // WHAT: one "  name=value" line.
    void probe_kv(std::wstring& s, const TCHAR* k, const std::wstring& v) { s += STR("  ") + std::wstring(k) + STR("=") + v + STR("\n"); }

    // WHAT: one property value as text: int64 and byte here, the rest through any_prop (bool, int, float, double, struct, object, array).
    std::wstring probe_val(UObject* o, FProperty* p, const std::wstring& name)
    {
        uint8_t* base = reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal();
        if (CastField<FInt64Property>(p)) return std::to_wstring(*reinterpret_cast<int64_t*>(base));
        if (CastField<FByteProperty>(p)) return std::to_wstring(static_cast<unsigned>(*base));
        return any_prop(o, name.c_str());
    }

    // WHAT: one line per property of o over its whole class chain: name [type] owner=<class> = value. Stops at Object.
    // WHY: the user asked for every field, so unknown fields show up too.
    void probe_dump_props(std::wstring& s, UObject* o)
    {
        for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
            for (FProperty* p : st->ForEachProperty())
            {
                const std::wstring n = p->GetName();
                s += STR("  ") + n + STR(" [") + type_text(p) + STR("] owner=") + std::wstring(st->GetName()) + STR(" = ") + probe_val(o, p, n) + STR("\n");
            }
    }

    // WHAT: the candidate names of kProbeNames: value from fm, else from ship, else "absent".
    void probe_named(std::wstring& s, UObject* fm, UObject* ship)
    {
        for (const TCHAR* n : kProbeNames)
        {
            UObject* src = nullptr;
            FProperty* p = nullptr;
            const TCHAR* where = STR("");
            if (alive(fm) && (p = fm->GetPropertyByNameInChain(n))) { src = fm; where = STR("fm"); }
            else if (alive(ship) && (p = ship->GetPropertyByNameInChain(n))) { src = ship; where = STR("ship"); }
            if (!src) { probe_kv(s, n, STR("absent")); continue; }
            probe_kv(s, n, std::wstring(where) + STR(" ") + type_text(p) + STR(" ") + probe_val(src, p, std::wstring(n)));
        }
    }

    // WHAT: class chain of o and its properties / functions whose name matches probe_name_match, with the owner class.
    void probe_class_info(std::wstring& s, const TCHAR* label, UObject* o)
    {
        std::wstring chain;
        for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct()) chain += std::wstring(st->GetName()) + STR(" > ");
        s += STR("  ") + std::wstring(label) + STR(" chain: ") + chain + STR("Object\n");
        for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
        {
            for (FProperty* p : st->ForEachProperty())
            {
                const std::wstring n = p->GetName();
                if (probe_name_match(n)) s += STR("  prop ") + n + STR(" [") + type_text(p) + STR("] owner=") + std::wstring(st->GetName()) + STR("\n");
            }
            if (UClass* sc = Cast<UClass>(st))
                for (UFunction* f : sc->ForEachFunction())
                {
                    const std::wstring n = f->GetName();
                    if (probe_name_match(n)) s += STR("  func ") + n + STR(" owner=") + std::wstring(sc->GetName()) + STR("\n");
                }
        }
    }

    // WHAT: ship state for the dump: autopilot, throttle, impulses, momentum, target distance, strafe/drift, speed limit, position, hull and shields.
    //       Units: vectors and positions raw (uu / PNum raw), speeds also m/s (raw / g_raw_per_m). n/a when unreadable.
    void probe_ship_state(std::wstring& s, UObject* ship, int sid)
    {
        ActFrame f;
        const bool hasf = act_frame(ship, f);
        int64_t lim_mod = 0, lim = 0;
        const bool ok_mod = call_pnum(ship, STR("GetModifiedMaxSpeed"), lim_mod);
        const bool ok_lim = call_pnum(ship, STR("GetMaxSpeed"), lim);
        probe_kv(s, STR("bAutopilot"), prop_bool(ship, STR("bAutopilot")));
        probe_kv(s, STR("Throttle"), prop_int(ship, STR("Throttle")));
        probe_kv(s, STR("ImpulseInput"), pvec_text(ship, STR("ImpulseInput")));
        probe_kv(s, STR("WorldImpulse"), pvec_text(ship, STR("WorldImpulse")));
        probe_kv(s, STR("ImpulseMomentum"), pvec_text(ship, STR("ImpulseMomentum")));
        probe_kv(s, STR("LocalMomentum"), pvec_text(ship, STR("LocalMomentum")));
        probe_kv(s, STR("DistanceToTarget"), pnum_raw_text(ship, STR("DistanceToTarget")) + STR(" m=") + dist_target_m(ship));
        probe_kv(s, STR("bStrafing"), prop_bool(ship, STR("bStrafing")));
        probe_kv(s, STR("bDrifting"), prop_bool(ship, STR("bDrifting")));
        probe_kv(s, STR("GetModifiedMaxSpeed"), ok_mod ? std::to_wstring(lim_mod) + STR(" m/s=") + probe_f2(lim_mod / g_raw_per_m) : std::wstring(STR("n/a")));
        probe_kv(s, STR("GetMaxSpeed"), ok_lim ? std::to_wstring(lim) + STR(" m/s=") + probe_f2(lim / g_raw_per_m) : std::wstring(STR("n/a")));
        probe_kv(s, STR("ship_speed"), probe_f2(spd_of(sid).v / g_raw_per_m) + STR(" m/s (sampler speed)"));
        probe_kv(s, STR("actor_loc_uu"), hasf ? probe_v3(f.loc) : std::wstring(STR("n/a")));
        probe_kv(s, STR("MaxHealth"), prop_int(ship, STR("MaxHealth")));
        probe_kv(s, STR("Health"), prop_int(ship, STR("Health")));
        probe_kv(s, STR("MaxShields"), prop_int(ship, STR("MaxShields")));
        probe_kv(s, STR("Shields"), prop_int(ship, STR("Shields")));
    }

    struct ProbeDumpCtx { UObject* ship; UObject* fm; int sid; std::wstring* out; };

    // WHAT: builds the whole dump text (runs under seh_run from probe_avoid_cmd).
    void probe_dump_fn(void* p)
    {
        auto* c = static_cast<ProbeDumpCtx*>(p);
        std::wstring& s = *c->out;
        s += STR("MiningProbeAvoid ship=") + std::to_wstring(c->sid) + STR("\n[ship state]\n");
        probe_ship_state(s, c->ship, c->sid);
        s += STR("[flight model named values]\n");
        if (!alive(c->fm)) s += STR("  FlightModel = n/a (object not readable)\n");
        else
        {
            s += STR("  FlightModel class = ") + std::wstring(c->fm->GetClassPrivate()->GetName()) + STR("\n");
            probe_named(s, c->fm, c->ship);
        }
        s += STR("[flight model properties, all]\n");
        if (alive(c->fm)) probe_dump_props(s, c->fm);
        else s += STR("  n/a\n");
        s += STR("[class info, names matching avoid|collision|obstacle|bubble|query|sweep|impulse|autopilot|proximity|overlap]\n");
        probe_class_info(s, STR("ship"), c->ship);
        if (alive(c->fm)) probe_class_info(s, STR("flight model"), c->fm);
    }

    // ---------------- probe collision (CollisionTest, MiningProbeAvoid.txt) ----------------
    // WHAT: probe_collision[:<ship>] dumps the CollisionTest signature, the ship's collision values and the CollisionEnv object.
    //       probe_collision_call:<ship>:<x>:<y>:<z> calls CollisionTest once with a local offset in m (x forward, y right, z up).
    //       probe_collision_box:<ship>:<minx>:<miny>:<minz>:<maxx>:<maxy>:<maxz>[:local|world|raw] (m) calls CollisionTest with a box, writes the hits;
    //       probe_collision_boxwatch:<ship>:<seconds>[:local|world] samples the all (+-50 m) and fwd boxes every 0.25 s (probe_boxwatch_fn).
    //       probe_collision_watch:<ship>:<seconds> samples it with the avoidance watch: at the forward point 50 * Throttle / 10 m,
    //       at 50 m, and at (forward, 0, 20 m).
    // WHY: see what the game's own obstacle query answers while our steering approaches a rock.
    // GAME FACTS: none. Only a signature with exactly one input vector is called (PVector: X/Y/Z PNum; FVector: X/Y/Z double or float).
    //       Anything else is reported in the file and not called.
    // SAFETY: game reads and the CollisionTest call run under seh_run, game thread only. The mod writes no game state.

    // WHAT: the call plan: CollisionTest, its one input parameter and the vector kind ("pnum" or "uu"). why = what does not fit.
    struct CollPlan
    {
        UFunction* fn = nullptr;
        FProperty* in = nullptr;
        std::wstring kind, why;
        bool ok() const { return fn && in && !kind.empty(); }
    };

    // WHAT: "pnum" for a PVector (X, Y, Z are PNum structs with an int Value), "uu" for an FVector (X, Y, Z double or float), "" otherwise.
    std::wstring probe_vec_kind(FProperty* p)
    {
        static const TCHAR* const kAxis[3] = {STR("X"), STR("Y"), STR("Z")};
        auto* sp = CastField<FStructProperty>(p);
        if (!sp || !sp->GetStruct()) return STR("");
        bool pnum = true, uu = true;
        for (const TCHAR* n : kAxis)
        {
            FProperty* f = sp->GetStruct()->FindProperty(FName(n, FNAME_Add));
            if (!f) return STR("");
            if (CastField<FDoubleProperty>(f) || CastField<FFloatProperty>(f)) pnum = false;
            else if (auto* fs = CastField<FStructProperty>(f))
            {
                uu = false;
                FProperty* v = fs->GetStruct() ? fs->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add)) : nullptr;
                if (!v || !(CastField<FIntProperty>(v) || CastField<FInt64Property>(v))) pnum = false;
            }
            else return STR("");
        }
        return pnum ? STR("pnum") : uu ? STR("uu") : STR("");
    }

    // WHAT: the call plan of CollisionTest on the ship: exactly one input parameter, and it is a vector.
    CollPlan probe_coll_plan(UObject* ship)
    {
        CollPlan pl;
        pl.fn = ship->GetFunctionByNameInChain(STR("CollisionTest"));
        if (!pl.fn) { pl.why = STR("CollisionTest not found"); return pl; }
        int nin = 0;
        for (FProperty* p : pl.fn->ForEachProperty())
        {
            if (!p->HasAnyPropertyFlags(CPF_Parm) || p->HasAnyPropertyFlags(CPF_OutParm) || p->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
            pl.in = p;
            ++nin;
        }
        if (nin != 1)
        {
            pl.in = nullptr;
            pl.why = STR("needs ") + std::to_wstring(nin) + STR(" input parameters, only one vector is supported");
            return pl;
        }
        pl.kind = probe_vec_kind(pl.in);
        if (pl.kind.empty()) pl.why = STR("input ") + std::wstring(pl.in->GetName()) + STR(" is ") + type_text(pl.in) + STR(", not a PVector or FVector");
        return pl;
    }

    // WHAT: plan text for the header and the dump: "fits: ..." or "not supported: <reason>".
    std::wstring probe_plan_note(const CollPlan& pl)
    {
        if (!pl.ok()) return STR("not supported: ") + pl.why;
        return STR("fits: input ") + std::wstring(pl.in->GetName()) + STR(" kind=") + pl.kind;
    }

    // WHAT: writes the local offset m (metres) into the input vector at base: pnum = Q16.16 raw in each Value, uu = FVector units (100 per m).
    void probe_put_vec(uint8_t* base, UStruct* vec, const std::wstring& kind, const double m[3])
    {
        static const TCHAR* const kAxis[3] = {STR("X"), STR("Y"), STR("Z")};
        for (int i = 0; i < 3; ++i)
        {
            FProperty* f = vec->FindProperty(FName(kAxis[i], FNAME_Add));
            uint8_t* d = base + f->GetOffset_Internal();
            if (kind == STR("pnum"))
            {
                FProperty* v = CastField<FStructProperty>(f)->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
                uint8_t* vd = d + v->GetOffset_Internal();
                if (CastField<FInt64Property>(v)) *reinterpret_cast<int64_t*>(vd) = probe_m_to_q16(m[i]);
                else *reinterpret_cast<int32_t*>(vd) = static_cast<int32_t>(probe_m_to_q16(m[i]));
            }
            else if (CastField<FFloatProperty>(f)) *reinterpret_cast<float*>(d) = static_cast<float>(probe_m_to_uu(m[i]));
            else *reinterpret_cast<double*>(d) = probe_m_to_uu(m[i]);
        }
    }

    // WHAT: one output or return value as text, read from the parameter buffer at its offset (bool through its field mask).
    std::wstring probe_pval_text(const uint8_t* buf, FProperty* p)
    {
        const uint8_t* d = buf + p->GetOffset_Internal();
        if (auto* bp = CastField<FBoolProperty>(p)) return (*d & bp->GetFieldMask()) ? STR("1") : STR("0");
        if (CastField<FIntProperty>(p)) return std::to_wstring(*reinterpret_cast<const int32_t*>(d));
        if (CastField<FInt64Property>(p)) return std::to_wstring(*reinterpret_cast<const int64_t*>(d));
        if (CastField<FByteProperty>(p)) return std::to_wstring(static_cast<unsigned>(*d));
        if (CastField<FFloatProperty>(p)) return std::to_wstring(*reinterpret_cast<const float*>(d));
        if (CastField<FDoubleProperty>(p)) return std::to_wstring(*reinterpret_cast<const double*>(d));
        if (auto* sp = CastField<FStructProperty>(p)) return dump_struct(d, sp->GetStruct());
        if (CastField<FObjectProperty>(p))
        {
            UObject* o = *reinterpret_cast<UObject* const*>(d);
            return alive(o) ? std::wstring(o->GetName()) : std::wstring(STR("null"));
        }
        return STR("?");
    }

    // WHAT: "name=value;" for each output and return parameter of fn, read from buf. "none" when there are none.
    std::wstring probe_results(UFunction* fn, const uint8_t* buf)
    {
        std::wstring s;
        for (FProperty* p : fn->ForEachProperty())
        {
            if (!p->HasAnyPropertyFlags(CPF_OutParm) && !p->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
            s += std::wstring(p->GetName()) + STR("=") + probe_pval_text(buf, p) + STR(";");
        }
        return s.empty() ? std::wstring(STR("none")) : s;
    }

    struct CollCall { std::wstring results, in_hex, out_hex; };

    // WHAT: one CollisionTest call with the local offset m (metres): the results and the parameter bytes before and after the call.
    // WHY: the bytes show the encoding used, so a wrong unit is visible in the file. Callers run it under seh_run.
    CollCall probe_coll_call(UObject* ship, const CollPlan& pl, const double m[3])
    {
        CollCall r;
        const size_t psz = static_cast<size_t>(pl.fn->GetParmsSize());
        std::vector<uint8_t> buf(psz + 16, 0);
        probe_put_vec(buf.data() + pl.in->GetOffset_Internal(), CastField<FStructProperty>(pl.in)->GetStruct(), pl.kind, m);
        r.in_hex = probe_hex(buf.data(), psz);
        ship->ProcessEvent(pl.fn, buf.data());
        r.out_hex = probe_hex(buf.data(), psz);
        r.results = probe_results(pl.fn, buf.data());
        return r;
    }

    // WHAT: one UFunction as "name(name:type off=N dir=in|out|return|local flags=0x..., ...) -> return ParmsSize=N".
    std::wstring probe_fn_text(UFunction* f)
    {
        std::wstring s = std::wstring(f->GetName()) + STR("(");
        std::wstring ret;
        for (FProperty* p : f->ForEachProperty())
        {
            const bool out = p->HasAnyPropertyFlags(CPF_OutParm), rv = p->HasAnyPropertyFlags(CPF_ReturnParm);
            const TCHAR* dir = rv ? STR("return") : out ? STR("out") : p->HasAnyPropertyFlags(CPF_Parm) ? STR("in") : STR("local");
            wchar_t fl[32];
            swprintf(fl, 32, L"0x%llx", static_cast<unsigned long long>(p->GetPropertyFlags()));
            const std::wstring pt = std::wstring(p->GetName()) + STR(":") + type_text(p) + STR(" off=") + std::to_wstring(p->GetOffset_Internal()) +
                                    STR(" dir=") + dir + STR(" flags=") + fl;
            if (rv) ret = pt;
            else s += pt + STR(", ");
        }
        s += STR(")");
        if (!ret.empty()) s += STR(" -> ") + ret;
        return s + STR(" ParmsSize=") + std::to_wstring(f->GetParmsSize());
    }

    struct CollNoteCtx { UObject* ship; std::wstring* out; };
    void probe_coll_note_fn(void* p)
    {
        auto* c = static_cast<CollNoteCtx*>(p);
        *c->out = probe_plan_note(probe_coll_plan(c->ship));
    }

    struct ProbeCollDumpCtx { UObject* ship; int sid; std::wstring* out; };

    // WHAT: probe_collision dump text (runs under seh_run): ship collision values, CollisionTest signature and plan, CollisionEnv properties and functions.
    void probe_coll_dump_fn(void* p)
    {
        auto* c = static_cast<ProbeCollDumpCtx*>(p);
        UObject* ship = c->ship;
        std::wstring& s = *c->out;
        s += STR("MiningProbeAvoid collision ship=") + std::to_wstring(c->sid) + STR("\n[ship collision]\n");
        int64_t cr = 0;
        probe_kv(s, STR("CollisionRadius"), pnum_prop(ship, STR("CollisionRadius"), cr) ? std::to_wstring(cr) + STR(" m=") + probe_f2(cr / PROBE_Q16_PER_M) : std::wstring(STR("n/a")));
        probe_kv(s, STR("AvoidanceBias"), struct_prop_text(ship, STR("AvoidanceBias")));
        probe_kv(s, STR("CollisionEnvID"), any_prop(ship, STR("CollisionEnvID")));
        UObject* env = get_obj_prop(ship, STR("CollisionEnv"));
        probe_kv(s, STR("CollisionEnv"), alive(env) ? std::wstring(env->GetName()) + STR(" class=") + std::wstring(env->GetClassPrivate()->GetName()) : std::wstring(STR("n/a")));
        s += STR("[CollisionTest signature]\n");
        const CollPlan pl = probe_coll_plan(ship);
        if (!pl.fn) s += STR("  not found\n");
        else s += STR("  ") + probe_fn_text(pl.fn) + STR("\n  plan: ") + probe_plan_note(pl) + STR("\n");
        s += STR("[CollisionEnv properties]\n");
        if (!alive(env)) { s += STR("  n/a\n"); return; }
        probe_dump_props(s, env);
        s += STR("[CollisionEnv functions]\n");
        for (UStruct* st = env->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
            if (UClass* sc = Cast<UClass>(st))
                for (UFunction* f : sc->ForEachFunction()) s += STR("  ") + probe_fn_text(f) + STR("\n");
    }

    // WHAT: the collision part of one watch sample (runs under seh_run inside the sample): CollisionTest at the forward point,
    //       at 50 m and at (forward, 0, 20 m). "skip" for each when the signature does not fit.
    std::wstring probe_collide_text(UObject* ship)
    {
        const CollPlan pl = probe_coll_plan(ship);
        if (!pl.ok()) return STR(" coll_fwd=skip coll_50=skip coll_up=skip");
        int32_t thr = 1;
        get_int_prop(ship, STR("Throttle"), thr);
        const double d = probe_fwd_m(thr);
        const double p_fwd[3] = {d, 0.0, 0.0}, p_50[3] = {PROBE_FWD_FIXED_M, 0.0, 0.0}, p_up[3] = {d, 0.0, PROBE_UP_M};
        return STR(" coll_fwd_m=") + probe_f2(d) + STR(" coll_fwd=") + probe_coll_call(ship, pl, p_fwd).results +
               STR(" coll_50=") + probe_coll_call(ship, pl, p_50).results + STR(" coll_up=") + probe_coll_call(ship, pl, p_up).results;
    }

    struct CollCallCtx { UObject* ship; int sid; const double* m; std::wstring* out; };
    void probe_coll_call_fn(void* p)
    {
        auto* c = static_cast<CollCallCtx*>(p);
        std::wstring& s = *c->out;
        const CollPlan pl = probe_coll_plan(c->ship);
        s += STR("MiningProbeAvoid collision call ship=") + std::to_wstring(c->sid) + STR("\n");
        s += STR("offset_m=(") + probe_f2(c->m[0]) + STR(",") + probe_f2(c->m[1]) + STR(",") + probe_f2(c->m[2]) +
             STR(") local x=forward y=right z=up, passed as is (no frame transform)\n");
        if (!pl.ok()) { s += STR("not called: ") + probe_plan_note(pl) + STR("\n"); return; }
        s += STR("signature: ") + probe_fn_text(pl.fn) + STR("\n");
        const CollCall r = probe_coll_call(c->ship, pl, c->m);
        s += STR("input_bytes: ") + r.in_hex + STR("\nafter_bytes: ") + r.out_hex + STR("\nresults: ") + r.results + STR("\n");
    }

    // WHAT: one sampler line (runs under seh_run): t, speed, Throttle, bAutopilot, DistanceToTarget, ImpulseInput, WorldImpulse,
    //       nearest rock distance and id (-, none when no rock is readable), actor location.
    // WHY: the nearest rock uses g_rock_actors and the cached rock location (act_aim), so it adds no scan.
    std::wstring probe_sample_text(UObject* ship, int sid, double t_s)
    {
        ActFrame f;
        const bool hasf = act_frame(ship, f);
        double best = -1.0;
        UObject* best_a = nullptr;
        if (hasf)
            for (UObject* a : g_rock_actors)
            {
                ActAim r;
                if (!act_aim(f, a, r)) continue;
                if (!best_a || r.dist_m < best) { best = r.dist_m; best_a = a; }
            }
        int64_t dt_raw = 0;
        const bool ok_dt = pnum_prop(ship, STR("DistanceToTarget"), dt_raw);
        return STR("t=") + probe_f2(t_s) + STR(" spd_mps=") + probe_f2(spd_of(sid).v / g_raw_per_m) +
               STR(" throttle=") + prop_int(ship, STR("Throttle")) + STR(" ap=") + prop_bool(ship, STR("bAutopilot")) +
               STR(" dist_tgt_m=") + (ok_dt ? probe_f2(dt_raw / g_raw_per_m) : std::wstring(STR("n/a"))) +
               STR(" impulse_in=") + pvec_text(ship, STR("ImpulseInput")) +
               STR(" world_impulse=") + pvec_text(ship, STR("WorldImpulse")) +
               STR(" rock_m=") + (best_a ? probe_f2(best) : std::wstring(STR("none"))) +
               STR(" rock_id=") + (best_a ? std::to_wstring(rock_id_of(best_a)) : std::wstring(STR("-"))) +
               STR(" loc_uu=") + (hasf ? probe_v3(f.loc) : std::wstring(STR("n/a")));
    }

    struct ProbeSampleCtx { UObject* ship; int sid; double t; std::wstring* out; bool collide; };
    void probe_sample_fn(void* p)
    {
        auto* c = static_cast<ProbeSampleCtx*>(p);
        *c->out = probe_sample_text(c->ship, c->sid, c->t);
        if (c->collide) *c->out += probe_collide_text(c->ship);
    }

    // ---------------- probe collision box (CollisionTest with a box, MiningProbeAvoid.txt) ----------------
    // WHAT: probe_collision_box:<ship>:<minx>:<miny>:<minz>:<maxx>:<maxy>:<maxz>[:local|world|raw] (metres) calls CollisionTest once with the box
    //       and writes the hits: class, ObjectId, display name, location, distance to the ship centre, and whether it is the ship, its CollisionEnv or other.
    //       probe_collision_boxwatch:<ship>:<seconds>[:local|world] samples two boxes every 0.25 s: all = +-50 m around the ship, fwd = the forward box
    //       (x 0..d, y and z +-15 m, d = 50 * Throttle / 10, at least 25 m). Each sample: hit count and nearest hit (class, id, distance) per box.
    // WHY: see whether the game's own box query reports rocks near our ship, and which frame it reads the box in (the frame is the test: local, world, raw).
    // GAME FACTS: CollisionTest(min FVector, max FVector) -> OutHits (array of objects), ParmsSize 64 (probe dump 2026-11-04). Other shapes are not called.
    // SAFETY: game reads and the call run under seh_run, game thread only. The mod writes no game state. OutHits is engine memory and is not freed
    //       here: a small leak per call (not measured).

    // WHAT: the box call plan: min and max (FVector inputs, in order) and the OutHits array. why = what does not fit.
    struct BoxPlan
    {
        UFunction* fn = nullptr;
        FProperty* lo = nullptr;
        FProperty* hi = nullptr;
        FProperty* out = nullptr;
        std::wstring why;
        bool ok() const { return fn && lo && hi && out; }
    };

    BoxPlan probe_box_plan(UObject* ship)
    {
        BoxPlan pl;
        pl.fn = ship->GetFunctionByNameInChain(STR("CollisionTest"));
        if (!pl.fn) { pl.why = STR("CollisionTest not found"); return pl; }
        std::vector<FProperty*> ins;
        for (FProperty* p : pl.fn->ForEachProperty())
        {
            if (p->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
            if (p->HasAnyPropertyFlags(CPF_OutParm)) pl.out = p;
            else if (p->HasAnyPropertyFlags(CPF_Parm)) ins.push_back(p);
        }
        if (pl.out && !CastField<FArrayProperty>(pl.out)) { pl.out = nullptr; pl.why = STR("out parameter is not an array; "); }
        if (ins.size() != 2 || !pl.out) { pl.why += STR("needs 2 vector inputs and 1 out array, found ") + std::to_wstring(ins.size()) + STR(" inputs"); return pl; }
        if (probe_vec_kind(ins[0]) != STR("uu") || probe_vec_kind(ins[1]) != STR("uu"))
        {
            pl.why = STR("inputs are not FVector (X, Y, Z double or float)");
            return pl;
        }
        pl.lo = ins[0];
        pl.hi = ins[1];
        return pl;
    }

    // WHAT: writes FVector units (uu, no conversion) into the struct at base. Double or float axes.
    void probe_put_uu(uint8_t* base, UStruct* vec, const double uu[3])
    {
        static const TCHAR* const kAxis[3] = {STR("X"), STR("Y"), STR("Z")};
        for (int i = 0; i < 3; ++i)
        {
            FProperty* f = vec->FindProperty(FName(kAxis[i], FNAME_Add));
            uint8_t* d = base + f->GetOffset_Internal();
            if (CastField<FFloatProperty>(f)) *reinterpret_cast<float*>(d) = static_cast<float>(uu[i]);
            else *reinterpret_cast<double*>(d) = uu[i];
        }
    }

    // WHAT: "(x,y,z)" with 2 decimals (metres or uu as given).
    std::wstring probe_v3f(const double v[3]) { return STR("(") + probe_f2(v[0]) + STR(",") + probe_f2(v[1]) + STR(",") + probe_f2(v[2]) + STR(")"); }

    struct BoxCall { std::wstring in_hex, after_hex, results; std::vector<UObject*> hits; };

    // WHAT: one CollisionTest call with the box lo_uu..hi_uu (uu). Reads the OutHits pointers after the call. Callers run it under seh_run.
    BoxCall probe_box_call(UObject* ship, const BoxPlan& pl, const double lo_uu[3], const double hi_uu[3])
    {
        BoxCall r;
        const size_t psz = static_cast<size_t>(pl.fn->GetParmsSize());
        std::vector<uint8_t> buf(psz + 16, 0);
        probe_put_uu(buf.data() + pl.lo->GetOffset_Internal(), CastField<FStructProperty>(pl.lo)->GetStruct(), lo_uu);
        probe_put_uu(buf.data() + pl.hi->GetOffset_Internal(), CastField<FStructProperty>(pl.hi)->GetStruct(), hi_uu);
        r.in_hex = probe_hex(buf.data(), psz);
        ship->ProcessEvent(pl.fn, buf.data());
        r.after_hex = probe_hex(buf.data(), psz);
        r.results = probe_results(pl.fn, buf.data());
        auto* arr = reinterpret_cast<TArray<UObject*>*>(buf.data() + pl.out->GetOffset_Internal());
        const int32_t n = clamp_tarray_num(arr->Num());
        for (int32_t k = 0; k < n; ++k) r.hits.push_back((*arr)[k]);
        return r;
    }

    // WHAT: one hit: class, ObjectId, display name, location text, distance to the ship centre (m, -1 = unknown), owner (ship / env / other).
    struct HitInfo { UObject* o = nullptr; std::wstring cls, id, name, loc, self; double dist = -1.0; };

    // WHAT: reads one hit (runs under seh_run). Location = K2_GetActorLocation (uu); when that fails, the Location property as text.
    HitInfo probe_hit_info(UObject* ship, UObject* env, UObject* h, const double ship_uu[3])
    {
        HitInfo hi;
        hi.o = h;
        if (!alive(h)) { hi.cls = STR("null"); hi.self = STR("-"); return hi; }
        hi.cls = std::wstring(h->GetClassPrivate()->GetName());
        hi.id = std::to_wstring(obj_id(h));
        hi.name = obj_display(h);
        hi.self = h == ship ? std::wstring(STR("ship")) : (alive(env) && h == env) ? std::wstring(STR("env")) : std::wstring(STR("other"));
        double p[3] = {0.0, 0.0, 0.0};
        std::wstring why;
        if (actor_vec_call(h, STR("K2_GetActorLocation"), p, why))
        {
            hi.loc = probe_v3f(p);
            const double d[3] = {p[0] - ship_uu[0], p[1] - ship_uu[1], p[2] - ship_uu[2]};
            hi.dist = norm3(d) / MV_UU_PER_M;
        }
        else hi.loc = pvec_text(h, STR("Location"));
        return hi;
    }

    struct BoxCmdCtx { UObject* ship = nullptr; int sid = 0; int mode = 0; double lo_m[3] = {0, 0, 0}, hi_m[3] = {0, 0, 0}; std::wstring* out = nullptr; bool called = false; size_t count = 0; };

    // WHAT: probe_collision_box dump text (runs under seh_run): the box as typed and sent, the signature, the bytes, the hits (first PROBE_BOX_HIT_LINES).
    void probe_box_cmd_fn(void* p)
    {
        auto* c = static_cast<BoxCmdCtx*>(p);
        std::wstring& s = *c->out;
        ActFrame f;
        const bool hasf = act_frame(c->ship, f);
        const double ship_uu[3] = {f.loc[0], f.loc[1], f.loc[2]};
        s += STR("MiningProbeAvoid collision box ship=") + std::to_wstring(c->sid) + STR(" mode=") + widen(probe_box_mode_name(c->mode)) + STR("\n");
        s += STR("box_m min=") + probe_v3f(c->lo_m) + STR(" max=") + probe_v3f(c->hi_m) + STR("\n");
        s += STR("ship_uu=") + (hasf ? probe_v3f(ship_uu) : std::wstring(STR("n/a"))) + STR(" (K2_GetActorLocation)\n");
        if (c->mode == PROBE_BOX_WORLD && !hasf) { s += STR("not called: ship location unreadable (world mode needs it)\n"); return; }
        const BoxPlan pl = probe_box_plan(c->ship);
        if (!pl.ok()) { s += STR("not called: ") + pl.why + STR("\n"); return; }
        double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        probe_box_uu(c->mode, c->lo_m, c->hi_m, ship_uu, lo, hi);
        s += STR("box_uu sent min=") + probe_v3f(lo) + STR(" max=") + probe_v3f(hi) + STR("\n");
        s += STR("signature: ") + probe_fn_text(pl.fn) + STR("\n");
        const BoxCall r = probe_box_call(c->ship, pl, lo, hi);
        c->called = true;
        c->count = r.hits.size();
        s += STR("input_bytes: ") + r.in_hex + STR("\nafter_bytes: ") + r.after_hex + STR("\nresults: ") + r.results + STR("\n");
        UObject* env = get_obj_prop(c->ship, STR("CollisionEnv"));
        const size_t shown = std::min(r.hits.size(), PROBE_BOX_HIT_LINES);
        s += STR("OutHits count=") + std::to_wstring(r.hits.size()) + STR(" shown=") + std::to_wstring(shown) + STR("\n");
        for (size_t i = 0; i < shown; ++i)
        {
            const HitInfo h = probe_hit_info(c->ship, env, r.hits[i], ship_uu);
            s += STR("  hit=") + std::to_wstring(i) + STR(" class=") + h.cls + STR(" id=") + h.id + STR(" name='") + h.name + STR("' loc_uu=") + h.loc +
                 STR(" dist_m=") + (h.dist < 0.0 ? std::wstring(STR("n/a")) : probe_f2(h.dist)) + STR(" self=") + h.self + STR("\n");
        }
    }

    struct BoxWatchCtx { UObject* ship = nullptr; int sid = 0; double t = 0.0; int mode = 0; std::wstring* out = nullptr; };

    // WHAT: one box of a boxwatch sample (under seh_run): "<tag>_n=<hits> <tag>_near=<class>#<id>@<dist>m" (none when no hit has a distance).
    std::wstring probe_box_watch_part(UObject* ship, UObject* env, const BoxPlan& pl, const double lo_m[3], const double hi_m[3], int mode,
                                      const double ship_uu[3], const wchar_t* tag)
    {
        double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        probe_box_uu(mode, lo_m, hi_m, ship_uu, lo, hi);
        const BoxCall r = probe_box_call(ship, pl, lo, hi);
        HitInfo best;
        for (UObject* h : r.hits)
        {
            const HitInfo cur = probe_hit_info(ship, env, h, ship_uu);
            if (cur.dist >= 0.0 && (best.dist < 0.0 || cur.dist < best.dist)) best = cur;
        }
        const std::wstring t(tag);
        return t + STR("_n=") + std::to_wstring(r.hits.size()) + STR(" ") + t + STR("_near=") +
               (best.dist < 0.0 ? std::wstring(STR("none")) : best.cls + STR("#") + best.id + STR("@") + probe_f2(best.dist) + STR("m"));
    }

    // WHAT: one boxwatch sample (runs under seh_run): time, Throttle, forward length, then the all and fwd boxes.
    void probe_boxwatch_fn(void* p)
    {
        auto* c = static_cast<BoxWatchCtx*>(p);
        std::wstring& s = *c->out;
        ActFrame f;
        const bool hasf = act_frame(c->ship, f);
        const double ship_uu[3] = {f.loc[0], f.loc[1], f.loc[2]};
        int32_t thr = 0;
        get_int_prop(c->ship, STR("Throttle"), thr);
        const BoxPlan pl = probe_box_plan(c->ship);
        if (!pl.ok()) { s = STR("t=") + probe_f2(c->t) + STR(" skip: ") + pl.why; return; }
        if (c->mode == PROBE_BOX_WORLD && !hasf) { s = STR("t=") + probe_f2(c->t) + STR(" skip: ship location unreadable"); return; }
        double all_lo[3], all_hi[3], fwd_lo[3], fwd_hi[3];
        probe_watch_boxes(thr, all_lo, all_hi, fwd_lo, fwd_hi);
        UObject* env = get_obj_prop(c->ship, STR("CollisionEnv"));
        s = STR("t=") + probe_f2(c->t) + STR(" thr=") + std::to_wstring(thr) + STR(" fwd_m=") + probe_f2(probe_box_fwd_m(thr)) + STR(" ") +
            probe_box_watch_part(c->ship, env, pl, all_lo, all_hi, c->mode, ship_uu, STR("all")) + STR(" ") +
            probe_box_watch_part(c->ship, env, pl, fwd_lo, fwd_hi, c->mode, ship_uu, STR("fwd"));
    }

    // WHAT: appends one UTF-8 line to MiningProbeAvoid.txt. false when the file cannot be opened.
    bool probe_append(const std::wstring& line)
    {
        std::ofstream f(probe_path(), std::ios::binary | std::ios::app);
        if (!f) return false;
        f << u8(line) << "\r\n";
        return true;
    }

    // WHAT: ends the watch: writes the end line and clears the state. why = text for the log and file.
    void probe_stop(const std::wstring& why)
    {
        probe_append(STR("end t=") + probe_f2(g_pav.elapsed) + STR(" lines=") + std::to_wstring(g_pav.lines + 1) + STR(" reason=") + why);
        g_pav.active = false;
        log_line(STR("PROBEAVOID watch end ") + why);
    }

    // WHAT: sampler step, called on every poll tick (0.25 s) from cmd_tick. Stops at the asked time, at the line cap (one line kept for the end)
    //       or when the ship is gone. Writes one sample line per call otherwise.
    void probe_avoid_tick(double dt)
    {
        if (!g_pav.active) return;
        const double before = g_pav.elapsed;
        g_pav.elapsed += dt;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), g_pav.sid);
        if (!alive(ship)) { probe_stop(STR("ship gone")); return; }
        if (!probe_watch_more(g_pav.lines, before, g_pav.total)) { probe_stop(STR("time or line cap")); return; }
        std::wstring line;
        bool ok = false;
        if (g_pav.boxw)
        {
            BoxWatchCtx bc{ship, g_pav.sid, g_pav.elapsed, g_pav.bmode, &line};
            ok = seh_run(probe_boxwatch_fn, &bc);
        }
        else
        {
            ProbeSampleCtx c{ship, g_pav.sid, g_pav.elapsed, &line, g_pav.collide};
            ok = seh_run(probe_sample_fn, &c);
        }
        if (!ok) line = STR("fault ") + seh_describe(g_seh);
        probe_append(line);
        ++g_pav.lines;
    }

    // WHAT: arms the sampler (g_pav) and truncates MiningProbeAvoid.txt with a header; note is appended to the header when not empty.
    // bmode >= 0: box watch with that PROBE_BOX_* mode (probe_boxwatch_fn); -1: the one-vector watch.
    std::wstring probe_watch_start(int ws, double secs, bool collide, const std::wstring& note, int bmode = -1)
    {
        if (!alive(find_by_id(STR("DMSPlayerShipObject"), ws))) return STR("error: ship not found");
        g_pav = ProbeWatch{};
        g_pav.active = true;
        g_pav.sid = ws;
        g_pav.total = secs;
        g_pav.lines = 1;
        g_pav.collide = collide;
        g_pav.boxw = bmode >= 0;
        g_pav.bmode = bmode >= 0 ? bmode : PROBE_BOX_LOCAL;
        const std::wstring kind = g_pav.boxw ? STR("collision box ") : collide ? STR("collision ") : STR("");
        std::wstring head = STR("MiningProbeAvoid ") + kind + STR("watch ship=") + std::to_wstring(ws) + STR(" seconds=") + probe_f2(secs);
        if (g_pav.boxw) head += STR(" mode=") + widen(probe_box_mode_name(bmode));
        if (!note.empty()) head += STR(" signature=") + note;
        write_atomic(probe_path(), u8(head) + "\r\n");
        log_line(STR("PROBEAVOID ") + kind + STR("watch start ship=") + std::to_wstring(ws) + STR(" seconds=") + probe_f2(secs));
        return STR("ok watch ship=") + std::to_wstring(ws) + STR(" seconds=") + probe_f2(secs) + STR(" file=MiningProbeAvoid.txt");
    }

    // WHAT: command entry for probe_collision[:<ship>], probe_collision_call:<ship>:<x>:<y>:<z> and probe_collision_watch:<ship>:<seconds>.
    //       Called from probe_avoid_cmd. Returns "ok ..." or "error: ...".
    std::wstring probe_collision_cmd(int sid, const std::string& cmd)
    {
        if (cmd.rfind("probe_collision_boxwatch", 0) == 0)
        {
            int ws = 0, mode = PROBE_BOX_LOCAL;
            double secs = 0.0;
            if (!probe_parse_boxwatch(cmd, ws, secs, mode)) return STR("error: usage probe_collision_boxwatch:<ship>:<seconds>[:local|world]");
            if (!alive(find_by_id(STR("DMSPlayerShipObject"), ws))) return STR("error: ship not found");
            return probe_watch_start(ws, secs, true, STR(""), mode);
        }
        if (cmd.rfind("probe_collision_box", 0) == 0)
        {
            int bs = 0, mode = PROBE_BOX_LOCAL;
            double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
            if (!probe_parse_box(cmd, bs, lo, hi, mode))
                return STR("error: usage probe_collision_box:<ship>:<minx>:<miny>:<minz>:<maxx>:<maxy>:<maxz>[:local|world|raw] (min <= max)");
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), bs);
            if (!alive(ship)) return STR("error: ship not found");
            std::wstring out;
            BoxCmdCtx c;
            c.ship = ship;
            c.sid = bs;
            c.mode = mode;
            std::copy(lo, lo + 3, c.lo_m);
            std::copy(hi, hi + 3, c.hi_m);
            c.out = &out;
            if (!seh_run(probe_box_cmd_fn, &c)) return STR("error: probe fault ") + seh_describe(g_seh);
            write_atomic(probe_path(), u8(out));
            log_line(STR("PROBEAVOID collision box ship=") + std::to_wstring(bs) + STR(" mode=") + widen(probe_box_mode_name(mode)) +
                     STR(" hits=") + std::to_wstring(c.count) + (c.called ? STR("") : STR(" (not called)")));
            if (!c.called) return STR("error: CollisionTest not called, see MiningProbeAvoid.txt");
            return STR("ok MiningProbeAvoid.txt written ship=") + std::to_wstring(bs) + STR(" hits=") + std::to_wstring(c.count);
        }
        if (cmd.rfind("probe_collision_watch", 0) == 0)
        {
            int ws = 0;
            double secs = 0.0;
            if (!probe_parse_cwatch(cmd, ws, secs)) return STR("error: usage probe_collision_watch:<ship>:<seconds>");
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), ws);
            if (!alive(ship)) return STR("error: ship not found");
            std::wstring note;
            CollNoteCtx nc{ship, &note};
            if (!seh_run(probe_coll_note_fn, &nc)) note = STR("fault ") + seh_describe(g_seh);
            return probe_watch_start(ws, secs, true, note);
        }
        if (cmd.rfind("probe_collision_call", 0) == 0)
        {
            int cs = 0;
            double m[3] = {0.0, 0.0, 0.0};
            if (!probe_parse_call(cmd, cs, m)) return STR("error: usage probe_collision_call:<ship>:<x_m>:<y_m>:<z_m>");
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), cs);
            if (!alive(ship)) return STR("error: ship not found");
            std::wstring out;
            CollCallCtx c{ship, cs, m, &out};
            if (!seh_run(probe_coll_call_fn, &c)) return STR("error: probe fault ") + seh_describe(g_seh);
            write_atomic(probe_path(), u8(out));
            log_line(STR("PROBEAVOID collision call ship=") + std::to_wstring(cs));
            return STR("ok MiningProbeAvoid.txt written ship=") + std::to_wstring(cs);
        }
        int ps = sid;
        if (cmd != "probe_collision" && !probe_parse_sid_pfx(cmd, "probe_collision:", ps))
            return STR("error: usage probe_collision[:<ship>], probe_collision_call:<ship>:<x>:<y>:<z> or probe_collision_watch:<ship>:<seconds>");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), ps);
        if (!alive(ship)) return STR("error: ship not found");
        std::wstring out;
        ProbeCollDumpCtx c{ship, ps, &out};
        if (!seh_run(probe_coll_dump_fn, &c)) return STR("error: probe fault ") + seh_describe(g_seh);
        write_atomic(probe_path(), u8(out));
        log_line(STR("PROBEAVOID collision dump written ship=") + std::to_wstring(ps) + STR(" bytes=") + std::to_wstring(out.size()));
        return STR("ok MiningProbeAvoid.txt written ship=") + std::to_wstring(ps);
    }

    // WHAT: command bus entry for probe_avoid, probe_avoid:<ship>, probe_avoid_watch:<ship>:<seconds> and the probe_collision* commands (run_cmd, mb_testcmd.hpp).
    //       A bare probe_avoid uses the line's ship. Returns "ok ..." or "error: ...".
    // WHY: one dump per command (overwrites the file); a watch truncates the file and then appends samples.
    std::wstring probe_avoid_cmd(int sid, const std::string& cmd)
    {
        if (cmd.rfind("probe_collision", 0) == 0) return probe_collision_cmd(sid, cmd);
        if (cmd.rfind("probe_avoid_watch", 0) == 0)
        {
            int ws = 0;
            double secs = 0.0;
            if (!probe_parse_watch(cmd, ws, secs)) return STR("error: usage probe_avoid_watch:<ship>:<seconds>");
            return probe_watch_start(ws, secs, false, STR(""));
        }
        int ps = sid;
        if (cmd != "probe_avoid" && !probe_parse_sid(cmd, ps)) return STR("error: usage probe_avoid[:<ship>] or probe_avoid_watch:<ship>:<seconds>");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), ps);
        if (!alive(ship)) return STR("error: ship not found");
        UObject* fm = get_obj_prop(ship, STR("FlightModel"));
        std::wstring out;
        ProbeDumpCtx c{ship, fm, ps, &out};
        if (!seh_run(probe_dump_fn, &c)) return STR("error: probe fault ") + seh_describe(g_seh);
        write_atomic(probe_path(), u8(out));
        log_line(STR("PROBEAVOID dump written ship=") + std::to_wstring(ps) + STR(" bytes=") + std::to_wstring(out.size()));
        return STR("ok MiningProbeAvoid.txt written ship=") + std::to_wstring(ps);
    }

    // ---------------- probe power (MiningProbePower.txt) ----------------
    // WHAT: probe_power[:<ship>] writes MiningProbePower.txt, read only, under seh_run: the ship and FlightModel properties whose name matches
    //       power_name_match (type, owner class, value); Thrusters / ThrusterIDs / StartingFuelItems; the fuel items (name, inventory total);
    //       the occupied Inventory.ItemSlots (item, amount); the functions with power words and their parameters; the properties of objects
    //       reachable from the ship by one property whose class or name matches power_obj_match / power_name_match.
    // WHY: the low-power names (bInLowPowerMode, bLowPower, PowerGenRate, PowerDraw, ...) come from the exe strings only. This shows which exist.
    // SAFETY: every game read runs under seh_run. Game thread only (run_cmd, Test tab / Dev Menu).
    struct ProbePowerCtx { UObject* ship; int sid; std::wstring* out; };

    // WHAT: power-named properties of o over its class chain: name [type] owner=<class> = value (probe_val).
    void probe_power_props(std::wstring& s, UObject* o, const TCHAR* label)
    {
        s += STR("  ") + std::wstring(label) + STR(" class=") + std::wstring(o->GetClassPrivate()->GetName()) + STR("\n");
        for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
            for (FProperty* p : st->ForEachProperty())
            {
                const std::wstring n = p->GetName();
                if (power_name_match(n)) s += STR("  ") + n + STR(" [") + type_text(p) + STR("] owner=") + std::wstring(st->GetName()) + STR(" = ") + probe_val(o, p, n) + STR("\n");
            }
    }

    // WHAT: one array property: count, then the element values (object class names via prop_classes, int32 / int64 values; 40 at most), else the count only.
    void probe_array(std::wstring& s, UObject* o, const TCHAR* name)
    {
        FProperty* p = o->GetPropertyByNameInChain(name);
        auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
        if (!ap) { probe_kv(s, name, p ? STR("not an array: ") + type_text(p) : std::wstring(STR("absent"))); return; }
        FProperty* inner = ap->GetInner();
        if (CastField<FObjectProperty>(inner)) { probe_kv(s, name, prop_classes(o, name)); return; }
        std::wstring vals;
        if (CastField<FIntProperty>(inner))
        {
            auto* a = o->GetValuePtrByPropertyNameInChain<TArray<int32_t>>(name);
            for (int32_t i = 0; a && i < a->Num() && i < 40; ++i) vals += std::to_wstring((*a)[i]) + STR(" ");
        }
        else if (CastField<FInt64Property>(inner))
        {
            auto* a = o->GetValuePtrByPropertyNameInChain<TArray<int64_t>>(name);
            for (int32_t i = 0; a && i < a->Num() && i < 40; ++i) vals += std::to_wstring((*a)[i]) + STR(" ");
        }
        probe_kv(s, name, prop_count(o, name) + STR(" elements ") + vals);
    }

    // WHAT: ItemSlots of holder (the ship Inventory, or a generator / thruster component), one line per occupied slot (slot index, item name,
    //       Amount). 60 lines at most. Same struct walk as cargo_pct.
    void probe_slots(std::wstring& s, UObject* inv)
    {
        constexpr int kSlotLines = 60;
        FProperty* tp = alive(inv) ? inv->GetPropertyByNameInChain(STR("ItemSlots")) : nullptr;
        auto* ap = tp ? CastField<FArrayProperty>(tp) : nullptr;
        auto* arr = ap ? inv->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("ItemSlots")) : nullptr;
        auto* isp = ap && ap->GetInner() ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
        FProperty* fi = isp && isp->GetStruct() ? isp->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add)) : nullptr;
        FProperty* fa = isp && isp->GetStruct() ? isp->GetStruct()->FindProperty(FName(STR("Amount"), FNAME_Add)) : nullptr;
        if (!arr || !fi || !fa || ap->GetInner()->GetElementSize() <= 0) { s += STR("  ItemSlots = n/a\n"); return; }
        const int32_t stride = ap->GetInner()->GetElementSize();
        int shown = 0;
        for (int32_t i = 0; i < arr->Num() && shown < kSlotLines; ++i)
        {
            const uint8_t* e = &(*arr)[0] + static_cast<size_t>(i) * static_cast<size_t>(stride);
            const int32_t amt = *reinterpret_cast<const int32_t*>(e + fa->GetOffset_Internal());
            if (amt <= 0) continue;
            UObject* it = *reinterpret_cast<UObject* const*>(e + fi->GetOffset_Internal());
            s += STR("  slot ") + std::to_wstring(i) + STR(" item=") + (alive(it) ? obj_display(it) : std::wstring(STR("none"))) + STR(" amount=") + std::to_wstring(amt) + STR("\n");
            ++shown;
        }
    }

    // WHAT: functions of o's class chain whose name matches power_name_match, with owner class and the parameter names and types.
    void probe_power_funcs(std::wstring& s, UObject* o)
    {
        for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
        {
            UClass* sc = Cast<UClass>(st);
            if (!sc) continue;
            for (UFunction* f : sc->ForEachFunction())
            {
                const std::wstring n = f->GetName();
                if (!power_name_match(n)) continue;
                std::wstring sig;
                for (FProperty* pp : f->ForEachProperty()) sig += pp->GetName() + STR(":") + type_text(pp) + STR(" ");
                s += STR("  func ") + n + STR(" owner=") + std::wstring(sc->GetName()) + STR(" params=(") + sig + STR(")\n");
            }
        }
    }

    // WHAT: every property of o over its class chain: name [type] owner=<class> = value. Lines whose name matches power_highlight_match start with "** ".
    void probe_obj_all(std::wstring& s, UObject* o)
    {
        for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
            for (FProperty* p : st->ForEachProperty())
            {
                const std::wstring n = p->GetName();
                s += power_highlight_match(n) ? STR("  ** ") : STR("  ");
                s += n + STR(" [") + type_text(p) + STR("] owner=") + std::wstring(st->GetName()) + STR(" = ") + probe_val(o, p, n) + STR("\n");
            }
    }

    // WHAT: LogisticFilters rows (pf_read: item name, amount, action LIMIT/BUFFER) and the field list of the filter struct (name, type, offset).
    void probe_filters(std::wstring& s, UObject* ship)
    {
        std::vector<PfRow> rows;
        if (!pf_read(ship, rows)) s += STR("  LogisticFilters unreadable (pf_read)\n");
        for (const PfRow& r : rows)
            s += STR("  row item=") + (alive(r.item) ? obj_display(r.item) : std::wstring(STR("none"))) + STR(" amount=") + std::to_wstring(r.amount) +
                 STR(" action=") + (r.action == 0 ? std::wstring(STR("LIMIT")) : r.action == 1 ? std::wstring(STR("BUFFER")) : std::to_wstring(r.action)) + STR("\n");
        FProperty* fp = ship->GetPropertyByNameInChain(STR("LogisticFilters"));
        auto* ap = fp ? CastField<FArrayProperty>(fp) : nullptr;
        auto* isp = ap && ap->GetInner() ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
        if (!isp || !isp->GetStruct()) { s += STR("  filter struct: n/a\n"); return; }
        s += STR("  filter struct fields:\n");
        for (FProperty* q : isp->GetStruct()->ForEachProperty())
            s += STR("    ") + std::wstring(q->GetName()) + STR(" [") + type_text(q) + STR("] offset=") + std::to_wstring(q->GetOffset_Internal()) + STR("\n");
    }

    // WHAT: power numbers: PowerGenRate, PowerDraw, ThrusterPowerDraw (ship, else FlightModel) and every ship/FlightModel property named like a buffer
    //       (fuel, buffer, stored, reserve, energy) with its value.
    void probe_power_numbers(std::wstring& s, UObject* ship)
    {
        UObject* fm = get_obj_prop(ship, STR("FlightModel"));
        for (const TCHAR* n : {STR("PowerGenRate"), STR("PowerDraw"), STR("ThrusterPowerDraw")})
            s += STR("  ") + std::wstring(n) + STR("=") + power_field(ship, n) + STR("\n");
        for (UObject* o : {ship, alive(fm) ? fm : ship})
            for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
                for (FProperty* p : st->ForEachProperty())
                {
                    const std::wstring n = p->GetName();
                    if (power_buffer_match(n)) s += STR("  buffer ") + n + STR(" [") + type_text(p) + STR("] = ") + probe_val(o, p, n) + STR("\n");
                }
    }

    // WHAT: objects reachable from o by one property (ObjectProperty, or ArrayProperty of objects) whose class matches power_obj_match or whose
    //       name matches power_name_match: all their properties (probe_dump_props). 12 objects at most.
    void probe_power_comps(std::wstring& s, UObject* o)
    {
        int found = 0;
        for (UStruct* st = o->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
            for (FProperty* p : st->ForEachProperty())
            {
                uint8_t* base = reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal();
                std::vector<UObject*> objs;
                if (CastField<FObjectProperty>(p)) objs.push_back(*reinterpret_cast<UObject**>(base));
                else if (auto* ap = CastField<FArrayProperty>(p))
                {
                    if (CastField<FObjectProperty>(ap->GetInner()))
                    {
                        auto* arr = reinterpret_cast<TArray<UObject*>*>(base);
                        for (int32_t k = 0; k < arr->Num(); ++k) objs.push_back((*arr)[k]);
                    }
                }
                for (UObject* x : objs)
                {
                    if (!alive(x) || x == o || found >= 12) continue;
                    const std::wstring cls = std::wstring(x->GetClassPrivate()->GetName());
                    const std::wstring nm = std::wstring(x->GetName());
                    if (!power_obj_match(cls) && !power_name_match(nm)) continue;
                    ++found;
                    s += STR("  component ") + nm + STR(" class=") + cls + STR(" via ") + std::wstring(p->GetName()) + STR("\n");
                    probe_obj_all(s, x);
                    s += STR("  slots of component:\n");
                    probe_slots(s, x);
                }
            }
        if (found == 0) s += STR("  none\n");
    }

    // WHAT: the probe_power dump text (runs under seh_run from probe_power_cmd).
    void probe_power_fn(void* p)
    {
        auto* c = static_cast<ProbePowerCtx*>(p);
        std::wstring& s = *c->out;
        UObject* ship = c->ship;
        UObject* fm = get_obj_prop(ship, STR("FlightModel"));
        s += STR("MiningProbePower ship=") + std::to_wstring(c->sid) + STR("\n[ship power properties]\n");
        probe_power_props(s, ship, STR("ship"));
        s += STR("[flight model power properties]\n");
        if (alive(fm)) probe_power_props(s, fm, STR("flight model")); else s += STR("  n/a\n");
        s += STR("[thruster and fuel fields]\n");
        probe_array(s, ship, STR("Thrusters"));
        probe_array(s, ship, STR("ThrusterIDs"));
        probe_array(s, ship, STR("StartingFuelItems"));
        s += STR("[fuel items: name, inventory total]\n");
        for (int i = 0; i < N_FUEL; ++i)
        {
            std::wstring w;
            UObject* it = item_c(FUEL_ITEM[i], w);
            s += STR("  ") + widen(FUEL_KEY[i]) + STR(" item=") + (alive(it) ? obj_display(it) : std::wstring(STR("n/a"))) +
                 STR(" total=") + (alive(it) ? i64w(inv_total(ship, it)) : std::wstring(STR("n/a"))) + STR("\n");
        }
        s += STR("[power numbers and stored buffers]\n");
        probe_power_numbers(s, ship);
        s += STR("[inventory ItemSlots, occupied slots (cargo)]\n");
        probe_slots(s, get_obj_prop(ship, STR("Inventory")));
        s += STR("[logistic filters (auto pull / buffer rows)]\n");
        probe_filters(s, ship);
        s += STR("[power functions, owner class chain]\n");
        probe_power_funcs(s, ship);
        if (alive(fm)) probe_power_funcs(s, fm);
        s += STR("[reachable component objects (class or name: power, generator, reactor, battery, thruster, fuel); ** = pull/auto/feed/buffer/mode/enabled/active]\n");
        probe_power_comps(s, ship);
    }

    // ---------------- probe power watch (MiningProbePower.txt) ----------------
    // WHAT: probe_power_watch:<ship>:<seconds> (<= PROBE_WATCH_MAX_S) appends one line per second: low-power flag, PowerGenRate, PowerDraw,
    //       the stored buffers and the cargo fuel totals (power_watch_line). Runs in the mining tick (probe_power_tick), read only.
    // WHY: shows the fuel drain live while a ship flies (generator from cargo or from its own slot).
    struct PowerWatch { bool active = false; int sid = 0; double elapsed = 0.0, total = 0.0, acc = 0.0; int lines = 0; std::string text; };
    PowerWatch g_pw;
    struct PowerWatchCtx { UObject* ship; double t; std::wstring* out; };

    // WHAT: one watch sample (runs under seh_run): low-power flag, PowerGenRate, PowerDraw, buffers, cargo fuel totals.
    void power_watch_fn(void* p)
    {
        auto* c = static_cast<PowerWatchCtx*>(p);
        bool lp = false;
        bool known = false;
        lp = power_dead(c->ship, power_grid(c->ship), known);   // no power on the grid (flag fallback)
        std::wstring buf;
        for (UStruct* st = c->ship->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
            for (FProperty* pr : st->ForEachProperty())
            {
                const std::wstring n = pr->GetName();
                if (power_buffer_match(n)) buf += n + STR("=") + probe_val(c->ship, pr, n) + STR(" ");
            }
        std::wstring cargo;
        for (int i = 0; i < N_FUEL; ++i)
        {
            std::wstring w;
            UObject* it = item_c(FUEL_ITEM[i], w);
            cargo += widen(FUEL_KEY[i]) + STR("=") + (alive(it) ? i64w(inv_total(c->ship, it)) : std::wstring(STR("n/a"))) + STR(" ");
        }
        *c->out = power_watch_line(c->t, known, lp, power_field(c->ship, STR("PowerGenRate")), power_field(c->ship, STR("PowerDraw")),
                                   buf.empty() ? std::wstring(STR("none")) : buf, cargo);
    }

    // WHAT: appends one line to the watch text and rewrites MiningProbePower.txt (at most 125 lines). Ends the watch at the time asked or the cap.
    void power_watch_write(const std::wstring& line)
    {
        g_pw.text += u8(line) + "\r\n";
        ++g_pw.lines;
        write_atomic(get_dll_dir() / "MiningProbePower.txt", g_pw.text);
    }

    // WHAT: mining tick hook (run_cmd driver, mb_testcmd.hpp): one watch sample per second while a probe_power_watch runs.
    void probe_power_tick(double dt)
    {
        if (!g_pw.active) return;
        g_pw.elapsed += dt;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), g_pw.sid);
        if (!alive(ship)) { power_watch_write(STR("ship gone")); g_pw.active = false; return; }
        if (!power_watch_due(g_pw.acc, dt)) return;
        std::wstring line;
        PowerWatchCtx c{ship, g_pw.elapsed, &line};
        if (!seh_run(power_watch_fn, &c)) line = STR("fault ") + seh_describe(g_seh);
        power_watch_write(line);
        if (g_pw.elapsed >= g_pw.total || g_pw.lines >= 125) { power_watch_write(STR("watch end")); g_pw.active = false; }
    }

    // WHAT: command entry for probe_power_watch:<ship>:<seconds>. Truncates MiningProbePower.txt and arms the watch. Returns "ok ..." or "error: ...".
    std::wstring probe_power_watch_cmd(const std::string& cmd)
    {
        int ws = 0;
        double secs = 0.0;
        if (!probe_parse_watch_pfx(cmd, "probe_power_watch:", ws, secs)) return STR("error: usage probe_power_watch:<ship>:<seconds> (max 120)");
        if (!alive(find_by_id(STR("DMSPlayerShipObject"), ws))) return STR("error: ship not found");
        g_pw = PowerWatch{};
        g_pw.active = true;
        g_pw.sid = ws;
        g_pw.total = secs;
        g_pw.text = u8(STR("MiningProbePower watch ship=") + std::to_wstring(ws) + STR(" seconds=") + probe_f2(secs) + STR(" (1 s samples)")) + "\r\n";
        write_atomic(get_dll_dir() / "MiningProbePower.txt", g_pw.text);
        log_line(STR("PROBEPOWER watch start ship=") + std::to_wstring(ws) + STR(" seconds=") + probe_f2(secs));
        return STR("ok watch ship=") + std::to_wstring(ws) + STR(" seconds=") + probe_f2(secs) + STR(" file=MiningProbePower.txt");
    }

    // WHAT: command entry for probe_power and probe_power:<ship> (run_cmd, mb_testcmd.hpp). Returns "ok ..." or "error: ...".
    std::wstring probe_power_cmd(int sid, const std::string& cmd)
    {
        if (cmd.rfind("probe_power_watch", 0) == 0) return probe_power_watch_cmd(cmd);
        int ps = sid;
        if (cmd != "probe_power" && !probe_parse_sid_pfx(cmd, "probe_power:", ps)) return STR("error: usage probe_power[:<ship>]");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), ps);
        if (!alive(ship)) return STR("error: ship not found");
        std::wstring out;
        ProbePowerCtx c{ship, ps, &out};
        if (!seh_run(probe_power_fn, &c)) return STR("error: probe fault ") + seh_describe(g_seh);
        write_atomic(get_dll_dir() / "MiningProbePower.txt", u8(out));
        log_line(STR("PROBEPOWER dump written ship=") + std::to_wstring(ps) + STR(" bytes=") + std::to_wstring(out.size()));
        return STR("ok MiningProbePower.txt written ship=") + std::to_wstring(ps);
    }

    // ---------------- probe reactor (MiningProbeReactor.txt) ----------------
    // WHAT: probe_reactor[:<ship>] writes MiningProbeReactor.txt, read only, under seh_run: each reactor of the ship (ship.Reactors) with all its
    //       properties, the functions of the reactor class chain with signatures (probe_fn_text), and every UClass of /Script/MainSequence whose
    //       name ends with Input (the AddInputCommand inputs) with its functions and parameters.
    // WHY: bAutoInputFuel is set by a direct write (reactor_auto_input) until the game's own input route is found here. The factory that sets
    //      a machine / reactor flag is the route: its receiver and parameters are in the input class listing.
    struct ProbeReactorCtx { UObject* ship; int sid; std::wstring* out; };

    void probe_reactor_fn(void* p)
    {
        auto* c = static_cast<ProbeReactorCtx*>(p);
        std::wstring& s = *c->out;
        const std::vector<UObject*> rs = reactors_of(c->ship);
        s += STR("MiningProbeReactor ship=") + std::to_wstring(c->sid) + STR(" reactors=") + std::to_wstring(rs.size()) + STR("\n[reactors: properties]\n");
        for (size_t i = 0; i < rs.size() && i < 4; ++i)
        {
            UObject* r = rs[i];
            s += STR("reactor ") + std::to_wstring(i) + STR(" id=") + std::to_wstring(obj_id(r)) + STR(" class=") + std::wstring(r->GetClassPrivate()->GetName()) + STR("\n");
            probe_dump_props(s, r);
        }
        s += STR("[reactor class chain: functions]\n");
        if (!rs.empty())
            for (UStruct* st = rs[0]->GetClassPrivate(); st && st->GetName() != STR("Object"); st = st->GetSuperStruct())
            {
                UClass* sc = Cast<UClass>(st);
                if (!sc) continue;
                s += STR("  class ") + std::wstring(sc->GetName()) + STR("\n");
                for (UFunction* f : sc->ForEachFunction()) s += STR("    func ") + probe_fn_text(f) + STR("\n");
            }
        s += STR("[input classes /Script/MainSequence *Input: functions and parameters]\n");
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("Class"), all);
        int n = 0;
        for (UObject* o : all)
        {
            if (!alive(o)) continue;
            const std::wstring name = std::wstring(o->GetName());
            if (name.size() < 5 || name.compare(name.size() - 5, 5, STR("Input")) != 0) continue;
            UClass* cls = Cast<UClass>(o);
            if (!cls) continue;
            UClass* pkg = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (std::wstring(STR("/Script/MainSequence.")) + name).c_str());
            if (pkg != cls) continue;
            if (++n > 150) break;
            s += STR("  input ") + name + STR("\n");
            for (UFunction* f : cls->ForEachFunction()) s += STR("    func ") + probe_fn_text(f) + STR("\n");
        }
        s += STR("  input classes listed: ") + std::to_wstring(n) + STR("\n");
    }

    // WHAT: command entry for probe_reactor and probe_reactor:<ship> (run_cmd, mb_testcmd.hpp). Returns "ok ..." or "error: ...".
    std::wstring probe_reactor_cmd(int sid, const std::string& cmd)
    {
        int ps = sid;
        if (cmd != "probe_reactor" && !probe_parse_sid_pfx(cmd, "probe_reactor:", ps)) return STR("error: usage probe_reactor[:<ship>]");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), ps);
        if (!alive(ship)) return STR("error: ship not found");
        std::wstring out;
        ProbeReactorCtx c{ship, ps, &out};
        if (!seh_run(probe_reactor_fn, &c)) return STR("error: probe fault ") + seh_describe(g_seh);
        write_atomic(get_dll_dir() / "MiningProbeReactor.txt", u8(out));
        log_line(STR("PROBEREACTOR dump written ship=") + std::to_wstring(ps) + STR(" bytes=") + std::to_wstring(out.size()));
        return STR("ok MiningProbeReactor.txt written ship=") + std::to_wstring(ps);
    }
}
