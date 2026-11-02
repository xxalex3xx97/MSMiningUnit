// FILE: mods/MiningBenchMod/mb_rocks.hpp
// WHAT: rock (asteroid) list taken from the game's own V-scan markers, ore per rock, nearest-rock picking, ship aim frame.
// WHY separate: rock scan and selection are shared by the move job (mb_steer.hpp) and the Test tab (mb_testcmd.hpp).
// DEPENDS ON: mb_common.hpp, mb_config.hpp (eff_cfg, g_raw_per_m), mb_ue.hpp, mb_math.hpp (norm3, dot3, cross3, MV_PI),
//             mb_docks.hpp (find_item), mb_cmdstate.hpp (g_cmd).
// USED BY: mb_steer.hpp (act_frame, act_aim, aim_calc, pick_rock, rock_radius_raw, rock_id_of, rock_kind, actor_ok),
//          mb_testcmd.hpp (rock_list_ensure, act_frame, nearest_item_actor, pick_rock, rock_actor_by_id, rock_kind,
//          rock_id_of, rock_sel in mb_devcmd.hpp), dllmain.cpp (include only).
// GAME FACTS (fact = taken from earlier in-game checks; ASSUMPTION = from code/notes, not re-checked; UNVERIFIED = not confirmed):
//   - Rock list = DMSPlayerController.ScannableHUDMarkers, the game's own V-scan list. Entries are rock actors
//     (BP_GenericResourceAsteroid_C : DMSActorBase).
//   - Ore = BPI_HUDMarkerActor.GetScanItem(Item) on each actor. Id = DMSActorBase.ObjectId = save object id
//     (SmallResourceAsteroidObject). Radius = GetRadius() on that save object, raw units.
//   - Distance = K2_GetActorLocation world difference / 100 (uu -> m). fact: 100 uu = 1 m (verified from weapon actors, earlier).
//   - Save-object Location is chunk-relative: NOT used for distances.
//   - Ship frame = ship object 'Actor' property: K2_GetActorLocation, GetActorForwardVector / RightVector / UpVector.
//   - IsMarkerVisible(InObject) on the controller: UNVERIFIED meaning (only the 'marked' column uses it).

#pragma once

namespace
{
    // ---------------- globals and constants ----------------

    // WHAT: save object id (ObjectId) of the rock picked by the user with rock_sel, per ship (sid -> id). Missing = 0 = auto (nearest rock of the INI resource).
    // WHY: face/approach and the status target use this rock while it is alive, ignoring the INI item filter.
    std::map<int, int> g_sel_rocks;
    int sel_rock_of(int sid) { auto it = g_sel_rocks.find(sid); return it == g_sel_rocks.end() ? 0 : it->second; }

    // WHAT: full rock pass period (s): ScannableHUDMarkers re-read, ore/id read for new actors, nearest rock per ore kind.
    // WHY: per-rock game calls are expensive; FindAllOf / reflection per rock per second froze the game (perf lesson).
    constexpr double ROCK_PASS_S = 10.0;

    // WHAT: Unreal units per metre (100 uu = 1 m). fact: verified earlier from weapon actors.
    constexpr double MV_UU_PER_M = 100.0;

    // ---------------- rock actors: the game's V-scan list (controller ScannableHUDMarkers) ----------------
    // WHY: the game's own V-scan and distances use the asteroid ACTORS listed in ScannableHUDMarkers. Distance = actor world
    // position difference (K2_GetActorLocation, uu: 100 uu = 1 m), i.e. the game's own math.
    // Ore = BPI_HUDMarkerActor::GetScanItem (read once per actor). Id = DMSActorBase.ObjectId = the SmallResourceAsteroidObject id.
    // The save-object Location is chunk-relative, so the list/pick/aim below never use it for distances.

