// FILE: mods/MiningBenchMod/mb_warpreal.hpp
// WHAT: real warp = variant e only (ShipTeleportInput bSetTarget, own aim, ShipMovementInputCommand DeltaThrottle +1). warp_real_cmd(sid, '<id|pinned>').
//       The game jumps the ship with warp fuel, like the autopilot. Every step logs WARPREAL and FUELWARP lines.
// WHY separate: research state machine with its own timers; it runs only from the command poll (warp_real_tick), never from a hook.
// DEPENDS ON: mb_warp.hpp (current_ship_id, sys_id, get_chunk, sec_axes, sec_warp_dist, arrival_of, jump_cost, trio_text, warp_send, pilot_console_of, add_input),
//             mb_dockjob.hpp (job_start, g_jobs), mb_orders.hpp (set_autopilot), mb_ue.hpp (find_by_id, obj_id, get_int_prop, get_bool_prop, set_bool_prop, set_int_prop, pvec_text, call_int),
//             mb_pin.hpp (pinned_now), mb_cmdstate.hpp (g_job_res), mb_compat.hpp (g_compat: send_ship_throttle is refused when WARPREAL is off).
// USED BY: mb_mining.hpp (belt warp, g_wr_wait, warp_cancel),mb_testcmd.hpp (run_cmd: warp_cur:; cmd_tick: warp_real_tick).
#pragma once

#include <cstring>
#include <map>
#include <string>

namespace
{
    // WHAT: state of the running warp_real. Ids only (no UObject*). var: 0 idle, 5 e (the only variant).
    // WHY: the run spans several ticks; the tick needs the target and the timers.
    struct WarpReal
    {
        int sid = 0, secid = 0, sys = 0;
        int64_t ch[3] = {0, 0, 0};
        int64_t dist = 0;          // sector GenParams.WarpArrivalDistance (0 = plain field)
        int var = 0;
        double t = 0.0;            // seconds left in the run
        double st = 0.0;           // seconds elapsed in the run
        double wacc = 0.0;         // watch log timer (1 s)
        int stage = 0;             // variant e: 1 aim + wait aligned, 2 thrust on, 3 done
        int32_t fuel0 = -1;        // GetAvailableJumpFuel before the run
        int cid = 0;               // variant e: pilot console id for the aim offset
        double aim_t = 0.0;        // variant e: aim resend timer
        bool al_sent = false;      // variant e: (0,0) already sent for the current aligned spell
        int hyp = 0;               // variant e: quaternion / yaw-sign combo in use
        double hyp_t = 0.0, err0 = -1.0;   // variant e: time in this combo, heading error when it started
        int ord = 0, ord_base = 0; // variant e: aim log counter; aligned ticks, then thrust start tick
    } g_wr;
    // WHAT: ships waiting for their undock before warp_real: ship id -> arg as warp_real_cmd got it. WHY: same as g_warp_wait.
    std::map<int, std::string> g_wr_wait;
    const TCHAR* const kVn[] = {STR("none"), STR("a"), STR("b"), STR("c"), STR("d"), STR("e"), STR("f"), STR("g"), STR("h"), STR("i")};

    // WHAT: fuel after the run and whether it dropped (FUELWARP real after ...), same text style as warp_check.
    void wr_fuel_after(UObject* ship)
    {
        int32_t f1 = -1;
        call_int(ship, STR("GetAvailableJumpFuel"), f1);
        log_line(std::wstring(STR("FUELWARP real after drive=")) + std::to_wstring(f1) + STR(" before=") + std::to_wstring(g_wr.fuel0) +
                 STR(" consumed=") + (f1 >= 0 && g_wr.fuel0 >= 0 && f1 < g_wr.fuel0 ? STR("yes") : STR("no")));
    }

