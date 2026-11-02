// FILE: mods/MiningBenchMod/mb_weapons.hpp
// WHAT: weapon lookup in ship.Weapons, the SetFiring and bFiring writes for mining laser and scooper, the ShipFiringInput group route.
// WHY separate: all firing code is in one place.
// DEPENDS ON: mb_common (alive, log_line), mb_ue (weapon-related reflection: get_obj_prop, put, set_bool_prop, get_bool_prop, first_of, obj_id).
// USED BY: mb_steer, mb_testcmd.
// GAME FACTS:
//   VERIFIED (manual run, log): mining laser is in the primary fire group and the item scooper (tractor) in the secondary group. Laser = primary, scooper = secondary.
//   VERIFIED (brief Update 21, P0 run C): writing the bFiring flag alone did not make the laser fire.
//   VERIFIED (brief Update 25): Test-tab laser and tractor on/off work in game.
//   UNVERIFIED: whether the ShipFiringInput route (fire_input) gives ore on its own (fire probe result not recorded).
//   ASSUMPTION: SetFiring(bNewFiring) exists on the Weapon ACTOR class, not on the sim WeaponObject (from the code comment).
#pragma once

namespace
{
    // ---------------- lookup ----------------

    // WHAT: pointer to the ship's Weapons TArray, or nullptr when the ship or the property is missing.
    // WHY: reflection read (there are no headers). The property type is checked first, and alive() guards the ship, because a native access violation cannot be caught and crashes the game.
    TArray<UObject*>* weapon_array(UObject* ship)
    {
        if (!alive(ship)) return nullptr;
        FProperty* p = ship->GetPropertyByNameInChain(STR("Weapons"));
        return (p && CastField<FArrayProperty>(p)) ? ship->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Weapons")) : nullptr;
    }

    // WHAT: true when the weapon is a mining weapon. With no filter: class name contains MiningLaser or ItemScooper. With a filter: class name contains the filter text.
    // WHY: the default matches the two weapons the mod uses for mining, the laser (primary group) and the scooper (secondary group). The filter lets the test tab target one class.
    bool weapon_match(UObject* w, const std::wstring& only = std::wstring())
    {
        if (!alive(w)) return false;
        if (only == STR("*")) return true;   // every weapon (stop: the cannon must go quiet too)
        std::wstring cn = w->GetClassPrivate()->GetName();
        if (!only.empty()) return cn.find(only) != std::wstring::npos;
        return cn.find(STR("MiningLaser")) != std::wstring::npos || cn.find(STR("ItemScooper")) != std::wstring::npos;
    }

    // ---------------- firing writes ----------------

    // WHAT: calls SetFiring(bNewFiring) on the weapon's Actor object. false when the actor or the function is missing.
    // WHY: the Weapon ACTOR class owns SetFiring; the sim WeaponObject flag alone did not fire (brief Update 21), so this is the route that works.
    bool actor_set_firing(UObject* wobj, bool on)
    {
        UObject* a = get_obj_prop(wobj, STR("Actor"));
        if (!alive(a)) return false;
        UFunction* fn = a->GetFunctionByNameInChain(STR("SetFiring"));
        if (!fn) return false;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        uint8_t v = on ? 1 : 0;
        if (!put(fn, buf, STR("bNewFiring"), &v, 1)) return false;
        a->ProcessEvent(fn, buf.data());
        return true;
    }

    // WHAT: sets every matching mining weapon on or off (actor SetFiring and the bFiring flag). `found` gets the number of matches; the return value counts weapons where either write worked.
    // WHY: both writes are made, because SetFiring is the route that works and the flag write is what the original code did. Whether the flag write is needed: WHY unknown (ported as is).
    int set_all_firing(UObject* ship, bool on, int& found, const std::wstring& only = std::wstring())
    {
        found = 0;
        int ok = 0;
        TArray<UObject*>* wa = weapon_array(ship);
        if (!wa) return 0;
        for (int32_t i = 0; i < wa->Num(); ++i)
        {
            UObject* w = (*wa)[i];
            if (!weapon_match(w, only)) continue;
            ++found;
            bool a = actor_set_firing(w, on);
            bool b = set_bool_prop(w, STR("bFiring"), on);
            if (a || b) ++ok;
        }
        return ok;
    }

    // WHAT: builds a ShipFiringInput with the CDO factory CreateInputShipFiring and sends it to the ship through DMSPlayerController.AddInputCommand. group 0 = all four flags set to `on`; 1 = primary group only; 2 = secondary group only (other group off).
    // WHY: this is the game's own input route for fire, the same route add_input uses for orders. Group 1 = laser, group 2 = scooper (manual-run log). The CDO is used because the factory is called on the class, not on an instance.
    bool fire_input(UObject* ship, bool on, int group = 0)
    {
        if (!alive(ship)) return false;
        auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ShipFiringInput"));
        UObject* cdo = cls ? cls->GetClassDefaultObject() : nullptr;
        UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(STR("CreateInputShipFiring")) : nullptr;
        if (!fn) { log_line(STR("fire_input: CreateInputShipFiring not found")); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        uint8_t v = on ? 1 : 0;
        uint8_t vp = (group == 0 || group == 1) ? v : 0;
        uint8_t vs = (group == 0 || group == 2) ? v : 0;
        if (!put(fn, buf, STR("bSetPrimary"), &vp, 1) || !put(fn, buf, STR("bFiringPrimary"), &vp, 1) ||
            !put(fn, buf, STR("bSetSecondary"), &vs, 1) || !put(fn, buf, STR("bFiringSecondary"), &vs, 1))
        { log_line(STR("fire_input: CreateInputShipFiring params not found")); return false; }
        cdo->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        if (!rp) return false;
        UObject* cmd = *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal());
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* f1 = (alive(ctrl) && alive(cmd)) ? ctrl->GetFunctionByNameInChain(STR("AddInputCommand")) : nullptr;
        if (!f1) { log_line(STR("fire_input: AddInputCommand not available")); return false; }
        std::vector<uint8_t> b1(f1->GetParmsSize() + 16, 0);
        int sid = obj_id(ship);
        if (!put(f1, b1, STR("InReceiverID"), &sid, sizeof(sid)) || !put(f1, b1, STR("InCommand"), &cmd, sizeof(cmd)))
        { log_line(STR("fire_input: AddInputCommand params not found")); return false; }
        ctrl->ProcessEvent(f1, b1.data());
        return true;
    }

}