    // CRASH FIX: rock actors are streamed in and out by the game. Pointers we cache across ticks (target of a move job, rock_sel,
    // list rows) can be FREED, and alive() (reads the object) then crashes with an access violation. actor_ok() first checks the
    // pointer is still in the controller's own ScannableHUDMarkers array (the game removes destroyed actors from it), and only then
    // is it safe to touch. The set is rebuilt at most once per engine tick (g_actor_frame++ in cmd_tick); only pointers are read.

    // WHAT: frame counters for actor_ok(). g_actor_frame is bumped once per engine tick (cmd_tick); g_actor_set_frame = frame the set was built for.
    uint64_t g_actor_frame = 1, g_actor_set_frame = 0;

    // WHAT: pointers of the controller's ScannableHUDMarkers entries valid for the current frame (rebuilt inside actor_ok).
    std::unordered_set<UObject*> g_actor_set;

    // FALLBACK: the HUD list is the player's own scan list. An AI ship far from the player can see no rock in it while rock actors
    // are loaded in its sector. A belt seek with no rock in range requests the fallback; rock_list_refresh then adds the actors of
    // class BP_GenericResourceAsteroid_C from FindAllOf (not in the HUD list). FindAllOf runs at most every ROCK_FALLBACK_S_MS.
    constexpr ULONGLONG ROCK_FALLBACK_WANT_MS = 6000;   // a request lasts this long (seek re-requests on each rock check)
    constexpr ULONGLONG ROCK_FALLBACK_S_MS = 5000;      // FindAllOf pass period (ms)
    ULONGLONG g_rock_fallback = 0;                      // GetTickCount64 of the last request (0 = never)
    ULONGLONG g_rock_fb_pass_ms = 0;                    // GetTickCount64 of the last FindAllOf pass (0 = never)
    std::vector<UObject*> g_fb_list;                    // fallback actors of the last pass
    std::unordered_set<UObject*> g_fb_set;              // same actors, for actor_ok

    // WHAT: a belt seek with no rock in range asks for the fallback (valid for ROCK_FALLBACK_WANT_MS).
    void rock_fallback_request() { g_rock_fallback = GetTickCount64(); }

    // WHAT: true while a fallback request is recent.
    bool fallback_wanted() { return g_rock_fallback != 0 && GetTickCount64() - g_rock_fallback <= ROCK_FALLBACK_WANT_MS; }

    // WHAT: true when a is still one of the controller's live ScannableHUDMarkers entries (or a live fallback actor while wanted) and still alive.
    // WHY: cached actor pointers can be freed when the game streams actors out; reading a freed actor crashes (access violation),
    //      so list membership is checked before any call on it.
    bool actor_ok(UObject* a)
    {
        if (!a) return false;
        // 1. rebuild the pointer set once per engine tick (frame changed)
        if (g_actor_set_frame != g_actor_frame)
        {
            g_actor_set_frame = g_actor_frame;
            g_actor_set.clear();
            UObject* ctrl = first_of(STR("DMSPlayerController"));
            if (alive(ctrl) && prop_is(ctrl, STR("ScannableHUDMarkers"), STR("ArrayProperty")))
            {
                auto* arr = ctrl->GetValuePtrByPropertyNameInChain<TArray<FScriptInterface>>(STR("ScannableHUDMarkers"));
                for (int32_t i = 0; arr && i < arr->Num(); ++i) if ((*arr)[i].ObjectPointer) g_actor_set.insert((*arr)[i].ObjectPointer);
            }
        }
        // 2. a must be in the live set and still alive
        return (g_actor_set.count(a) != 0 || (g_fb_set.count(a) != 0 && fallback_wanted())) && alive(a);
    }

    // WHAT: rock actors from the last list refresh. Dead entries fail alive() and callers skip them.
    std::vector<UObject*> g_rock_actors;

    // WHAT: actor -> ore item (GetScanItem). nullptr = unreadable. Read once per actor and kept until the actor leaves the list.
    std::map<UObject*, UObject*> g_rock_item;

