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
            if (id <= 0) continue;
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

    // WHAT: adds the collected items to the cargo of the player's ships: the piloted ship first, then every other ship that has an Inventory,
    //       Inventory.AddItems per item kind with the amount still left. Returns the units that went in.
    // WHY: a new crate cannot be created from a mod (the sim creates them). One ship hold took only 6519 of 21600 units, so the rest goes to the other ships;
    //      what still does not fit is lost (logged).
    int64_t station_stash_to_ships(std::vector<StashItem>& acc)
    {
        std::vector<UObject*> ships;
        const int cur = current_ship_id();
        if (cur > 0) if (UObject* c = find_by_id(STR("DMSPlayerShipObject"), cur)) if (alive(c)) ships.push_back(c);
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), all);
        for (UObject* o : all) if (alive(o) && std::find(ships.begin(), ships.end(), o) == ships.end()) ships.push_back(o);
        int64_t put_total = 0;
        for (UObject* ship : ships)
        {
            UObject* inv = get_obj_prop(ship, STR("Inventory"));
            UFunction* add = alive(inv) ? inv->GetFunctionByNameInChain(STR("AddItems")) : nullptr;
            if (!add) continue;
            int64_t here = 0;
            for (auto& si : acc)
            {
                if (si.amount <= 0 || si.raw.size() < static_cast<size_t>(si.off_amount) + 4) continue;
                std::vector<uint8_t> raw = si.raw;
                const int32_t want = static_cast<int32_t>(std::min<int64_t>(si.amount, 2000000000));
                memcpy(raw.data() + si.off_amount, &want, 4);
                std::vector<uint8_t> b(add->GetParmsSize() + 16, 0);
                if (!put(add, b, STR("ItemsToAdd"), raw.data(), raw.size())) continue;
                inv->ProcessEvent(add, b.data());
                int32_t rv = 0;
                if (FProperty* rp = add->FindProperty(FName(STR("ReturnValue"), FNAME_Add))) rv = *reinterpret_cast<int32_t*>(b.data() + rp->GetOffset_Internal());
                si.amount -= rv; here += rv;
            }
            put_total += here;
            log_line(STR("DEVSTATION stash ship ") + std::to_wstring(obj_id(ship)) + STR(" took ") + std::to_wstring(here));
        }
        int64_t lost = 0;
        for (auto& si : acc) lost += std::max<int64_t>(si.amount, 0);
        log_line(STR("DEVSTATION stash total added=") + std::to_wstring(put_total) + STR(" lost=") + std::to_wstring(lost));
        return put_total;
    }

    // WHAT: one destroy pass. Machines first (a block with machines attached cannot be removed), then the construction objects,
    //       then the platforms; after 20 s with nothing sent, or still present, a mass disassemble of the station id itself.
    //       Returns true when the station object is gone.
    // WHY: the first run showed PlatformIDs = 0 on every player station: the parts live in MachineIDs / ConstructionObjectIDs.
    //      INFERRED that disassembly is a queued task, so the pass repeats (station_destroy_tick) until the station disappears.
    bool station_step()
    {
        UObject* st = find_by_id(STR("StationObject"), g_sd.id);
        if (!alive(st)) return true;
        const std::vector<int> machines = station_ids(st, STR("MachineIDs"));
        const std::vector<int> cons = station_ids(st, STR("ConstructionObjectIDs"));
        const std::vector<int> plats = station_ids(st, STR("PlatformIDs"));
        const std::vector<int>* pick = !machines.empty() ? &machines : (!cons.empty() ? &cons : &plats);
        std::wstring err, first;
        int sent = 0, fail = 0;
        if (!pick->empty() && !g_sd.diag)
        {
            // once: what are the machines that did not go away? class histogram + inventory id of a sample of each class
            g_sd.diag = true;
            std::set<int> want(pick->begin(), pick->end());
            std::map<std::wstring, int> count;
            std::map<std::wstring, std::wstring> sample;
            std::vector<UObject*> all;
            UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
            for (UObject* o : all)
            {
                if (!alive(o)) continue;
                const int oid = obj_id(o);
                if (!want.count(oid)) continue;
                const std::wstring cn = o->GetClassPrivate()->GetName();
                if (++count[cn] == 1)
                {
                    int32_t inv = 0, parent = 0;
                    get_int_prop(o, STR("InventoryID"), inv);
                    get_int_prop(o, STR("ParentID"), parent);
                    sample[cn] = STR("id=") + std::to_wstring(oid) + STR(" InventoryID=") + std::to_wstring(inv) + STR(" ParentID=") + std::to_wstring(parent);
                }
            }
            for (auto& kv : count) log_line(STR("DEVSTATION remaining class ") + kv.first + STR(" x") + std::to_wstring(kv.second) + STR(" sample ") + sample[kv.first]);
            // per class: ask the game itself (CanPlayerDisassemble), and look into the sample's inventory object (array sizes)
            std::map<int, UObject*> byid;
            for (UObject* o : all) if (alive(o)) byid[obj_id(o)] = o;
            // ---- STASH PROBE (log only, nothing is changed here): what could be moved into one crate at the player ----
            {
                std::map<std::wstring, int> sig_done;
                UObject* sample_inv = nullptr;
                UObject* crate = nullptr;
                int n_inv = 0, n_elem = 0;
                std::set<std::wstring> distinct;
                for (int id : *pick)
                {
                    auto it = byid.find(id);
                    if (it == byid.end() || !alive(it->second)) continue;
                    int32_t iid = 0;
                    if (!get_int_prop(it->second, STR("InventoryID"), iid) || iid <= 0) continue;
                    auto iv = byid.find(iid);
                    if (iv == byid.end() || !alive(iv->second)) continue;
                    if (!crate && it->second->GetClassPrivate()->GetName() == STR("BP_ItemCrateObject_C")) crate = it->second;
                    ++n_inv;
                    if (!sample_inv) sample_inv = iv->second;
                    FProperty* tp = iv->second->GetPropertyByNameInChain(STR("ItemTotals"));
                    auto* ap = tp ? CastField<FArrayProperty>(tp) : nullptr;
                    auto* ta = ap ? iv->second->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("ItemTotals")) : nullptr;
                    if (!ap || !ta) continue;
                    auto* sp = CastField<FStructProperty>(ap->GetInner());
                    for (int i = 0; sp && i < ta->Num(); ++i)
                    {
                        ++n_elem;
                        const uint8_t* e = reinterpret_cast<uint8_t*>(ta->GetData()) + static_cast<size_t>(i) * ap->GetInner()->GetElementSize();
                        if (distinct.size() < 6) distinct.insert(dump_struct(e, sp->GetStruct()));
                    }
                }
                log_line(STR("DEVSTATION stash: machines_with_inventory=") + std::to_wstring(n_inv) + STR(" ItemTotals_elements=") + std::to_wstring(n_elem));
                for (auto& d : distinct) log_line(STR("DEVSTATION stash element: ") + d);
                if (sample_inv)
                    for (const TCHAR* fn : {STR("SetInventorySize"), STR("AddItems"), STR("RemoveItems"), STR("AddItemsToSlot")})
                        if (UFunction* f = sample_inv->GetFunctionByNameInChain(fn))
                        {
                            std::wstring sig = std::wstring(fn) + STR("(");
                            for (FProperty* pp : f->ForEachProperty()) sig += pp->GetName() + STR(":") + type_text(pp) + STR(" ");
                            log_line(STR("DEVSTATION stash signature ") + sig + STR(")"));
                        }
                if (crate)
                {
                    log_line(STR("DEVSTATION stash crate id=") + std::to_wstring(obj_id(crate)) + STR(" Location=") + pvec_text(crate, STR("Location")) + STR(" ChunkLocation=") + pvec_text(crate, STR("ChunkLocation")));
                    station_class_dump(crate);
                }
                if (UObject* po = first_of(STR("DMSPlayerObject")))
                {
                    log_line(STR("DEVSTATION stash player object id=") + std::to_wstring(obj_id(po)) + STR(" Location=") + pvec_text(po, STR("Location")) + STR(" ChunkLocation=") + pvec_text(po, STR("ChunkLocation")) + STR(" CurrentChunk=") + pvec_text(po, STR("CurrentChunk")));
                    station_class_dump(po);
                }
                if (UObject* ch = first_of(STR("DMSCharacter")))
                    log_line(STR("DEVSTATION stash character LocalLocation=") + pvec_text(ch, STR("LocalLocation")) + STR(" PredictedLocation=") + pvec_text(ch, STR("PredictedLocation")));
            }
            int cleared = 0, with_inv = 0;
            std::vector<StashItem> acc;
            for (int id : *pick)
            {
                auto it = byid.find(id);
                if (it == byid.end() || !alive(it->second)) continue;
                int32_t iid = 0;
                if (!get_int_prop(it->second, STR("InventoryID"), iid) || iid <= 0) continue;
                auto iv = byid.find(iid);
                if (iv == byid.end()) continue;
                ++with_inv;
                cleared += station_drain_inventory(iv->second, acc);
            }
            log_line(STR("DEVSTATION drained inventories: machines_with_inventory=") + std::to_wstring(with_inv) + STR(" stacks_removed=") + std::to_wstring(cleared) + STR(" item_kinds=") + std::to_wstring(acc.size()));
            station_stash_to_ships(acc);
            std::set<std::wstring> seen;
            for (int id : *pick)
            {
                auto it = byid.find(id);
                if (it == byid.end() || !alive(it->second)) continue;
                UObject* o = it->second;
                const std::wstring cn = o->GetClassPrivate()->GetName();
                if (!seen.insert(cn).second) continue;
                std::wstring can = STR("?");
                if (UFunction* f = o->GetFunctionByNameInChain(STR("CanPlayerDisassemble")))
                {
                    std::vector<uint8_t> b(f->GetParmsSize() + 16, 0);
                    o->ProcessEvent(f, b.data());
                    if (FProperty* rp = f->FindProperty(FName(STR("ReturnValue"), FNAME_Add))) can = b[rp->GetOffset_Internal()] ? STR("true") : STR("false");
                }
                bool bpd = false; get_bool_prop(o, STR("bPlayerDisassembly"), bpd);
                int32_t inv = 0; get_int_prop(o, STR("InventoryID"), inv);
                std::wstring invinfo;
                auto iv = byid.find(inv);
                if (inv > 0 && iv != byid.end() && alive(iv->second))
                {
                    UObject* io = iv->second;
                    station_class_dump(io);
                    invinfo = STR(" inv_class=") + io->GetClassPrivate()->GetName() + STR(" arrays:");
                    for (UStruct* sc = io->GetClassPrivate(); sc && sc->GetName() != STR("Object"); sc = sc->GetSuperStruct())
                        for (FProperty* pp : sc->ForEachProperty())
                            if (CastField<FArrayProperty>(pp))
                            {
                                auto* ta = io->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(pp->GetName().c_str());
                                invinfo += STR(" ") + pp->GetName() + STR("=") + std::to_wstring(ta ? ta->Num() : -1);
                            }
                }
                log_line(STR("DEVSTATION probe ") + cn + STR(" CanPlayerDisassemble=") + can + STR(" bPlayerDisassembly=") + (bpd ? STR("1") : STR("0")) + invinfo);
            }
        }
        for (int id : *pick)
        {
            if (sent + fail < 3) first += std::to_wstring(id) + STR(" ");
            if (station_disassemble(id, err, false)) ++sent; else ++fail;
        }
        if (pick->empty() || g_sd.t > 20.0)
        {
            if (station_disassemble(g_sd.id, err, true)) ++sent; else ++fail;   // mass disassemble of the whole station
        }
        log_line(STR("DEVSTATION step station=") + std::to_wstring(g_sd.id) + STR(" t=") + std::to_wstring(static_cast<int>(g_sd.t)) +
                 STR("s machines=") + std::to_wstring(machines.size()) + STR(" construction=") + std::to_wstring(cons.size()) +
                 STR(" platforms=") + std::to_wstring(plats.size()) + STR(" first_ids=") + first +
                 STR(" commands_sent=") + std::to_wstring(sent) + STR(" failed=") + std::to_wstring(fail) + (err.empty() ? std::wstring() : STR(" err=") + err));
        if (fail > 0 && sent == 0) { g_sd.active = false; station_result(g_sd.id, "error", err); }
        return false;
    }

    // WHAT: runs from cmd_tick every 0.25 s (dt = poll step). Every 2 s it does a pass until the station is gone or 30 s passed. Rewrites the list file at the end.
    void station_destroy_tick(double dt)
    {
        if (!g_sd.active) return;
        g_sd.t += dt;
        g_sd.next -= dt;
        if (g_sd.next > 0.0) return;
        g_sd.next = 2.0;
        if (g_sd.t > 30.0)
        {
            g_sd.active = false;
            log_line(STR("DEVSTATION timeout station=") + std::to_wstring(g_sd.id) + STR(" still present after 30 s"));
            {
                UObject* left = find_by_id(STR("StationObject"), g_sd.id);
                const size_t n = alive(left) ? station_ids(left, STR("MachineIDs")).size() : 0;
                station_result(g_sd.id, "error", STR("30 s: items moved to your ships, ") + std::to_wstring(n) + STR(" parts still stand - remove them by hand"));
            }
            stations_write();
            return;
        }
        if (station_step())
        {
            g_sd.active = false;
            log_line(STR("DEVSTATION done station=") + std::to_wstring(g_sd.id) + STR(" gone after ") + std::to_wstring(static_cast<int>(g_sd.t)) + STR(" s"));
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
        if (cmd == "dev_stations") return STR("ok stations=") + std::to_wstring(stations_write());
        if (cmd.rfind("dev_destroy_station:", 0) == 0) return station_destroy_start(cmd.substr(20));
        handled = false;
        return std::wstring();
    }
}