    // WHAT: ends the run (budget used up or ship lost): log line, state reset. Only variant e exists now, so there is no next variant.
    void wr_end()
    {
        log_line(std::wstring(STR("WARPREAL only variant ")) + kVn[g_wr.var] + STR(" failed"));
        g_wr.var = 0;
    }

    // WHAT: aligned / JumpAnimProgress / JumpProgress / ChargeToJump of the ship as text, for the variant e watch line.
    std::wstring wr_e_state(UObject* ship)
    {
        bool al = false;
        int32_t a = -1, j = -1, c = -1;
        get_bool_prop(ship, STR("bAlignedWithJumpTarget"), al);
        get_int_prop(ship, STR("JumpAnimProgress"), a);
        get_int_prop(ship, STR("JumpProgress"), j);
        get_int_prop(ship, STR("ChargeToJump"), c);
        return std::wstring(STR("aligned=")) + (al ? STR("1") : STR("0")) + STR(" anim=") + std::to_wstring(a) + STR(" jump=") + std::to_wstring(j) + STR(" charge=") + std::to_wstring(c);
    }

    // WHAT: variant e start: the player's J-warp. Sends ShipTeleportInput with bSetTarget=1 (sets the jump target on the ship, no teleport).
    //       wr_e_step then waits for bAlignedWithJumpTarget (own aim via wr_e_aim) and applies Throttle +1 once, like the thrust press.
    void wr_e_start(UObject* ship)
    {
        std::wstring err;
        int64_t arr[3] = {0, 0, 0};
        const int64_t dist = g_wr.dist > 0 ? g_wr.dist : 26214400;
        if (!arrival_of(ship, g_wr.sys, g_wr.ch, dist, arr, err)) { log_line(std::wstring(STR("WARPREAL e arrival failed: ")) + err); wr_end(); return; }
        const bool ok = warp_send(g_wr.sid, g_wr.sys, g_wr.ch, arr, false, err, true);
        log_line(std::wstring(STR("WARPREAL e set-target command ")) + (ok ? STR("sent arrival ") + trio_text(arr) : err));
        if (!ok) { wr_end(); return; }
        { size_t np = 0; std::wstring how; UObject* con = pilot_console_of(g_wr.sid, how, np); g_wr.cid = alive(con) ? obj_id(con) : 0; }
        log_line(std::wstring(STR("WARPREAL e console id=")) + std::to_wstring(g_wr.cid));
        g_wr.stage = 1;
    }