    // WHAT: actor -> ObjectId (save object id). 0 = unknown.
    std::map<UObject*, int> g_rock_id;

    // WHAT: ObjectId -> actor (only ids > 0). Lookup for rock_sel and the status target.
    std::map<int, UObject*> g_rock_by_id;

    // WHAT: ObjectId -> SmallResourceAsteroidObject (save object, source of GetRadius). Rebuilt on every list refresh.
    std::map<int, UObject*> g_save_by_id;

    // WHAT: GetTickCount64() in ms at the last list refresh (0 = never).
    ULONGLONG g_rock_list_ms = 0;

    // ---------------- ore and radius helpers ----------------

    // WHAT: display name of an ore item (DisplayName text, GetName as fallback). "?" when the item object is gone.
    // WHY: the rocks list shows the name the player sees in game (user request), not the ITEM_ source name.
    std::wstring item_kind(UObject* item)
    {
        if (!alive(item)) return STR("?");
        const std::wstring dn = obj_display(item);
        if (!dn.empty()) return dn;
        const std::wstring nm = item->GetName();
        return nm;
    }

    // WHAT: ore display name of a rock actor from g_rock_item. "?" when the ore is unknown.
    // WHY: the rocks file shows which ore each rock holds, for the Test tab target rows.
    std::wstring rock_kind(UObject* act)
    {
        auto it = g_rock_item.find(act);
        if (it == g_rock_item.end()) return STR("?");
        return item_kind(it->second);
    }

    // WHAT: GetRadius() of a save object (SmallResourceAsteroidObject), raw units. 0 when the call fails.
    // WHY: surface distance = centre distance - radius; the stop rule uses surface distance.
    double rock_radius(UObject* o)
    {
        int64_t rad = 0;
        return call_pnum(o, STR("GetRadius"), rad) ? static_cast<double>(rad) : 0.0;
    }

    // WHAT: ObjectId (save object id) of a rock actor from g_rock_id. 0 when unknown.
    // WHY: ids are what the user types (rock_sel <id>) and what the status file shows.
    int rock_id_of(UObject* act)
    {
        auto it = g_rock_id.find(act);
        return it == g_rock_id.end() ? 0 : it->second;
    }

    // WHAT: rock actor with this save id, or nullptr when it is not in the loaded list (or not alive).
    // WHY: rock_sel and the status target look rocks up by id each time, so no actor pointer is kept across ticks.
    UObject* rock_actor_by_id(int id)
    {
        auto it = g_rock_by_id.find(id);
        return (it != g_rock_by_id.end() && actor_ok(it->second)) ? it->second : nullptr;
    }

    // WHAT: radius of a rock actor in raw units, read from its save object (g_save_by_id). 0 when the save object is unknown.
    // WHY: the radius lives on the save object, matched to the actor by ObjectId.
    double rock_radius_raw(UObject* act)
    {
        auto sv = g_save_by_id.find(rock_id_of(act));
        return (sv != g_save_by_id.end() && alive(sv->second)) ? rock_radius(sv->second) : 0.0;
    }


    // WHAT: ore item of a rock actor: calls GetScanItem(Item) on the actor (BPI_HUDMarkerActor). nullptr when unreadable.
    // WHY: the Item parameter is read from a raw parameter buffer after ProcessEvent; the property type check makes sure
    //      the bytes are an object pointer before they are used as one.
    UObject* actor_scan_item(UObject* a)
    {
        UFunction* fn = a->GetFunctionByNameInChain(STR("GetScanItem"));
        FProperty* ip = fn ? fn->FindProperty(FName(STR("Item"), FNAME_Add)) : nullptr;
        if (!ip || !CastField<FObjectProperty>(ip))
        {
            static bool warned = false;
            if (!warned) { warned = true; log_line(STR("rock: GetScanItem Item param missing or not an object property")); }
            return nullptr;
        }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        a->ProcessEvent(fn, buf.data());
        return *reinterpret_cast<UObject**>(buf.data() + ip->GetOffset_Internal());
    }

