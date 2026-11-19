// FILE: mods/ShipControl/sc_bubble_run.hpp
// WHAT: bubble engine of ShipControl (SC3 A2): one pass per ship and tick (profile, flight snapshot, waypoint resume, activation gate, ladder,
//       fix), the host wrappers (fail closed) and sc_bubble_tick (SEH per ship).
// WHY: ported 1:1 from MiningBenchMod mb_bubble.hpp (bubble_tick, bubble_one, bub_exec). The mining side answers the host callbacks
//      (registry query, near rock, flight start, replan / skip bodies); no game names here.
// DEPENDS ON (exact): sc_move.hpp (sc_seh_run, host_logw, hex32, ms_flight, ms_set_throttle, frame_from_flight, g_spd, SC_RAW_PER_M),
//                     sc_bubble.hpp (g_bub), sc_logic_bubble.hpp (ladder rules), sc_logic_path.hpp (path_waypoint, block_r_m),
//                     sc_math.hpp (dist3), sc_logic_steer.hpp (SM_UU_PER_M), windows.h, <algorithm>, <cmath>, <cstring>, <map>, <string>, <vector>
// USED BY: dllmain.cpp (SC_tick_bubble). Not for tests (sc_move.hpp only).
// GAME FACTS: none. Units: m, m/s, s; the ship frame is in uu (100 per m), flight points in chunk + raw (65536 per m).
#pragma once