    // WHAT: variant e aim: direction to the jump target from the positions (JumpTargetDirection stays 0 for ships the player does not fly):
    //       delta = (target chunk - ship chunk) * 134217728 + (JumpTargetLocation - ship Location), rotated into the ship axes with Quat.
    //       Then the proven view offset (view_send), same maths as view_aim_drive with fw = body X, R = body Y, U = body Z.
    //       The quaternion convention and axis sign are not known, so 4 combos (g_wr.hyp bit0 = inverse quat, bit1 = flip yaw) are tried in turn:
    //       when err has not dropped by 5 deg 4 s after a combo started, the next one starts. WARPREAL e aim lines show err, hyp and the body direction.
    void wr_e_aim(UObject* ship, double dt)
    {
        g_wr.aim_t += dt;
        g_wr.hyp_t += dt;
        if (g_wr.aim_t < MV_VIEW_PERIOD_S) return;
        g_wr.aim_t = 0.0;
        int64_t tl[3], tc[3], sc[3];
        double sl[3], q[4];
        if (!sec_axes(ship, STR("JumpTargetLocation"), tl) || !sec_axes(ship, STR("JumpTargetChunkLocation"), tc) || !get_chunk(ship, sc) || !get_loc(ship, sl) || !quat4(ship, q)) return;
        double d[3];
        for (int i = 0; i < 3; ++i) d[i] = static_cast<double>(tc[i] - sc[i]) * 134217728.0 + (static_cast<double>(tl[i]) - sl[i]);
        const double dn = norm3(d);
        if (dn < 1.0) return;
        double qq[4] = {q[0], q[1], q[2], q[3]};
        if (g_wr.hyp & 1) qconj(q, qq);
        // body = conj(qq) rotated d  (qq maps body -> world, so world -> body uses the conjugate)
        const double w = qq[0], u[3] = {-qq[1], -qq[2], -qq[3]};
        double t[3], uxt[3], b[3];
        cross3(u, d, t);
        for (double& x : t) x *= 2.0;
        cross3(u, t, uxt);
        for (int i = 0; i < 3; ++i) b[i] = d[i] + w * t[i] + uxt[i];
        const double bn = norm3(b);
        for (double& x : b) x /= bn;
        const double err = std::acos(std::clamp(b[0], -1.0, 1.0)) * 180.0 / MV_PI;
        if (g_wr.err0 < 0.0 || g_wr.hyp_t >= 4.0)
        {
            if (g_wr.err0 >= 0.0 && err > g_wr.err0 - 5.0 && err >= MV_FACE_DONE_DEG) { g_wr.hyp = (g_wr.hyp + 1) % 4; log_line(std::wstring(STR("WARPREAL e aim: no progress, next hyp=")) + std::to_wstring(g_wr.hyp)); }
            g_wr.err0 = err;
            g_wr.hyp_t = 0.0;
        }
        std::wstring e;
        double yaw = 0.0, pit = 0.0;
        if (err < MV_FACE_DONE_DEG)
        {
            if (!g_wr.al_sent) { g_wr.al_sent = true; view_send(g_wr.cid, 0, 0, e); }
        }
        else
        {
            g_wr.al_sent = false;
            const double ab[3] = {0.0, -b[2], b[1]};   // cross(body X, b) on (fw, R, U)
            const double an = std::sqrt(ab[1] * ab[1] + ab[2] * ab[2]);
            const double mag = std::min(err + MV_VIEW_DEADZONE_DEG, MV_VIEW_MAX_DEG);
            yaw = an > 1e-9 ? mag * dot3(ab, g_view_up) / an : 0.0;
            pit = an > 1e-9 ? mag * dot3(ab, g_view_pitch) / an : 0.0;
            if (g_wr.hyp & 2) yaw = -yaw;
            const int64_t yaw_raw = static_cast<int64_t>(std::llround(MV_VIEW_YAW_SIGN * yaw * VIEW_RAW_PER_DEG));
            const int64_t pit_raw = static_cast<int64_t>(std::llround(MV_VIEW_PITCH_SIGN * pit * VIEW_RAW_PER_DEG));
            if (!view_send(g_wr.cid, yaw_raw, pit_raw, e)) { log_line(std::wstring(STR("WARPREAL e view_send failed: ")) + e); return; }
        }
        if (++g_wr.ord % 10 == 1)
            log_line(std::wstring(STR("WARPREAL e aim err=")) + std::to_wstring(err) + STR(" hyp=") + std::to_wstring(g_wr.hyp) + STR(" yaw=") + std::to_wstring(yaw) +
                     STR(" pit=") + std::to_wstring(pit) + STR(" body_dir=(") + std::to_wstring(b[0]) + STR(",") + std::to_wstring(b[1]) + STR(",") + std::to_wstring(b[2]) + STR(")"));
    }

    // WHAT: the command type id of ShipMovementInputCommand (int32 at cmd+kCmdTypeOff), and that offset. Checked by the compat self-test (mb_compat_run.hpp).
    constexpr int32_t kThrottleTypeId = 0x3a;
    constexpr size_t kCmdTypeOff = 0x28;