    // ---------------- list refresh ----------------

    // WHAT: adds rock actor a to the refresh being built (list, items, ids, by_id). Ore and id are reused from the previous refresh
    //       when known; otherwise GetScanItem and ObjectId are read once. A nullptr ore is cached the same way.
    // WHY: one place for the HUD walk and the fallback, so both read ore and id the same way.
    void rock_add(UObject* a, std::vector<UObject*>& list, std::map<UObject*, UObject*>& items, std::map<UObject*, int>& ids, std::map<int, UObject*>& by_id)
    {
        UObject* item = nullptr;
        int id = 0;
        if (g_rock_item.count(a) && g_rock_id.count(a))
        {
            item = g_rock_item[a];
            id = rock_id_of(a);
        }
        else
        {
            item = actor_scan_item(a);
            int32_t v = 0;
            id = get_int_prop(a, STR("ObjectId"), v) ? static_cast<int>(v) : 0;
        }
        items[a] = item;
        ids[a] = id;
        if (id > 0) by_id[id] = a;
        list.push_back(a);
    }

    // WHAT: adds the FindAllOf(BP_GenericResourceAsteroid_C) actors to the refresh being built while a fallback request is recent.
    //       FindAllOf runs at most every ROCK_FALLBACK_S_MS; between passes the last pass's live actors are reused. Logs one
    //       "ROCKFALLBACK found=<n> with_ore=<m>" per pass (found = live actors, with_ore = those whose ore item was read).
    // WHY: an AI ship far from the player sees its belt rocks in the world but not in the player's HUD list.
    void rock_fallback_merge(std::vector<UObject*>& list, std::map<UObject*, UObject*>& items, std::map<UObject*, int>& ids, std::map<int, UObject*>& by_id)
    {
        if (!fallback_wanted()) { g_fb_list.clear(); g_fb_set.clear(); return; }
        const ULONGLONG now = GetTickCount64();
        bool ran = false;
        if (g_rock_fb_pass_ms == 0 || now - g_rock_fb_pass_ms >= ROCK_FALLBACK_S_MS)
        {
            ran = true;
            g_rock_fb_pass_ms = now;
            std::vector<UObject*> found;
            UObjectGlobals::FindAllOf(STR("BP_GenericResourceAsteroid_C"), found);
            g_fb_list.clear();
            g_fb_set.clear();
            for (UObject* a : found) if (alive(a)) { g_fb_list.push_back(a); g_fb_set.insert(a); }
        }
        for (UObject* a : g_fb_list) if (alive(a) && !items.count(a)) rock_add(a, list, items, ids, by_id);
        if (!ran) return;
        int with_ore = 0;
        for (UObject* a : g_fb_list) { auto it = items.find(a); if (it != items.end() && it->second) ++with_ore; }
        log_line(STR("ROCKFALLBACK found=") + std::to_wstring(g_fb_list.size()) + STR(" with_ore=") + std::to_wstring(with_ore));
    }

