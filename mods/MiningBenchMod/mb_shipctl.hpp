// FILE: mods/MiningBenchMod/mb_shipctl.hpp
// WHAT: loads ShipControl (ue4ss/Mods/ShipControl/dlls/main.dll, sc_api.h API 2) and claims / releases the bench ships for owner "mining".
//       Also binds the level 2 speed monitor (SC_watch, SC_tick_speed, SC_speed, SC_zero) and the host callbacks.
// WHY: one owner per ship. While the mining loop drives a ship, ShipControl records that mining owns it. SC1 only records the claim: a refused claim is logged and mining goes on (the policy flip is SC6).
//      SC2a: the speed monitor and zero run in ShipControl when it is usable; the legacy mb_steer.hpp monitor is the fallback (dual path).
// DEPENDS ON: mb_common.hpp (get_dll_dir, log_line, STR), mb_config.hpp (g_cfg), sc_api.h (copied next to this file by build.ps1).
// USED BY: mb_steer.hpp (sc_speed_block, sc_watch_ship, sc_tick_speed, sc_speed_read, sc_speed_zero, sc_speed_tune, g_sc_* state),
//          mb_mining.hpp (mine_cmd, sweep_cmd, mine_forget_ship, mining_tick), dllmain.cpp (sc_load in the ctor, sc_release_all in the dtor).
// GAME FACTS: none. Ship ids are the bench ship ids (plain ints for ShipControl).
#pragma once

#include <map>
#include <set>
#include <string>
#include "sc_api.h"

namespace
{
    // WHAT: resolved ShipControl entry points. h is the loaded module (nullptr = not loaded). api_level = SC_api() result.
    //       speed_bound = all level 2 exports resolved and api_level >= 2.
    struct ScApi
    {
        HMODULE h = nullptr;
        int api_level = 0;
        bool speed_bound = false;
        int (*api)() = nullptr;
        int (*claim)(const wchar_t*, int, wchar_t*, int) = nullptr;
        int (*release)(const wchar_t*, int, wchar_t*, int) = nullptr;
        int (*release_all)(const wchar_t*, wchar_t*, int) = nullptr;
        int (*caps)() = nullptr;
        int (*set_host)(const ScHost*) = nullptr;
        int (*set_tune)(const ScTune*) = nullptr;
        int (*watch)(const char*, int, int) = nullptr;
        int (*tick_speed)(double) = nullptr;
        int (*speed)(int, ScSpeed*) = nullptr;
        int (*zero)(const char*, int) = nullptr;
        int (*move_start)(const char*, int, ScMoveReq*, wchar_t*, int) = nullptr;   // level 3
        int (*move_cancel)(const char*, int) = nullptr;
        int (*move_active)(int) = nullptr;
        int (*move_status)(int, ScMoveStatus*) = nullptr;
        int (*tick_frame)(double) = nullptr;
        bool move_bound = false;            // api_level >= 3 and every move export resolved (the caps bit is checked by sc_move_block)
        int (*tick_bubble)(double) = nullptr;                       // level 4
        int (*bubble_status)(int, ScBubbleStatus*) = nullptr;
        bool bubble_bound = false;          // api_level >= 4, move_bound and both bubble exports resolved (the caps bit is checked by sc_bubble_block)
    };
    ScApi g_sc;
    ScHost g_sc_host;                   // host callbacks (zero-initialised; filled by sc_load)
    std::set<int> g_sc_held;            // ships this mod has claimed and not released yet
    const wchar_t* const kScOwner = L"mining";
    const char* const kScOwnerA = "mining";   // speed monitor / zero owner (narrow, SC_watch / SC_zero take char*)
    // SC6 policy: Start, sweep and move jobs refuse without ShipControl API 4 (MSCore caps bits 1/2/4/8 checked by the sc_*_block gates)
    const wchar_t* const SC_NEED_TEXT = L"ShipControl not installed or too old (needs API 4); update MSCore Manager";

    // WHAT: ScHost size sent to ShipControl by API level: 112 (API 4), 80 (API 3), 24 (API 2). SC_set_host refuses any other size.
    // WHY: an API 3 ShipControl rejects the 112-byte host, so the size follows the level it reports.
    int sc_host_size() { return g_sc.api_level >= 4 ? static_cast<int>(sizeof(ScHost)) : g_sc.api_level >= 3 ? SC_HOST_SIZE_L3 : SC_HOST_SIZE_L2; }