    // WHAT: sends a ShipMovementInputCommand{DeltaThrottle = delta} to ship sid with AddInputCommand (no pilot, no seat).
    // WHY: Ghidra (part6f-ghidra-findings): the jump charge starts when the ship handles this command with DeltaThrottle=+1 (ChangeThrottle) while
    //      bJumpTargetSet and bAlignedWithJumpTarget hold. MSCore_throttle builds the ShipMovementInputCommand and sets its type id.
    bool send_ship_throttle(int sid, int delta, std::wstring& err)
    {
        if (!g_compat.usable(F_WARPREAL)) { err = STR("COMPAT: ") + widen(g_compat.reason(F_WARPREAL)); return false; }
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        int ok = g_ms.throttle(sid, delta, e, 256);
        if (!ok) err = e;
        return ok != 0;
    }

    // WHAT: thrust for variant e: ShipMovementInputCommand to the ship (works for unpiloted ships).
    void wr_e_thrust(int v)
    {
        std::wstring err;
        if (v != 0 && !send_ship_throttle(g_wr.sid, v, err)) log_line(std::wstring(STR("WARPREAL e throttle command failed: ")) + err);
    }

    // WHAT: variant e step. Stage 1: aligned for 0.5 s -> Throttle = 1 (stage 2). Stage 2: 1.5 s later Throttle = 0 (stage 3).
    void wr_e_step(UObject* ship, double dt)
    {
        bool al = false;
        if (g_wr.stage == 1) wr_e_aim(ship, dt);
        get_bool_prop(ship, STR("bAlignedWithJumpTarget"), al);
        if (g_wr.stage == 1)
        {
            g_wr.ord_base = al ? g_wr.ord_base + 1 : 0;   // counts ticks while aligned
            if (g_wr.ord_base >= 3)
            {
                std::wstring e;
                view_send(g_wr.cid, 0, 0, e);
                wr_e_thrust(1);
                log_line(STR("WARPREAL e aligned: thrust 1"));
                g_wr.ord = 0; g_wr.stage = 2;
                g_wr.ord_base = static_cast<int>(g_wr.st * 10);
            }
        }
        else if (g_wr.stage == 2)
        {
            if (g_wr.st * 10 - g_wr.ord_base < 30) { wr_e_thrust(1); return; }
            wr_e_thrust(0);
            log_line(STR("WARPREAL e thrust 0"));
            g_wr.stage = 3;
        }
    }

