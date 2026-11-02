// FILE: mods/MiningBenchMod/mb_belt.hpp
// WHAT: sector reading for the belt warp (PART 6d C1). sectors_read lists every SolarSystemSector with its system, chunk, half extents, name and ore list
//       (GenParams.ResourceRichness[].Item). belt_target picks the sector a ship should warp to for the needed ore: the same system only, the ore must be there,
//       score = squared chunk distance (the current sector and the belt setting "<id>" win). sectors_file_tick writes Sectors.txt (id|name|ores) every 10 s.
// WHY separate: the belt choice is its own rule set (AUTO / none / sector id) and feeds the mining loop state machine, so it stays out of mb_mining.hpp.
// DEPENDS ON: mb_common.hpp (alive, log_line, write_atomic, get_dll_dir, u8), mb_ue.hpp (obj_id, obj_display, get_int_prop, get_obj_prop, call_text, type_text),
//             mb_warp.hpp (sec_axes, sys_id, get_chunk), mb_config.hpp (Config belt field, passed in as a string).
// USED BY: mb_mining.hpp (belt_target in the CHECK/SEEK states, sectors_file_tick from the status tick) - wired in the next slice.
// GAME FACTS (probes 2026-10-29): SolarSystemSector{ID, ParentID (solar system id), ChunkLocation, HalfExtents, GenParams}; GenParams is an OBJECT (data asset).
//   ResourceRichness elements {Item (object), Amount}. Item names: obj_display, else GetName (ITEM_CobaltOre). Unvisited sectors are readable (SECTOR-LIST probe).
#pragma once

#include <set>

namespace
{
    // WHAT: one sector as read by sectors_read. he = half extents in chunks (default 2 when unreadable). ores = ore item objects of GenParams.ResourceRichness.
    struct SecInfo
    {
        int id = 0;
        int sys = 0;
        int64_t ch[3] = {0, 0, 0};
        int64_t he[3] = {2, 2, 2};
        std::wstring name;
        std::vector<UObject*> ores;
        std::wstring ores_txt;
    };


    // WHAT: display text of an ore item: obj_display, else the object name.
    // WHY: the display name is the readable one, but some ResourceRichness items return an empty display name (probe 2026-10-29).
    std::wstring ore_label(UObject* it)
    {
        if (!alive(it)) return STR("");
        std::wstring n = obj_display(it);
        return n.empty() ? it->GetName() : n;
    }

    // WHAT: every SolarSystemSector with its system, chunk, half extents, name and ores (from MSCore). Sectors without a readable chunk are skipped.
    // WHY: one MSCore walk per call (count, then fill with a small margin in case sectors appeared in between); belt_target and sectors_file_tick both use it.
    std::vector<SecInfo> sectors_read()
    {
        std::vector<SecInfo> out;
        std::wstring e;
        if (!ms_load(e)) return out;
        const int n = g_ms.sectors(nullptr, 0);
        if (n <= 0) return out;
        std::vector<MsSector> rows(static_cast<size_t>(n) + 16);
        const int got = std::min(g_ms.sectors(rows.data(), static_cast<int>(rows.size())), static_cast<int>(rows.size()));
        for (int i = 0; i < got; ++i)
        {
            const MsSector& r = rows[static_cast<size_t>(i)];
            SecInfo s;
            s.id = r.id;
            s.sys = r.sys;
            for (int k = 0; k < 3; ++k) { s.ch[k] = r.ch[k]; s.he[k] = r.he[k]; }
            s.name = r.name;
            for (int k = 0; k < r.nores && k < 40; ++k) s.ores.push_back(static_cast<UObject*>(r.ores[k]));
            for (size_t k = 0; k < s.ores.size(); ++k)
            {
                if (k) s.ores_txt += STR(", ");
                s.ores_txt += ore_label(s.ores[k]);
            }
            out.push_back(std::move(s));
        }
        return out;
    }

