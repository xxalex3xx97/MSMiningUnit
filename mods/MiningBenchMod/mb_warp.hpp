// FILE: mods/MiningBenchMod/mb_warp.hpp
// WHAT: warp to a sector (PART 6c test). warp_sector_cmd asks the ship for its jump arrival point in the sector (GetJumpArrivalLocation), checks it is
//       plausible, then sends the teleport the player's manual warp sends (ShipTeleportInput). First try is without the SetPilotingInput wrapper;
//       warp_check logs the result 1.5 s later and retries once with the wrapper when the ship did not move. warp_by_name_cmd finds the ship by display name.
// WHY separate: the warp is its own command family (the dock flow no longer warps: the game autopilot does that).
// DEPENDS ON: mb_common.hpp (alive, log_line, tlog, widen), mb_ue.hpp (find_by_id, obj_id, obj_display, get_loc, get_obj_prop, get_int_prop, first_of, pvec_text),
//             mb_mscore.hpp (g_ms: arrival, jump cost, warp send, chunk/axes/sys reads), mb_steer.hpp (pilot_console_of), mb_pin.hpp (pinned_now).
// USED BY: mb_testcmd.hpp (run_cmd: warp_sector:, warp_by_name:; cmd_tick: warp_check).
// GAME FACTS (probe 2026-10-29, probe_waypoint.txt):
//   fact: warp = AddInputCommand(receiver, ShipTeleportInput{bSetTarget=1, bClearTarget=0, NewLocation, NewQuat{W=65536}, NewChunkLocation, NewSolarSystem}).
//         The manual warp wraps it: SetPilotingInput(receiver=player controller id 2325, bNewPiloting=1, ConsoleID=console) before, bNewPiloting=0 after.
//   fact: DMSPlayerShipObject:GetJumpArrivalLocation(TargetLocation out PVector, TargetChunkLocation out IntVector, TargetSolarSystemID int, ArrivalDistance PNum) -> PVector.
//   fact: sector 1008 arrival (-51644600,75833800,14000) +-5000 (raw units); |xy| = GenParams.WarpArrivalDistance (91750400).
//   UNVERIFIED: whether the teleport alone (no wrapper) moves the ship, and whether receiver 2325 is right for the piloting inputs (the log prints the ids).
#pragma once

#include <cmath>
#include <cstdlib>

namespace
{
    // WHAT: the last warp, read by warp_check: ship id, target system, target chunk, arrival point, pending flag, retry flag, wrapper flag, seconds left.
    // WHY: the arrival is logged 1.5 s after the teleport, when the game has applied it; the retry needs the same target.
    struct WarpChk { int sid = 0; int sys = 0; int64_t ch[3] = {0, 0, 0}; int64_t arr[3] = {0, 0, 0}; double t = 0; bool pend = false; bool retried = false; bool wrap = false; int32_t fuel0 = -1; } g_warp_chk;   // fuel0: GetAvailableJumpFuel before the teleport (-1 unknown)
    // WHAT: ships waiting for their undock before the warp: ship id -> sector arg as warp_sector_cmd got it. WHY: a docked ship undocks first; warp_wait_tick sends the warp when the undock job is done.
    std::map<int, std::string> g_warp_wait;

    // WHAT: "(a,b,c)" text of 3 int64 values for logs.
    std::wstring trio_text(const int64_t c[3])
    {
        return STR("(") + std::to_wstring(c[0]) + STR(",") + std::to_wstring(c[1]) + STR(",") + std::to_wstring(c[2]) + STR(")");
    }