    // WHAT: starts the variant e run (var 5): 60 s budget, logs the begin line with the ship's current system and chunk.
    void wr_start(int var)
    {
        g_wr.var = var;
        g_wr.t = 60.0;
        g_wr.st = 0.0;
        g_wr.wacc = 0.0;
        g_wr.stage = 0;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), g_wr.sid);
        if (!alive(ship)) { log_line(STR("WARPREAL ship gone, run stopped")); g_wr.var = 0; return; }
        int64_t cc[3] = {0, 0, 0};
        get_chunk(ship, cc);
        log_line(std::wstring(STR("WARPREAL variant=")) + kVn[var] + STR(" begin ship=") + std::to_wstring(g_wr.sid) + STR(" from sys=") +
                 std::to_wstring(sys_id(ship)) + STR(" chunk=") + trio_text(cc) + STR(" to sys=") + std::to_wstring(g_wr.sys) + STR(" chunk=") + trio_text(g_wr.ch));
        wr_e_start(ship);
    }

    // WHAT: cancels every warp of ship sid not yet charging: pending undock-then-warp (g_warp_wait, g_wr_wait) and a warp_real run at stage 0/1. A run at stage >= 2 finishes.
    void warp_cancel(int sid)
    {
        if (g_warp_wait.erase(sid)) log_line(STR("WARPCANCEL ship=") + std::to_wstring(sid) + STR(" pending instant warp removed"));
        if (g_wr_wait.erase(sid)) log_line(STR("WARPCANCEL ship=") + std::to_wstring(sid) + STR(" pending real warp removed"));
        if (g_wr.var != 0 && g_wr.sid == sid && g_wr.stage <= 1)
        {
            std::wstring e;
            if (g_wr.cid != 0) view_send(g_wr.cid, 0, 0, e);
            g_wr.var = 0;
            log_line(STR("WARPCANCEL ship=") + std::to_wstring(sid) + STR(" real warp run stopped before charging"));
        }
    }

    // WHAT: warp_real for ship sid: sector arg "<id|pinned>". Resolves the sector like warp_sector_cmd, undocks first (deferred via g_wr_wait),
    //       logs FUELWARP before, then starts variant e. Returns "ok" or "error: ...".
    // WHY: the user wants the warp the autopilot does (with warp fuel). One run at a time (g_wr is one state).
    std::wstring warp_real_cmd(int sid, const std::string& arg)
    {
        if (!g_compat.usable(F_WARPREAL)) return STR("error: COMPAT warp disabled (") + widen(g_compat.done ? g_compat.reason(F_WARPREAL) : std::string("self-test pending")) + STR(")");
        if (g_wr.var != 0) { g_wr_wait[sid] = arg; g_job_res.erase(sid); log_line(STR("WARPREAL queued ship=") + std::to_wstring(sid) + STR(" (another warp running)")); return STR("ok queued"); }   // ponytail: one warp at a time, the rest wait their turn in g_wr_wait
        if (sid == 0) sid = current_ship_id();   // Dev Menu sends ship 0 = the ship the player is piloting
        const std::string secs = arg;   // "<sectorId|pinned>"
        const int first = 5;            // variant e is the only one left
        const int secid = secs == "pinned" ? pinned_now() : std::atoi(secs.c_str());
        if (secid <= 0) return STR("error: no sector (pin a sector on the map first)");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        UObject* secobj = find_by_id(STR("SolarSystemSector"), secid);
        if (!alive(secobj)) return STR("error: sector ") + std::to_wstring(secid) + STR(" not found");
        int64_t ch[3] = {0, 0, 0}, dist = 0;
        int32_t sys = 0;
        if (!sec_axes(secobj, STR("ChunkLocation"), ch) || !get_int_prop(secobj, STR("ParentID"), sys) || !sec_warp_dist(secobj, dist))
        {
            log_line(STR("WARPREAL error: sector ") + std::to_wstring(secid) + STR(" data unreadable"));
            return STR("error: sector data unreadable");
        }
        // a docked ship undocks first (same as warp_sector_cmd)
        g_job_res.erase(sid);
        const std::wstring ud = job_start(sid, "undock", -1);
        if (ud == STR("waiting"))
        {
            g_wr_wait[sid] = arg;
            log_line(STR("WARPREAL undocking first ship=") + std::to_wstring(sid) + STR(" sector=") + std::to_wstring(secid));
            return STR("ok undocking first");
        }
        if (ud.rfind(STR("ok"), 0) != 0) { log_line(STR("WARPREAL error: undock failed: ") + ud); return STR("error: undock failed: ") + ud; }
        g_wr = WarpReal{};
        g_wr.sid = sid;
        g_wr.secid = secid;
        g_wr.sys = sys;
        g_wr.dist = dist;
        for (int i = 0; i < 3; ++i) g_wr.ch[i] = ch[i];
        int32_t f0 = -1, c0 = -1;
        std::wstring ce;
        call_int(ship, STR("GetAvailableJumpFuel"), f0);
        jump_cost(ship, sys, ch, c0, ce);
        g_wr.fuel0 = f0;
        log_line(STR("FUELWARP real before drive=") + std::to_wstring(f0) + STR(" jump_cost=") + std::to_wstring(c0));
        log_line(STR("WARPREAL start ship=") + std::to_wstring(sid) + STR(" sector=") + std::to_wstring(secid) + STR(" sys=") + std::to_wstring(sys) +
                 STR(" chunk=") + trio_text(ch) + STR(" variants=") + kVn[first]);
        wr_start(first);
        return STR("ok");
    }

    // WHAT: called every command poll (cmd_tick). Runs deferred undocks, then the active run: its steps, the result check
    //       (system + chunk = sector), the watch line every second, and the 60 s timeout.
    void warp_real_tick(double dt)
    {
        for (auto it = g_wr_wait.begin(); it != g_wr_wait.end();)
        {
            const int sid = it->first;
            const std::string arg = it->second;
            bool busy = false;
            for (const DockJob& j : g_jobs) if (j.sid == sid) busy = true;
            if (busy || g_wr.var != 0) { ++it; continue; }   // undock still running, or another warp holds g_wr
            it = g_wr_wait.erase(it);
            const std::wstring r = g_job_res[sid];
            if (!r.empty() && r.rfind(STR("ok"), 0) != 0) { log_line(STR("WARPREAL undock failed ship=") + std::to_wstring(sid) + STR(": ") + r + STR(", no warp")); continue; }
            log_line(STR("WARPREAL undock done ship=") + std::to_wstring(sid) + STR(" (") + r + STR("), starting"));
            const std::wstring w = warp_real_cmd(sid, arg);
            if (w.rfind(STR("ok"), 0) != 0) log_line(STR("WARPREAL start after undock failed: ") + w);
        }
        if (g_wr.var == 0) return;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), g_wr.sid);
        if (!alive(ship)) { log_line(STR("WARPREAL ship gone, run stopped")); g_wr.var = 0; return; }
        g_wr.t -= dt;
        g_wr.st += dt;
        g_wr.wacc += dt;
        if (g_wr.var == 5) wr_e_step(ship, dt);
        const int sys = sys_id(ship);
        int64_t c[3] = {0, 0, 0};
        const bool cok = get_chunk(ship, c);
        // arrival renormalises across chunk edges (arrival y rolled to chunk +1), so +-1 chunk counts as there
        auto near1 = [](int64_t x, int64_t y) { return x - y <= 1 && y - x <= 1; };
        const bool match = cok && sys == g_wr.sys && near1(c[0], g_wr.ch[0]) && near1(c[1], g_wr.ch[1]) && near1(c[2], g_wr.ch[2]);
        if (g_wr.wacc >= 1.0)
        {
            g_wr.wacc = 0.0;
            bool ap = false, jts = false;
            int32_t idx = -1, f = -1;
            get_bool_prop(ship, STR("bAutopilot"), ap);
            get_int_prop(ship, STR("CurrentOrderIndex"), idx);
            get_bool_prop(ship, STR("bJumpTargetSet"), jts);
            call_int(ship, STR("GetAvailableJumpFuel"), f);
            log_line(std::wstring(STR("WARPREAL watch variant=")) + kVn[g_wr.var] + STR(" autopilot=") + (ap ? STR("1") : STR("0")) +
                     STR(" order_idx=") + std::to_wstring(idx) + STR(" jump_set=") + (jts ? STR("1") : STR("0")) +
                     STR(" jump_loc=") + pvec_text(ship, STR("JumpTargetLocation")) + STR(" sys=") + std::to_wstring(sys) +
                     STR(" chunk=") + (cok ? trio_text(c) : std::wstring(STR("n/a"))) + STR(" fuel=") + std::to_wstring(f) + (g_wr.var >= 5 ? STR(" ") + wr_e_state(ship) : std::wstring()));
        }
        if (match)
        {
            log_line(std::wstring(STR("WARPREAL result variant=")) + kVn[g_wr.var] + STR(" match=yes"));
            wr_fuel_after(ship);
            std::wstring err;
            set_autopilot(g_wr.sid, false, err);
            g_wr.var = 0;
            return;
        }
        if (g_wr.t > 0.0) return;
        log_line(std::wstring(STR("WARPREAL variant=")) + kVn[g_wr.var] + STR(" timeout match=no"));
        wr_fuel_after(ship);
        std::wstring err;
        set_autopilot(g_wr.sid, false, err);
        wr_end();
    }
}
