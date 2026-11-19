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
    };
    ScApi g_sc;
    std::set<int> g_sc_held;            // ships this mod has claimed and not released yet
    const wchar_t* const kScOwner = L"mining";
    const char* const kScOwnerA = "mining";   // speed monitor / zero owner (narrow, SC_watch / SC_zero take char*)

    // speed monitor state (level 2)
    std::set<int> g_sc_watch;           // ships ShipControl samples for "mining" (SC_watch on succeeded)
    std::map<int, double> g_sc_abort;   // fresh speed aborts reported by SC_tick_speed (host callback), consumed by spd_update_dual
    double g_sc_speed_k = -1.0;         // speed_k last sent with SC_set_tune (-1 = never)
    int g_sc_last_tick = 0;             // result of the last SC_tick_speed (-1 = MSCore not usable)
    bool g_sc_speed_on = false;         // ShipControl does the speed monitor this poll (set by spd_update_dual)
    std::wstring g_sc_mode_note;        // last logged speed mode (log once per change)

    // WHAT: host callback: ShipControl log lines go to the UE4SS log.
    void sc_host_log(const wchar_t* text) { try { log_line(text); } catch (...) {} }

    // WHAT: host callback: a fresh speed abort (ShipControl already zeroed the ship). Queued for the mining thread (spd_update_dual).
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
        g_sc = s;
        if (g_sc.speed_bound)
        {
            static ScHost host;   // static: the callbacks stay valid while the DLL is loaded
            host.size = static_cast<int32_t>(sizeof(ScHost));
            host.log = sc_host_log;
            host.on_speed_abort = sc_host_abort;
            g_sc.set_host(&host);
        }
        if (!ok_logged) { ok_logged = true; log_line(std::wstring(STR("SHIPCTL loaded: ")) + path.wstring()); }
        return true;
    }

    // WHAT: claims a ship for "mining". ShipControl missing = nothing happens. Refused = one log line, mining goes on.
    void sc_claim_ship(int sid)
    {
        if (g_sc_held.count(sid) || !sc_load()) return;
        wchar_t e[256] = {};
        try
        {
            if (g_sc.claim(kScOwner, sid, e, 256))
            {
                g_sc_held.insert(sid);
                log_line(STR("SHIPCTL claim ship=") + std::to_wstring(sid) + STR(" owner=mining"));
            }
            else log_line(STR("SHIPCTL claim refused ship=") + std::to_wstring(sid) + STR(": ") + std::wstring(e));
        }
        catch (...) { log_line(STR("SHIPCTL claim exception ship=") + std::to_wstring(sid)); }
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
        if (!active) sc_release_ship(sid);
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
}