    // WHAT: true when the sector lists the ore item (same object, or same object name).
    // WHY: the name test covers a second item object with the same name.
    bool sec_has_ore(const SecInfo& s, UObject* item)
    {
        for (UObject* o : s.ores)
            if (alive(o) && (o == item || o->GetName() == item->GetName())) return true;
        return false;
    }

    // WHAT: system id and chunk of a ship. false when either is unreadable.
    bool ship_pos(UObject* ship, int& sys, int64_t ch[3])
    {
        sys = sys_id(ship);
        return sys > 0 && get_chunk(ship, ch);
    }


    // WHAT: the sector of system sys that contains chunk ch (|diff| <= half extents on all axes). Smallest squared distance wins. nullptr when none.
    const SecInfo* sector_of(const std::vector<SecInfo>& v, int sys, const int64_t ch[3])
    {
        const SecInfo* best = nullptr;
        double bd = 0;
        for (const SecInfo& s : v)
        {
            if (s.sys != sys) continue;
            if (!sector_contains(s.ch, s.he, ch)) continue;
            const double d = csq(s.ch, ch);
            if (!best || d < bd) { best = &s; bd = d; }
        }
        return best;
    }

    // WHAT: id of the sector the ship is in, 0 when none or unreadable.
    int cur_sector_id(UObject* ship)
    {
        int sys = 0;
        int64_t ch[3] = {0, 0, 0};
        if (!ship_pos(ship, sys, ch)) return 0;
        const std::vector<SecInfo> secs = sectors_read();
        const SecInfo* s = sector_of(secs, sys, ch);
        return s ? s->id : 0;
    }

    // WHAT: round-trip fuel check for a belt sector. Same system: need = 2 * ceil(dist_km / 50) warp fuel items (1 item = 50 km); have = GetAvailableJumpFuel = items in the WarpDrive inventory (max 100). Other system: 2 * GetJumpCost <= drive.
    bool belt_fuel_ok(UObject* ship, const SecInfo& s, std::wstring& why)
    {
        int32_t cost = 0, drive = 0;
        std::wstring err;
        if (!jump_cost(ship, s.sys, s.ch, cost, err)) { why = err; return false; }
        if (!call_int(ship, STR("GetAvailableJumpFuel"), drive)) { why = STR("GetAvailableJumpFuel unreadable"); return false; }
        int sys = 0;
        int64_t ch[3] = {0, 0, 0};
        const bool same = ship_pos(ship, sys, ch) && sys == s.sys;
        const double dist = same ? std::sqrt(csq(ch, s.ch)) : 0.0;
        double km = 0;
        long long need = 0;
        if (same) { km = dist * 2.048; need = warp_fuel_need(dist); }
        const bool ok = warp_fuel_ok(same, dist, cost, drive);
        log_line(STR("FUELCHK sector=") + std::to_wstring(s.id) + STR(" rule=") + (same ? STR("km") : STR("jumpcost")) +
                 STR(" dist_km=") + std::to_wstring(static_cast<long long>(km)) + STR(" need=") + std::to_wstring(need) +
                 STR(" drive=") + std::to_wstring(drive) + STR(" jump_cost=") + std::to_wstring(cost) +
                 STR(" round_trip_ok=") + (ok ? STR("yes") : STR("no")));
        if (!ok) why = STR("not enough warp fuel for round trip");
        return ok;
    }