    // WHAT: one warp through MSCore (SetPilotingInput wrap optional, ShipTeleportInput). set_target = bSetTarget only. false with err on failure.
    bool warp_send(int sid, int sys, const int64_t ch[3], const int64_t arr[3], bool wrap, std::wstring& err, bool set_target = false)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        return ms_err(g_ms.warp_send(sid, sys, ch, arr, wrap ? 1 : 0, set_target ? 1 : 0, e, 256), e, err);
    }

    // WHAT: the ship's jump arrival point in a sector (GetJumpArrivalLocation via MSCore). arr gets the raw PVector. false with err on failure.
    bool arrival_of(UObject* ship, int sys, const int64_t ch[3], int64_t dist, int64_t arr[3], std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        return ms_err(g_ms.arrival(ship, sys, ch, dist, arr, e, 256), e, err);
    }

    // WHAT: GetJumpCost(ch, sys) on the ship via MSCore. false+err on failure (cost untouched).
    bool jump_cost(UObject* ship, int sys, const int64_t ch[3], int32_t& cost, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        int c = 0;
        if (!ms_err(g_ms.jump_cost(ship, sys, ch, &c, e, 256), e, err)) return false;
        cost = c;
        return true;
    }

    // WHAT: chunk location of o via MSCore (GetChunkLocation or ChunkLocation). false when unreadable.
    bool get_chunk(UObject* o, int64_t c[3])
    {
        std::wstring e;
        return alive(o) && ms_load(e) && g_ms.chunk_of(o, c) != 0;
    }

    // WHAT: chunk (or any X/Y/Z struct property) of object o into out[3] via MSCore. false when missing.
    bool sec_axes(UObject* o, const TCHAR* prop, int64_t out[3])
    {
        std::wstring e;
        return alive(o) && ms_load(e) && g_ms.axes_of(o, prop, out) != 0;
    }

    // WHAT: GenParams.WarpArrivalDistance of a sector (raw PNum) via MSCore. false when missing.
    bool sec_warp_dist(UObject* sec, int64_t& out)
    {
        std::wstring e;
        return ms_load(e) && g_ms.sec_warp_dist(sec, &out) != 0;
    }

    // WHAT: solar system id of o via MSCore. 0 when unreadable.
    int sys_id(UObject* o)
    {
        std::wstring e;
        return ms_load(e) ? g_ms.sys_id(o) : 0;
    }

    // WHAT: warps ship sid to a sector: secid = a sector id, or "pinned" (last map pin, MiningPinned.txt).
    //       Steps: 1 read sector chunk, parent system and warp arrival distance. 2 arrival point from the ship (arrival_of). 3 plausible check:
    //       |xy| of the arrival within 2% of the warp arrival distance (else no warp, logged). 4 send the teleport without the wrapper (warp_send).
    //       warp_check then logs the result and retries once with the wrapper if the ship did not move. Returns "ok" or "error: ...".
    // WHY: the user pins a sector on the map and presses the Test button; the log shows input, arrival and result (WARPSECTOR lines).
    std::wstring warp_sector_cmd(int sid, const std::string& arg)
    {
        if (!g_compat.usable(F_WARP)) return STR("error: COMPAT warp disabled (") + widen(g_compat.done ? g_compat.reason(F_WARP) : std::string("self-test pending")) + STR(")");
        const int secid = arg == "pinned" ? pinned_now() : std::atoi(arg.c_str());
        if (secid <= 0) return STR("error: no sector (pin a sector on the map first)");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        UObject* sec = find_by_id(STR("SolarSystemSector"), secid);
        if (!alive(sec)) return STR("error: sector ") + std::to_wstring(secid) + STR(" not found");
        int64_t ch[3] = {0, 0, 0}, dist = 0;
        int32_t sys = 0;
        if (!sec_axes(sec, STR("ChunkLocation"), ch) || !get_int_prop(sec, STR("ParentID"), sys) || !sec_warp_dist(sec, dist))
        {
            log_line(STR("WARPSECTOR error: sector ") + std::to_wstring(secid) + STR(" data unreadable"));
            return STR("error: sector data unreadable");
        }
        // a docked ship undocks first: job_start "undock" answers "ok already free" when it is not docked, else starts the undock job
        g_job_res.erase(sid);
        const std::wstring ud = job_start(sid, "undock", -1);
        if (ud == STR("waiting"))
        {
            g_warp_wait[sid] = arg;
            log_line(STR("WARPSECTOR undocking first ship=") + std::to_wstring(sid) + STR(" sector=") + std::to_wstring(secid));
            return STR("ok undocking first");
        }
        if (ud.rfind(STR("ok"), 0) != 0) { log_line(STR("WARPSECTOR error: undock failed: ") + ud); return STR("error: undock failed: ") + ud; }
        log_line(STR("WARPSECTOR undocked (") + ud + STR("), warping ship=") + std::to_wstring(sid));
        int64_t arr[3] = {0, 0, 0};
        std::wstring err;
        if (!arrival_of(ship, sys, ch, dist, arr, err))
        {
            log_line(STR("WARPSECTOR error: arrival failed: ") + err);
            return STR("error: ") + err;
        }
        const double rxy = std::sqrt(static_cast<double>(arr[0]) * static_cast<double>(arr[0]) + static_cast<double>(arr[1]) * static_cast<double>(arr[1]));
        // dist 0 (plain asteroid fields): the game's arrival ring is 400 m (raw 26214400, probe)
        const double want = dist > 0 ? static_cast<double>(dist) : 26214400.0;
        const bool plausible = std::fabs(rxy - want) <= 0.02 * want;
        log_line(STR("WARPSECTOR input ship=") + std::to_wstring(sid) + STR(" sector=") + std::to_wstring(secid) + STR(" sys=") + std::to_wstring(sys) +
                 STR(" chunk=") + trio_text(ch) + STR(" arrival_dist=") + std::to_wstring(dist) + STR(" -> arrival=") + trio_text(arr) +
                 STR(" |xy|=") + std::to_wstring(static_cast<long long>(rxy)) + STR(" plausible=") + (plausible ? STR("yes") : STR("no")));
        if (secid == 1008)
        {
            const bool known = std::llabs(arr[0] + 51644600LL) <= 5000 && std::llabs(arr[1] - 75833800LL) <= 5000 && std::llabs(arr[2] - 14000LL) <= 5000;
            log_line(std::wstring(STR("WARPSECTOR known 1008 arrival (-51644600,75833800,14000) match=")) + (known ? STR("yes") : STR("no")));
        }
        if (!plausible) return STR("error: implausible arrival point (see log), no warp");
        // fuel before the teleport is read here, not after: the teleport may already have spent it
        int32_t f0 = -1, c0 = -1;
        std::wstring ce;
        call_int(ship, STR("GetAvailableJumpFuel"), f0);
        jump_cost(ship, sys, ch, c0, ce);
        log_line(STR("FUELWARP before drive=") + std::to_wstring(f0) + STR(" jump_cost=") + std::to_wstring(c0));
        if (!warp_send(sid, sys, ch, arr, false, err))
        {
            log_line(STR("WARPSECTOR error: teleport failed: ") + err);
            return STR("error: ") + err;
        }
        g_warp_chk = WarpChk{};
        g_warp_chk.fuel0 = f0;
        g_warp_chk.sid = sid;
        g_warp_chk.sys = sys;
        for (int i = 0; i < 3; ++i) { g_warp_chk.ch[i] = ch[i]; g_warp_chk.arr[i] = arr[i]; }
        g_warp_chk.t = 1.5;
        g_warp_chk.pend = true;
        log_line(STR("WARPSECTOR sent ship=") + std::to_wstring(sid) + STR(" wrap=0 (first try)"));
        return STR("ok");
    }

    // WHAT: warp_sector for the ship whose display name is the text before the last ':' ("warp_by_name:Miner 1:pinned").
    // WHY: the Dev Menu buttons know the ship name, not its id.
    std::wstring warp_by_name_cmd(const std::string& rest)
    {
        const size_t c = rest.rfind(':');
        if (c == std::string::npos || c == 0) return STR("error: want warp_by_name:<ship name>:<sector id|pinned>");
        const std::wstring name = widen(rest.substr(0, c));
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* s : ships)
        {
            if (!alive(s) || obj_display(s) != name) continue;
            return warp_sector_cmd(obj_id(s), rest.substr(c + 1));
        }
        return STR("error: ship '") + name + STR("' not found");
    }

    // WHAT: id of the DMSPlayerShipObject the player pilots: the alive ship whose PilotID equals the player controller's id.
    //       No match: logs every ship's PilotID/PilotConsoleID, then uses the only ship with PilotConsoleID > 0. 0 when neither works.
    // WHY: the cheat warps "the ship I am flying" without a ship name. The PilotID mapping is UNVERIFIED: the WARPCUR log shows which path matched.
    int current_ship_id()
    {
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) return 0;
        const int cid = obj_id(ctrl);
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        std::wstring list;
        int con_id = 0, con_n = 0, pil_id = 0, pil_n = 0;
        for (UObject* s : ships)
        {
            if (!alive(s)) continue;
            int32_t pilot = -1, con = 0;
            get_int_prop(s, STR("PilotID"), pilot);
            get_int_prop(s, STR("PilotConsoleID"), con);
            const int id = obj_id(s);
            list += STR("[") + std::to_wstring(id) + STR("/") + std::to_wstring(pilot) + STR("/") + std::to_wstring(con) + STR("] ");
            if (pilot == cid)
            {
                log_line(STR("WARPCUR ship ") + std::to_wstring(id) + STR(" matched PilotID==ctrl ") + std::to_wstring(cid) + STR(" (UNVERIFIED mapping)"));
                return id;
            }
            if (pilot > 0) { pil_id = id; ++pil_n; }
            if (con > 0) { con_id = id; ++con_n; }
        }
        log_line(STR("WARPCUR no ship with PilotID==ctrl ") + std::to_wstring(cid) + STR("; ships id/PilotID/PilotConsoleID: ") + list);
        if (pil_n == 1)
        {
            log_line(STR("WARPCUR single piloted ship (PilotID>0) = ") + std::to_wstring(pil_id));
            return pil_id;
        }
        if (con_n == 1)
        {
            log_line(STR("WARPCUR fallback: single ship with PilotConsoleID>0 = ") + std::to_wstring(con_id) + STR(" (UNVERIFIED mapping)"));
            return con_id;
        }
        return 0;
    }

    // WHAT: 1.5 s after a warp, re-reads the ship (system, chunk, location, JumpTargetLocation) and logs "WARPSECTOR result ... match=yes/no".
    //       match = same system and chunk as the target. When there is no match and no retry yet, sends the same warp once more with the SetPilotingInput wrapper.
    // WHY: the only way to see from the log whether the teleport landed and which variant worked. Called from the command poll in cmd_tick.
    void warp_check(double dt)
    {
        if (!g_warp_chk.pend) return;
        g_warp_chk.t -= dt;
        if (g_warp_chk.t > 0.0) return;
        g_warp_chk.pend = false;
        const WarpChk w = g_warp_chk;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), w.sid);
        if (!alive(ship)) { log_line(STR("WARPSECTOR result: ship ") + std::to_wstring(w.sid) + STR(" gone")); return; }
        const int sys = sys_id(ship);
        int64_t c[3] = {0, 0, 0};
        const bool ok = get_chunk(ship, c);
        double l[3] = {0, 0, 0};
        get_loc(ship, l);
        // absolute position: the game renormalises chunk + location across chunk borders (chunk 2048 m = 134217728 raw), so compare the sum, tolerance 100 m (6553600 raw)
        bool match = ok && sys == w.sys;
        for (int i = 0; match && i < 3; ++i)
            if (std::fabs((static_cast<double>(c[i]) - static_cast<double>(w.ch[i])) * 134217728.0 + l[i] - static_cast<double>(w.arr[i])) > 6553600.0) match = false;
        log_line(STR("WARPSECTOR result ship=") + std::to_wstring(w.sid) + STR(" wrap=") + (w.wrap ? STR("1") : STR("0")) + STR(" sys=") + std::to_wstring(sys) +
                 STR(" chunk=") + (ok ? trio_text(c) : std::wstring(STR("n/a"))) + STR(" loc=(") + std::to_wstring(static_cast<long long>(l[0])) + STR(",") +
                 std::to_wstring(static_cast<long long>(l[1])) + STR(",") + std::to_wstring(static_cast<long long>(l[2])) + STR(") jump_target=") +
                 pvec_text(ship, STR("JumpTargetLocation")) + STR(" match=") + (match ? STR("yes") : STR("no")));
        int32_t f1 = -1;   // fuel after the teleport: lower than before = the warp spent fuel
        call_int(ship, STR("GetAvailableJumpFuel"), f1);
        log_line(STR("FUELWARP after drive=") + std::to_wstring(f1) + STR(" before=") + std::to_wstring(w.fuel0) + STR(" consumed=") + (f1 >= 0 && w.fuel0 >= 0 && f1 < w.fuel0 ? STR("yes") : STR("no")));
        if (match) return;
        if (w.retried)
        {
            log_line(STR("WARPSECTOR no move with or without the wrapper"));
            return;
        }
        std::wstring err;
        if (!warp_send(w.sid, w.sys, w.ch, w.arr, true, err)) { log_line(STR("WARPSECTOR retry failed: ") + err); return; }
        g_warp_chk.pend = true;
        g_warp_chk.t = 1.5;
        g_warp_chk.retried = true;
        g_warp_chk.wrap = true;
        log_line(STR("WARPSECTOR retry sent ship=") + std::to_wstring(w.sid) + STR(" wrap=1 (SetPilotingInput around the teleport)"));
    }

    // WHAT: sends the pending warp of each ship whose undock job has ended (ship in g_warp_wait). Called from cmd_tick after jobs_tick.
    // WHY: warp_sector_cmd returns at once while a docked ship undocks; the warp follows when the undock is done.
    void warp_wait_tick()
    {
        for (auto it = g_warp_wait.begin(); it != g_warp_wait.end();)
        {
            const int sid = it->first;
            const std::string arg = it->second;
            bool busy = false;
            for (const DockJob& j : g_jobs) if (j.sid == sid) busy = true;
            if (busy) { ++it; continue; }
            it = g_warp_wait.erase(it);
            const std::wstring r = g_job_res[sid];
            if (r.rfind(STR("ok"), 0) != 0) { log_line(STR("WARPSECTOR undock failed ship=") + std::to_wstring(sid) + STR(": ") + r + STR(", no warp")); continue; }
            log_line(STR("WARPSECTOR undock done ship=") + std::to_wstring(sid) + STR(" (") + r + STR("), warping"));
            const std::wstring w = warp_sector_cmd(sid, arg);
            if (w.rfind(STR("ok"), 0) != 0) log_line(STR("WARPSECTOR warp after undock failed: ") + w);
        }
    }
}