    // host callbacks of the move jobs: defined in mb_steer.hpp (they need the move jobs). Declared here for sc_load.
    void sc_host_tlog(const wchar_t* text);
    void sc_host_move_done(int sid, int seq, const wchar_t* result);
    int sc_host_warp_begin(int sid, const ScMoveReq* req);
    int sc_host_remain(int sid, const double ship_m[3], double* out_m);
    int sc_host_target_ok(int sid, void* handle);
    int sc_host_ore_loc(int sid, void* handle, double* out3_uu);
    // bubble ladder callbacks (SC3-C, ScHost level 4): defined in mb_bubble.hpp
    int sc_host_first_blocker(int sid, int sys, int phase, int rock, const double p[3], const double b[3], double clear_m, ScBlockHit* out);
    int sc_host_bubble_profile(int sid, ScBubbleProfile* out);
    int sc_host_on_bubble(int sid, const ScBubbleEvent* ev);
    int sc_host_near_rock(int sid, const double p[3], double ship_r_m, double* out_c3, double* out_r);
    void mv_cancel(int sid);            // mb_steer.hpp
    bool mv_active_sc(int sid);         // mb_steer.hpp

    // speed monitor state (level 2)
    std::set<int> g_sc_watch;           // ships ShipControl samples for "mining" (SC_watch on succeeded)
    std::map<int, double> g_sc_abort;   // fresh speed aborts reported by SC_tick_speed (host callback), consumed by spd_update_sc
    double g_sc_speed_k = -1.0;         // speed_k last sent with SC_set_tune (-1 = never)
    int g_sc_last_tick = 0;             // result of the last SC_tick_speed (-1 = MSCore not usable)
    bool g_sc_speed_on = false;         // ShipControl does the speed monitor this poll (set by spd_update_sc)
    std::wstring g_sc_mode_note;        // last logged speed mode (log once per change)

    // WHAT: host callback: ShipControl log lines go to the UE4SS log.
    void sc_host_log(const wchar_t* text) { try { log_line(text); } catch (...) {} }

    // WHAT: host callback: a fresh speed abort (ShipControl already zeroed the ship). Queued for the mining thread (spd_update_sc).
    void sc_host_abort(int sid, double ratio) { g_sc_abort[sid] = ratio; }