    // WHAT: full refresh of the rock list (steps 1-5 below). Runs from rock_list_ensure() when rock_list_due() is true.
    // WHY: one place reads the V-scan list, caches ore and id per actor, rebuilds the save-object radius map, and drops
    //      actors that left the list. Run every ROCK_PASS_S, not every tick (perf).
    void rock_list_refresh()
    {
        // 1. stamp the refresh time (rock_list_due() reads it)
        g_rock_list_ms = GetTickCount64();
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        std::vector<UObject*> list;
        std::map<UObject*, UObject*> items;
        std::map<UObject*, int> ids;
        std::map<int, UObject*> by_id;
        if (alive(ctrl) && prop_is(ctrl, STR("ScannableHUDMarkers"), STR("ArrayProperty")))
        {
            // 2. walk ScannableHUDMarkers. FScriptInterface = {UObject* ObjectPointer, void* InterfacePointer}: ObjectPointer is the actor.
            auto* arr = ctrl->GetValuePtrByPropertyNameInChain<TArray<FScriptInterface>>(STR("ScannableHUDMarkers"));
            for (int32_t i = 0; arr && i < arr->Num(); ++i)
            {
                UObject* a = (*arr)[i].ObjectPointer;
                if (!alive(a) || items.count(a)) continue;   // skip dead entries and duplicates
                rock_add(a, list, items, ids, by_id);
            }
        }
        // 3c. fallback: rock actors missing from the HUD list (only while a belt seek asks for them)
        rock_fallback_merge(list, items, ids, by_id);
        // 4. swap in the new maps: actors no longer in the list drop out of every map
        g_rock_actors.swap(list);
        g_rock_item.swap(items);
        g_rock_id.swap(ids);
        g_rock_by_id.swap(by_id);
        // 5. save objects for radius: g_save_by_id keeps only the ids of the listed rocks. The full FindAllOf over every
        //    SmallResourceAsteroidObject (tens of thousands) runs only when a listed rock has no live entry (it was the CPU spike every pass).
        static std::set<int> save_miss;   // ids a walk could not find: not searched again until the set is cleared below
        if (save_miss.size() > 2000) save_miss.clear();
        bool missing = false;
        for (auto& kv : g_rock_id)
        {
            auto sv = g_save_by_id.find(kv.second);
            if ((sv == g_save_by_id.end() || !alive(sv->second)) && !save_miss.count(kv.second)) { missing = true; break; }
        }
        if (missing)
        {
            std::set<int> want;
            for (auto& kv : g_rock_id) want.insert(kv.second);
            g_save_by_id.clear();
            std::vector<UObject*> saves;
            UObjectGlobals::FindAllOf(STR("SmallResourceAsteroidObject"), saves);
            for (UObject* o : saves) if (alive(o)) { const int sid_ = obj_id(o); if (want.count(sid_)) g_save_by_id[sid_] = o; }
            for (int w : want) if (!g_save_by_id.count(w)) save_miss.insert(w);
        }
    }

    // WHAT: true when the rock list must be refreshed: empty, or older than ROCK_PASS_S. No game calls inside.
    // WHY: cheap check, so callers can ask every time. NOTE: an empty list is due on every call (ported as is); callers
    //      throttle their own calls.
    bool rock_list_due()
    {
        return g_rock_actors.empty() || static_cast<double>(GetTickCount64() - g_rock_list_ms) >= ROCK_PASS_S * 1000.0;
    }

    // WHAT: refresh the rock list if it is due.
    // WHY: one entry point, so no caller repeats the timing check.
    void rock_list_ensure()
    {
        const ULONGLONG now = GetTickCount64();
        const bool fb_due = fallback_wanted() && (g_rock_fb_pass_ms == 0 || now - g_rock_fb_pass_ms >= ROCK_FALLBACK_S_MS);
        if (rock_list_due() || fb_due) rock_list_refresh();
    }

    // ---------------- aim frame and per-rock aim numbers ----------------

    // WHAT: ship ACTOR frame in world space. loc = position (uu), fw / R / U = unit forward, right, up axes (unitless).
    // WHY: the game's distances and aim axes come from the actor. The ship object's own Location is chunk-relative.
    struct ActFrame { double loc[3] = {0, 0, 0}, fw[3] = {1, 0, 0}, R[3] = {0, 1, 0}, U[3] = {0, 0, 1}; };