#include "sc_move.hpp"
#include "sc_bubble.hpp"
#include "sc_logic_bubble.hpp"
#include "sc_logic_path.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    constexpr ULONGLONG BUBBLE_OFF_MS = 10000;        // a ship over the pass budget is not checked for this long (ms)
    constexpr ULONGLONG BUBBLE_SLOW_LOG_MS = 30000;   // 'BUBBLE slow' line at most this often (ms)

    std::map<int, double> g_bub_r;         // sid -> ship collision radius (m), cached from the first read with FF_COLL
    std::map<int, ULONGLONG> g_bub_off;    // sid -> GetTickCount64 until which the ship is not checked (slow pass)
    ULONGLONG g_bub_slow_ms = 0;           // GetTickCount64 of the last 'BUBBLE slow' line (0 = none yet)

    // WHAT: narrow ASCII text to wide (log text of the legacy lines). Local so this header needs no UE4SS helpers.
    std::wstring bub_text(const char* s)
    {
        std::wstring w;
        for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
        return w;
    }

    // WHAT: host profile of ship sid. false = null, throw or refused (the ship's state is erased by the caller).
    bool host_profile(const ScHost& h, int sid, ScBubbleProfile& pr)
    {
        if (!h.bubble_profile) return false;
        try { return h.bubble_profile(sid, &pr) == 1; } catch (...) { return false; }
    }

    // WHAT: host blocker query on segment p..b. Returns the blocker id (0 = clear, < 0 = ship sphere); out filled by the host. Null or throw = 0 (clear).
    int host_first_blocker(const ScHost& h, int sid, int sys, int phase, int rock, const double p[3], const double b[3], double clear_m, ScBlockHit& out)
    {
        if (!h.first_blocker) return 0;
        try { return h.first_blocker(sid, sys, phase, rock, p, b, clear_m, &out); } catch (...) { return 0; }
    }

    // WHAT: host near-rock test of ship sid at p (m). true = too near a rock; nc / nr = rock centre (m) and radius (m). Null or throw = false.
    bool host_near_rock(const ScHost& h, int sid, const double p[3], double ship_r, double nc[3], double& nr)
    {
        if (!h.near_rock) return false;
        try { return h.near_rock(sid, p, ship_r, nc, &nr) == 1; } catch (...) { return false; }
    }

    // WHAT: host event of the ladder. true = done (for FLY: the flight started). Null or throw = false.
    bool host_on_bubble(const ScHost& h, int sid, const ScBubbleEvent& ev)
    {
        if (!h.on_bubble) return false;
        try { return h.on_bubble(sid, &ev) == 1; } catch (...) { return false; }
    }

    // WHAT: collision radius (m) of ship sid from the flight snapshot. Cached once read (FF_COLL); else BUBBLE_SHIP_R_M (not cached).
    double bub_ship_r(int sid, const MsFlight& f)
    {
        auto it = g_bub_r.find(sid);
        if (it != g_bub_r.end()) return it->second;
        if ((f.flags & FF_COLL) && f.coll_radius_raw > 0)
        {
            const double r = ship_r_from_flight(true, f.coll_radius_raw);
            g_bub_r[sid] = r;
            return r;
        }
        return BUBBLE_SHIP_R_M;
    }

    // WHAT: sends one FLY event for ship sid: a flight to the point wp (m), in the chunk + raw loc of the snapshot f (ship at p, m).
    //       false when the object pose or the system is unknown, or the host refused.
    bool bubble_fly(const ScHost& h, int sid, const MsFlight& f, const double p[3], const double wp[3], int reason)
    {
        if (!(f.flags & FF_OBJ_POSE) || f.sys <= 0) return false;
        ScBubbleEvent ev{};
        ev.size = static_cast<int32_t>(sizeof(ScBubbleEvent));
        ev.kind = 3;
        ev.reason = reason;
        ev.sys = f.sys;
        bubble_fly_point(p, wp, f.ch, f.loc_raw, ev.ch, ev.loc);
        return host_on_bubble(h, sid, ev);
    }

    // WHAT: sends a REPLAN (kind 1) or SKIP (kind 2) event of ship sid with the ladder state s0 (the state of the hit).
    void bubble_event(const ScHost& h, int sid, const MsFlight& f, int kind, const BubbleState& s0, int act)
    {
        ScBubbleEvent ev{};
        ev.size = static_cast<int32_t>(sizeof(ScBubbleEvent));
        ev.kind = kind;
        ev.phase = s0.phase;
        ev.act = act;
        ev.blk = s0.blk;
        ev.sys = f.sys;
        ev.blk_d = s0.blk_d;
        host_on_bubble(h, sid, ev);
    }

    // WHAT: carries out the fix of one ladder event (legacy bub_exec). REPLAN and SKIP go to the host; a waypoint (or nudge) is flown
    //       through the host; brake only clears resume. p = ship (m), cr = ship radius (m). The state is re-found after every host call.
    void bubble_exec(const ScHost& h, int sid, const ScBubbleProfile& pr, const MsFlight& f, const double p[3], double cr, int act)
    {
        auto it = g_bub.find(sid);
        if (it == g_bub.end()) return;
        const BubbleState s0 = it->second;
        double nc[3] = {0, 0, 0}, nr = 0.0;
        const bool nrock = host_near_rock(h, sid, p, cr, nc, nr);
        // a waypoint needs the flight destination and the blocker geometry (rock radius > 0 = the geometry was read)
        const bool have_geom = s0.blk != 0 && s0.blk_rr > 0.0;
        const BubblePlan plan = bubble_plan(s0.phase, act, s0.wp, nrock, pr.dst_ok != 0, have_geom);
        if (plan.ev == BEV_REPLAN) { bubble_event(h, sid, f, 1, s0, act); return; }   // resume untouched (legacy)
        bool skip = plan.ev == BEV_SKIP;
        if (plan.ev == BEV_FLY && plan.reason == BFR_WAYPOINT)
        {
            double wp[3] = {0, 0, 0};
            path_waypoint(p, pr.dst_m, s0.blk_c, block_r_m(s0.blk_rr, bubble_clear_m(cr, pr.clearance_m)), wp);
            const bool ok = bubble_fly(h, sid, f, p, wp, BFR_WAYPOINT);
            it = g_bub.find(sid);
            if (it == g_bub.end()) return;
            if (ok)
            {
                ++it->second.wp;
                it->second.resume = true;
                std::copy(pr.dst_m, pr.dst_m + 3, it->second.dst);
                return;
            }
            skip = true;   // no waypoint possible: skip below
        }
        else if (plan.ev == BEV_FLY && plan.reason == BFR_NUDGE)
        {
            double wp[3] = {0, 0, 0};
            bubble_nudge_point(p, nc, wp);
            bubble_fly(h, sid, f, p, wp, BFR_NUDGE);
            it = g_bub.find(sid);
            if (it != g_bub.end()) it->second.resume = false;
            return;
        }
        it = g_bub.find(sid);
        if (it == g_bub.end()) return;
        it->second.resume = false;
        if (skip) bubble_event(h, sid, f, 2, s0, act);
    }

    // WHAT: one ladder pass of ship sid in phase ph (legacy bubble_one): first blocker query, hit decision, ladder step, brake (throttle 0)
    //       and the fix. p = ship (m), fr = frame (uu), v = speed (m/s).
    void bubble_one(const ScHost& h, int sid, const ScBubbleProfile& pr, const MsFlight& f, const ScFrame& fr, const double p[3], double v, int ph)
    {
        const double cr = bub_ship_r(sid, f);
        const double L = bubble_look_ahead(v);
        const double b[3] = {p[0] + fr.fw[0] * L, p[1] + fr.fw[1] * L, p[2] + fr.fw[2] * L};
        ScBlockHit hit{};
        hit.size = static_cast<int32_t>(sizeof(ScBlockHit));
        const int raw = host_first_blocker(h, sid, f.sys, ph, pr.rock, p, b, bubble_clear_m(cr, pr.clearance_m), hit);
        BubbleState& bs = g_bub[sid];
        int bid = raw;
        double dnow = 0.0;
        if (raw != 0 && !hit.geom_ok)
        {   // no blocker geometry: brake without the closing check (legacy: reg_geom failed)
            bs.prev_id = raw;
            bs.prev_d = 0.0;
        }
        else bid = bubble_hit_decide(bs, raw, p, fr.fw, hit.c, v, dnow);
        const BubbleAct act = bubble_pass(bs, bid, ph, hit.d, hit.c, hit.rock_r, pr.rock, v);
        if (act == BBA_BRAKE)
        {
            std::wstring e;
            ms_set_throttle(sid, 0, e);
            const int bph = bs.phase, bpend = bs.pending, bwp = bs.wp;   // copied: the host call below may erase the state
            double nc[3] = {0, 0, 0}, nr = 0.0;
            const bool nrock = host_near_rock(h, sid, p, cr, nc, nr);
            const BubbleFix fix = bubble_fix(static_cast<BubblePhase>(bph), bpend, bwp, nrock);
            host_logw(h, L"BUBBLE brake ship=" + std::to_wstring(sid) + L" phase=" + bub_text(bubble_phase_name(static_cast<BubblePhase>(bph))) +
                         L" rock=" + std::to_wstring(bid) + L" dist=" + std::to_wstring(static_cast<long long>(std::llround(hit.d))) +
                         L" rad=" + std::to_wstring(static_cast<long long>(std::llround(hit.block_r))) + L" speed=" + std::to_wstring(v) +
                         L" -> " + bub_text(bubble_fix_name(fix)));
        }
        else if (act == BBA_REPLAN || act == BBA_RETARGET) bubble_exec(h, sid, pr, f, p, cr, act);
    }

    // WHAT: SEH context of one ship pass: ship id, host, GetTickCount64 of this tick, passes run (bubble_one calls).
    struct BubCtx { int sid; const ScHost* h; ULONGLONG now; int ran; };

    // WHAT: one ship pass (legacy bubble_tick body, same order). Plain function for sc_seh_run.
    //       Erases the ship's state when it is not active, dead or refused. Profile and flight snapshot are read every pass.
    void bub_pass_fn(void* arg)
    {
        BubCtx* c = static_cast<BubCtx*>(arg);
        const int sid = c->sid;
        const ScHost& h = *c->h;
        ScBubbleProfile pr{};
        pr.size = static_cast<int32_t>(sizeof(ScBubbleProfile));
        if (!host_profile(h, sid, pr) || !pr.active) { g_bub.erase(sid); return; }
        MsFlight f;
        std::memset(&f, 0, sizeof(f));
        f.size = static_cast<int32_t>(sizeof(MsFlight));
        if (ms_flight(sid, FL_POSE | FL_STATE, &f) != 1 || !(f.flags & FF_ALIVE)) { g_bub.erase(sid); return; }
        ScFrame fr;
        std::wstring why;
        const bool frame_ok = frame_from_flight(f, fr, why);
        double p[3] = {0, 0, 0};
        if (frame_ok) for (int i = 0; i < 3; ++i) p[i] = fr.loc[i] / SM_UU_PER_M;
        // a flown waypoint ends: fly again to the flight destination, unless the ship is there (no state reference is held across the host call)
        auto st = g_bub.find(sid);
        if (st != g_bub.end() && st->second.resume && !pr.job_active)
        {
            if (!frame_ok) st->second.resume = false;
            else
            {
                const ResumeStep step = bubble_resume_step(true, false, dist3(p, st->second.dst));
                if (step == BRS_ARRIVED) st->second.resume = false;
                else if (step == BRS_FLY)
                {
                    double dst[3] = {0, 0, 0};
                    std::copy(st->second.dst, st->second.dst + 3, dst);
                    const bool ok = bubble_fly(h, sid, f, p, dst, BFR_RESUME);
                    st = g_bub.find(sid);
                    if (st == g_bub.end()) return;
                    if (!ok) st->second.resume = false;
                }
            }
        }
        BubbleState& bs = g_bub[sid];
        double v = 0.0;
        auto sm = g_spd.mon.find(sid);
        if (g_spd.watch.count(sid) && sm != g_spd.mon.end() && sm->second.have) v = sm->second.v / SC_RAW_PER_M;
        const BubbleWhy wy = bubble_why((f.flags & FF_AUTOPILOT) != 0, (f.flags & FF_DOCK_AP) != 0, (f.flags & FF_TRAVELLING) != 0, pr.thrust != 0, v);
        const ULONGLONG now = c->now;
        auto off = g_bub_off.find(sid);
        const bool paused = off != g_bub_off.end() && now < off->second;
        const BubbleGate gate = bubble_gate(bs, pr.warp_wait != 0, pr.job_active != 0, wy, paused);
        if (gate == BGT_SKIP) return;
        if (gate == BGT_ERASE) { g_bub.erase(sid); return; }
        const BubblePhase ph = bs.braking ? static_cast<BubblePhase>(bs.phase)
                                          : bubble_phase(pr.thrust != 0, pr.pt_on != 0, pr.ore_on != 0, pr.thrust == 0);
        if (!frame_ok) return;
        ++c->ran;
        LARGE_INTEGER fq, t0, t1;
        QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&t0);
        bubble_one(h, sid, pr, f, fr, p, v, ph);
        QueryPerformanceCounter(&t1);
        const double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(fq.QuadPart);
        if (ms > BUBBLE_SLOW_MS)
        {
            g_bub_off[sid] = now + BUBBLE_OFF_MS;
            if (g_bub_slow_ms == 0 || now - g_bub_slow_ms >= BUBBLE_SLOW_LOG_MS)
            {
                g_bub_slow_ms = now;
                host_logw(h, L"BUBBLE slow ship=" + std::to_wstring(sid) + L" ms=" + std::to_wstring(ms));
            }
        }
    }

    // WHAT: one bubble tick over every watched ship and every ship with a ladder state. One SEH guard per ship: a fault logs
    //       'BUBBLE fault' and erases that ship's state. Returns the passes run (dt unused: the ladder is per pass, not per second).
    int sc_bubble_tick(double dt, const ScHost& h)
    {
        (void)dt;
        std::vector<int> ids;
        for (const auto& w : g_spd.watch) ids.push_back(w.first);
        for (const auto& b : g_bub) if (!g_spd.watch.count(b.first)) ids.push_back(b.first);
        int ran = 0;
        for (int sid : ids)
        {
            BubCtx c{sid, &h, GetTickCount64(), 0};
            const DWORD code = sc_seh_run(bub_pass_fn, &c);
            if (code != 0)
            {
                host_logw(h, L"BUBBLE fault ship=" + std::to_wstring(sid) + L" code=0x" + hex32(code));
                g_bub.erase(sid);
            }
            ran += c.ran;
        }
        return ran;
    }
}