    // WHAT: loads ShipControl once and resolves the exports (API 1, level 2 speed exports optional). true when ready. A failed try is retried at most every 10 s.
    // WHY: ShipControl may be missing or older; mining must run without it. The retry window keeps a missing DLL from being probed on every call.
    //      The host callbacks are registered here, once, when the speed exports are there.
    bool sc_load()
    {
        if (g_sc.h) return true;
        static ULONGLONG next_try = 0;
        static bool fail_logged = false;
        static bool ok_logged = false;
        if (GetTickCount64() < next_try) return false;
        next_try = GetTickCount64() + 10000;
        auto path = (get_dll_dir() / L"../../ShipControl/dlls/main.dll").lexically_normal();
        HMODULE h = LoadLibraryW(path.c_str());
        ScApi s;
        s.h = h;
        if (h)
        {
            s.api = reinterpret_cast<int (*)()>(GetProcAddress(h, "SC_api"));
            s.claim = reinterpret_cast<int (*)(const wchar_t*, int, wchar_t*, int)>(GetProcAddress(h, "SC_claim"));
            s.release = reinterpret_cast<int (*)(const wchar_t*, int, wchar_t*, int)>(GetProcAddress(h, "SC_release"));
            s.release_all = reinterpret_cast<int (*)(const wchar_t*, wchar_t*, int)>(GetProcAddress(h, "SC_release_all"));
        }
        if (!h || !s.api || !s.claim || !s.release || !s.release_all || s.api() < 1)
        {
            if (h) FreeLibrary(h);
            if (!fail_logged) { fail_logged = true; log_line(std::wstring(STR("SHIPCTL load failed: ")) + path.wstring()); }
            return false;
        }
        s.api_level = s.api();
        s.caps = reinterpret_cast<int (*)()>(GetProcAddress(h, "SC_caps"));
        s.set_host = reinterpret_cast<int (*)(const ScHost*)>(GetProcAddress(h, "SC_set_host"));
        s.set_tune = reinterpret_cast<int (*)(const ScTune*)>(GetProcAddress(h, "SC_set_tune"));
        s.watch = reinterpret_cast<int (*)(const char*, int, int)>(GetProcAddress(h, "SC_watch"));
        s.tick_speed = reinterpret_cast<int (*)(double)>(GetProcAddress(h, "SC_tick_speed"));
        s.speed = reinterpret_cast<int (*)(int, ScSpeed*)>(GetProcAddress(h, "SC_speed"));
        s.zero = reinterpret_cast<int (*)(const char*, int)>(GetProcAddress(h, "SC_zero"));
        s.speed_bound = s.api_level >= 2 && s.caps && s.set_host && s.set_tune && s.watch && s.tick_speed && s.speed && s.zero;
        s.move_start = reinterpret_cast<int (*)(const char*, int, ScMoveReq*, wchar_t*, int)>(GetProcAddress(h, "SC_move_start"));
        s.move_cancel = reinterpret_cast<int (*)(const char*, int)>(GetProcAddress(h, "SC_move_cancel"));
        s.move_active = reinterpret_cast<int (*)(int)>(GetProcAddress(h, "SC_move_active"));
        s.move_status = reinterpret_cast<int (*)(int, ScMoveStatus*)>(GetProcAddress(h, "SC_move_status"));
        s.tick_frame = reinterpret_cast<int (*)(double)>(GetProcAddress(h, "SC_tick_frame"));
        s.move_bound = s.api_level >= 3 && s.speed_bound && s.move_start && s.move_cancel && s.move_active && s.move_status && s.tick_frame;
        s.tick_bubble = reinterpret_cast<int (*)(double)>(GetProcAddress(h, "SC_tick_bubble"));
        s.bubble_status = reinterpret_cast<int (*)(int, ScBubbleStatus*)>(GetProcAddress(h, "SC_bubble_status"));
        s.bubble_bound = s.api_level >= 4 && s.move_bound && s.tick_bubble && s.bubble_status;
        g_sc = s;
        if (g_sc.speed_bound)
        {
            // API 4 takes the 112-byte host, API 3 the 80-byte prefix, API 2 the 24-byte one (sc_host_size). Fields past the size are ignored there.
            g_sc_host.size = static_cast<int32_t>(sc_host_size());
            g_sc_host.log = sc_host_log;
            g_sc_host.on_speed_abort = sc_host_abort;
            g_sc_host.tlog = sc_host_tlog;
            g_sc_host.on_move_done = sc_host_move_done;
            g_sc_host.warp_begin = sc_host_warp_begin;
            g_sc_host.remain_m = sc_host_remain;
            g_sc_host.target_ok = sc_host_target_ok;
            g_sc_host.ore_loc = sc_host_ore_loc;
            g_sc_host.braking = nullptr;   // legacy bubble brake removed (SC6-D2); ShipControl tolerates a null callback
            g_sc_host.first_blocker = sc_host_first_blocker;
            g_sc_host.bubble_profile = sc_host_bubble_profile;
            g_sc_host.on_bubble = sc_host_on_bubble;
            g_sc_host.near_rock = sc_host_near_rock;
            g_sc.set_host(&g_sc_host);
        }
        if (!ok_logged) { ok_logged = true; log_line(std::wstring(STR("SHIPCTL loaded: ")) + path.wstring()); }
        return true;
    }

    // WHAT: claims a ship for "mining". true = held (now or before). false = refused, failed or ShipControl missing: *why = "ship <err>" (SC_NEED_TEXT when missing).
    // WHY: SC6 policy: the claim gates Start, sweep and move jobs; the caller stops and shows *why.
    bool sc_claim_ship(int sid, std::wstring* why = nullptr)
    {
        if (g_sc_held.count(sid)) return true;
        std::wstring w;
        if (!sc_load()) w = SC_NEED_TEXT;
        else
        {
            wchar_t e[256] = {};
            try
            {
                if (g_sc.claim(kScOwner, sid, e, 256))
                {
                    g_sc_held.insert(sid);
                    log_line(STR("SHIPCTL claim ship=") + std::to_wstring(sid) + STR(" owner=mining"));
                    return true;
                }
                log_line(STR("SHIPCTL claim refused ship=") + std::to_wstring(sid) + STR(": ") + std::wstring(e));
                w = std::wstring(STR("ship ")) + e;
            }
            catch (...)
            {
                log_line(STR("SHIPCTL claim exception ship=") + std::to_wstring(sid));
                w = STR("ship claim exception");
            }
        }
        if (why) *why = w;
        return false;
    }