    // WHAT: fill f with the ship actor frame. false when the actor or one of its vector calls is unreadable.
    // WHY: all aim maths (aim_calc, act_aim, speed monitor) start from this one frame.
    bool act_frame(UObject* ship, ActFrame& f)
    {
        UObject* sa = obj_prop(ship, STR("Actor"));
        if (!alive(sa)) return false;
        std::wstring why;
        return actor_vec_call(sa, STR("K2_GetActorLocation"), f.loc, why) && actor_vec_call(sa, STR("GetActorForwardVector"), f.fw, why) &&
               actor_vec_call(sa, STR("GetActorRightVector"), f.R, why) && actor_vec_call(sa, STR("GetActorUpVector"), f.U, why);
    }

    // WHAT: aim numbers of one rock against the ship frame.
    //       dist_m = centre distance (m); err_deg = angle between the nose and the direction to the rock (deg);
    //       a_body = rotation axis (unitless, ship axes) that turns the nose toward the rock, a = cross(fw, dir), projected on fw/R/U
    //       (a_body[0] is always ~0 because a is perpendicular to fw); rloc = rock actor location (uu, for logs).
    // WHY: one struct for picking, steering and status, so every caller uses the same aim math.
    struct ActAim { double dist_m = 0.0, err_deg = 0.0, a_body[3] = {0, 0, 0}, rloc[3] = {0, 0, 0}; };

    // WHAT: fill r with the aim numbers of rock actor 'rock' against frame f. false when the rock is not live or the vector is unreadable.
    // WHY: the rock location is read only after actor_ok(), so a freed actor is never touched.
    bool act_aim(const ActFrame& f, UObject* rock, ActAim& r)
    {
        std::wstring why;
        if (!actor_ok(rock) || !actor_vec_call(rock, STR("K2_GetActorLocation"), r.rloc, why)) return false;
        double d[3] = {r.rloc[0] - f.loc[0], r.rloc[1] - f.loc[1], r.rloc[2] - f.loc[2]};
        const double n = norm3(d);
        if (n < 1e-9) return false;
        for (double& x : d) x /= n;
        r.dist_m = n / MV_UU_PER_M;
        r.err_deg = std::acos(std::clamp(dot3(f.fw, d), -1.0, 1.0)) * 180.0 / MV_PI;
        double a[3];
        cross3(f.fw, d, a);
        r.a_body[0] = dot3(a, f.fw);
        r.a_body[1] = dot3(a, f.R);
        r.a_body[2] = dot3(a, f.U);
        return true;
    }

    // WHAT: nearest rock actor whose ore == item, by game distance. out = its aim numbers. nullptr when none.
    // WHY: filters by ore first (cheap map lookup), so only rocks of that ore have their location read.
    UObject* nearest_item_actor(const ActFrame& f, UObject* item, ActAim& out)
    {
        UObject* best = nullptr;
        for (UObject* a : g_rock_actors)
        {
            auto it = g_rock_item.find(a);
            if (it == g_rock_item.end() || it->second != item) continue;
            ActAim r;
            if (!act_aim(f, a, r)) continue;
            if (!best || r.dist_m < out.dist_m) { best = a; out = r; }
        }
        return best;
    }

    // WHAT: rock for a face/approach job or the status target. Order: rock_sel (g_sel_rock, while alive) > nearest rock actor of the
    //       INI resource by game distance. rule = which rule applied (logged at move start). nullptr when no rock.
    // WHY: an explicit user choice wins; otherwise the nearest rock with the configured ore. No V-scan requirement.
    UObject* pick_rock(int sid, UObject* ship, std::wstring& why, std::wstring& rule)
    {
        rock_list_ensure();
        const int sel = sel_rock_of(sid);
        if (sel > 0)
        {
            UObject* a = rock_actor_by_id(sel);
            if (alive(a)) { rule = STR("rock_sel"); return a; }
        }
        rule = STR("INI resource");
        UObject* item = find_item(eff_cfg(sid).resource, why);
        if (!alive(item)) return nullptr;
        ActFrame f;
        if (!act_frame(ship, f)) { why = STR("ship actor unreadable"); return nullptr; }
        ActAim r;
        return nearest_item_actor(f, item, r);
    }

}