    // WHAT: the sector a ship should warp to for ore item (nullptr = any ore). Returns the sector id (>0).
    //       0 = fuel-blocked (why set), -2 = no sector has the ore (why set), -1 = unreadable.
    //       cur gets the ship's current sector id (0 = unknown). tname gets the chosen sector name.
    //       Candidates: same system as the ship, not in tried, has the ore. Score: the belt-setting sector (digits) wins, then the current sector, then squared chunk distance.
    // WHY: AUTO = closest sector with the needed ore. tried lets the caller skip sectors that already failed.
    int belt_target(UObject* ship, const std::string& belt, UObject* item, const std::set<int>& tried, int& cur, std::wstring& tname, std::wstring& why)
    {
        cur = 0;
        int sys = 0;
        int64_t ch[3] = {0, 0, 0};
        const std::vector<SecInfo> secs = sectors_read();
        if (!ship_pos(ship, sys, ch) || secs.empty())
        {
            log_line(STR("SECTOR target unreadable: ship position or sector list (sectors=") + std::to_wstring(secs.size()) + STR(" sys=") + std::to_wstring(sys) + STR(" chunk=") + trio_text(ch) + STR(")"));
            return -1;
        }
        if (const SecInfo* here = sector_of(secs, sys, ch)) cur = here->id;
        const int want = belt_want(belt);
        struct Cand { const SecInfo* s; double d; };
        std::vector<Cand> cands;
        for (const SecInfo& s : secs)
        {
            if (s.sys != sys || tried.count(s.id) || (item && !sec_has_ore(s, item))) continue;
            cands.push_back({&s, belt_score(s.id, want, cur, csq(s.ch, ch))});
        }
        // best first; the fuel check (a JUMPCOST log line + 2 game calls) runs only until one candidate passes
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
        const SecInfo* best = nullptr;
        bool fuel_blocked = false;
        std::wstring fuel_why;
        for (const Cand& k : cands)
        {
            // FUEL-FILTER: a sector we would warp to must pass the round-trip check; the sector the ship is in needs no warp
            if (k.s->id != cur)
            {
                std::wstring fw;
                if (!belt_fuel_ok(ship, *k.s, fw)) { fuel_blocked = true; fuel_why = fw; continue; }
            }
            best = k.s;
            break;
        }
        if (!best)
        {
            if (fuel_blocked) { why = fuel_why; return 0; }
            why = STR("no sector has ") + (item ? ore_label(item) : std::wstring(STR("the ore"))) + STR(" left to mine");
            return -2;
        }
        tname = best->name;
        return best->id;
    }

    // WHAT: writes Sectors.txt ("<id>|<name>|<ores>|<ore ids>|<sys>,<chx>,<chy>,<chz>" per sector, | in names replaced by a space) every 10 s, only when the text changed.
    //       5th field = system id and chunk of the sector (Lua computes the distance to the ship). Logs one BELT line on each change (sector count, first sector, its ores).
    // WHY: the Lua window reads the list for the belt selector; the log shows what the game reports for ore names.
    void sectors_file_tick(double dt)
    {
        static double acc = 60.0;   // first call writes at once; sectors do not change, so then once a minute (the walk is a full object scan)
        acc += dt;
        if (acc < 60.0) return;
        acc = 0.0;
        const std::vector<SecInfo> secs = sectors_read();
        std::string s;
        for (const SecInfo& e : secs)
        {
            std::wstring nm = e.name, ores = e.ores_txt;
            std::replace(nm.begin(), nm.end(), L'|', L' ');
            std::replace(ores.begin(), ores.end(), L'|', L' ');
            std::string ids;  // 4th field: ore item ids (object names, same as items.txt), comma separated
            for (UObject* o : e.ores)
            {
                if (!alive(o)) continue;
                if (!ids.empty()) ids += ",";
                ids += u8(o->GetName());
            }
            const std::string where = std::to_string(e.sys) + "," + std::to_string(e.ch[0]) + "," + std::to_string(e.ch[1]) + "," + std::to_string(e.ch[2]);
            s += std::to_string(e.id) + "|" + u8(nm) + "|" + u8(ores) + "|" + ids + "|" + where + "\n";
        }
        static std::string last = "\x01";
        if (s == last) return;
        last = s;
        try { write_atomic(get_dll_dir() / "Sectors.txt", s); } catch (...) {}
        if (!secs.empty())
            log_line(STR("SECTOR sectors=") + std::to_wstring(secs.size()) + STR(" first: ") + secs[0].name + STR(" ores: ") + secs[0].ores_txt);
    }
}