    // WHAT: releases a ship this mod holds. Not held = no-op.
    void sc_release_ship(int sid)
    {
        if (!g_sc_held.count(sid)) return;
        g_sc_held.erase(sid);
        wchar_t e[256] = {};
        try
        {
            if (g_sc.release(kScOwner, sid, e, 256)) log_line(STR("SHIPCTL release ship=") + std::to_wstring(sid) + STR(" owner=mining"));
            else log_line(STR("SHIPCTL release refused ship=") + std::to_wstring(sid) + STR(": ") + std::wstring(e));
        }
        catch (...) { log_line(STR("SHIPCTL release exception ship=") + std::to_wstring(sid)); }
    }

    // WHAT: per tick per bench ship: the claim stays while the loop is active (run, sweep, or any state but HALT / OFF), else it is released.
    void sc_sync(int sid, bool active)
    {
        if (active) return;
        if (mv_active_sc(sid)) mv_cancel(sid);   // a ShipControl move job ends before its ship is released
        sc_release_ship(sid);
    }

    // WHAT: releases every ship this mod holds (mod unload). Logs the count. No-op when ShipControl was never loaded.
    void sc_release_all()
    {
        if (!g_sc.release_all) return;
        wchar_t e[256] = {};
        try
        {
            const int n = g_sc.release_all(kScOwner, e, 256);
            if (n < 0) log_line(STR("SHIPCTL release_all failed: ") + std::wstring(e));
            else log_line(STR("SHIPCTL release_all owner=mining count=") + std::to_wstring(n));
        }
        catch (...) { log_line(STR("SHIPCTL release_all exception")); }
        g_sc_held.clear();
    }

    // ---------------- speed monitor (level 2) ----------------

    // WHAT: why ShipControl cannot do the speed monitor now. nullptr = usable (loaded, API >= 2, MSCore usable by caps bit 1).
    const wchar_t* sc_speed_block()
    {
        if (!g_sc.h) return STR("ShipControl not loaded");
        if (!g_sc.speed_bound) return STR("ShipControl API < 2");
        try { if (!(g_sc.caps() & 2)) return STR("MSCore not usable"); }
        catch (...) { return STR("MSCore not usable"); }
        return nullptr;
    }

    // WHAT: sends speed_k (config steer_probe_speed_k) to ShipControl when it changed (first use and every config reload).
    void sc_speed_tune()
    {
        const double k = static_cast<double>(g_cfg.steer_probe_speed_k);
        if (!g_sc.speed_bound || g_sc_speed_k == k) return;
        ScTune t = {};
        t.size = static_cast<int32_t>(sizeof(ScTune));
        t.speed_k = k;
        try { if (g_sc.set_tune(&t)) g_sc_speed_k = k; } catch (...) {}
    }

    // WHAT: SC_watch for ship sid (on = 1 watch, 0 stop). true = accepted (on=1: the ship is watched; refused = another owner holds it).
    bool sc_watch_ship(int sid, bool on)
    {
        if (!g_sc.speed_bound) return false;
        try { return g_sc.watch(kScOwnerA, sid, on ? 1 : 0) != 0; }
        catch (...) { return false; }
    }

    // WHAT: SC_tick_speed. >= 0 = fresh aborts this step, -1 = not usable (also on an exception).
    int sc_tick_speed(double dt)
    {
        if (!g_sc.speed_bound) return -1;
        try { return g_sc.tick_speed(dt); }
        catch (...) { return -1; }
    }

    // WHAT: SC_speed reading of ship sid. true = the call worked (check out.have).
    bool sc_speed_read(int sid, ScSpeed& out)
    {
        if (!g_sc.speed_bound) return false;
        try { return g_sc.speed(sid, &out) != 0; }
        catch (...) { return false; }
    }

    // WHAT: SC_zero for ship sid as owner "mining". true = zeroed by ShipControl (false = refused or failed: caller zeroes itself).
    bool sc_speed_zero(int sid)
    {
        if (!g_sc.speed_bound) return false;
        try { return g_sc.zero(kScOwnerA, sid) != 0; }
        catch (...) { return false; }
    }

    // ---------------- move jobs (level 3, SC2b) ----------------

    // WHAT: why the move jobs cannot run in ShipControl now. nullptr = usable (API >= 3 and move-job caps bit 4, value 4).
    const wchar_t* sc_move_block()
    {
        if (!g_sc.h) return STR("ShipControl not loaded");
        if (!g_sc.move_bound) return STR("ShipControl API < 3");
        try { if (!(g_sc.caps() & 4)) return STR("MSCore flight not usable"); }
        catch (...) { return STR("MSCore flight not usable"); }
        return nullptr;
    }

    // WHAT: true when the move job of ship sid runs in ShipControl: move jobs usable, ship claimed by mining.
    //       The ship is watched first (SC_watch, owner mining) when not yet watched. false = refused (no legacy path in SC6).
    bool sc_move_ready(int sid)
    {
        if (sc_move_block() || !g_sc_held.count(sid)) return false;
        if (g_sc_watch.count(sid)) return true;
        if (!sc_watch_ship(sid, true)) return false;
        g_sc_watch.insert(sid);
        return true;
    }

    // WHAT: SC_move_start for owner mining. true = started (req.out_cid / out_steps set). false = refused: err holds the 'error: ...' text. Never throws.
    bool sc_move_start(ScMoveReq& req, int sid, std::wstring& err)
    {
        wchar_t e[256] = {};
        try
        {
            const int ok = g_sc.move_start(kScOwnerA, sid, &req, e, 256);
            err = e;
            return ok != 0;
        }
        catch (...) { err = STR("error: ShipControl exception"); return false; }
    }

    // WHAT: SC_move_cancel for owner mining (no zero, no callback). true = cancelled or none. Never throws.
    bool sc_move_cancel(int sid)
    {
        try { return g_sc.move_cancel(kScOwnerA, sid) != 0; }
        catch (...) { return false; }
    }

    // WHAT: SC_move_status of ship sid. true = read (out.active 0 when no job). Never throws.
    bool sc_move_status(int sid, ScMoveStatus& out)
    {
        out = ScMoveStatus{};
        out.size = static_cast<int32_t>(sizeof(ScMoveStatus));
        try { return g_sc.move_status(sid, &out) != 0; }
        catch (...) { return false; }
    }

    // WHAT: SC_tick_frame: one tick of every move job. >= 0 = jobs stepped, -1 = not usable (also on an exception).
    int sc_tick_frame(double dt)
    {
        try { return g_sc.tick_frame(dt); }
        catch (...) { return -1; }
    }

    // ---------------- bubble ladder (level 4, SC3-C) ----------------

    // WHAT: why the bubble ladder cannot run in ShipControl now. nullptr = usable (API >= 4 and bubble caps bit 8, value 8).
    const wchar_t* sc_bubble_block()
    {
        if (!g_sc.h) return STR("ShipControl not loaded");
        if (!g_sc.bubble_bound) return STR("ShipControl API < 4");
        try { if (!(g_sc.caps() & 8)) return STR("MSCore flight not usable"); }
        catch (...) { return STR("MSCore flight not usable"); }
        return nullptr;
    }

    // WHAT: the SC6 gate of Start, sweep and move jobs: ShipControl loaded and move, bubble and speed usable (caps). false = refuse with SC_NEED_TEXT.
    bool sc_policy_ok() { return sc_load() && !sc_move_block() && !sc_bubble_block() && !sc_speed_block(); }

    // WHAT: true when the bubble ladder of ship sid runs in ShipControl: ladder usable, speed monitor on (g_sc_speed_on), ship claimed by mining.
    bool sc_bub_on_ship(int sid) { return !sc_bubble_block() && g_sc_speed_on && g_sc_held.count(sid) != 0; }
    // WHAT: true when the legacy bubble pass skips ship sid: ShipControl runs its ladder and the ship is watched (move-job ships).
    bool sc_bub_covers(int sid) { return sc_bub_on_ship(sid) && g_sc_watch.count(sid) != 0; }

    // WHAT: SC_tick_bubble: one bubble pass of ShipControl (dt = s). >= 0 = ships passed, -1 = not usable (also on an exception).
    int sc_tick_bubble(double dt)
    {
        if (!g_sc.bubble_bound) return -1;
        try { return g_sc.tick_bubble(dt); }
        catch (...) { return -1; }
    }

    // WHAT: SC_bubble_status of ship sid into o (size set here). true = call worked (o.have 0 when no state). Never throws.
    bool sc_bubble_status(int sid, ScBubbleStatus& o)
    {
        o = ScBubbleStatus{};
        o.size = static_cast<int32_t>(sizeof(ScBubbleStatus));
        if (!g_sc.bubble_bound) return false;
        try { return g_sc.bubble_status(sid, &o) != 0; }
        catch (...) { return false; }
    }

    // WHAT: host callbacks cleared (zeroed struct, size as installed). Called at mod unload before sc_release_all. No-op when never installed.
    void sc_host_clear()
    {
        if (!g_sc.speed_bound || !g_sc.set_host) return;
        ScHost z = {};
        z.size = static_cast<int32_t>(sc_host_size());
        try { g_sc.set_host(&z); }
        catch (...) {}
    }
}
