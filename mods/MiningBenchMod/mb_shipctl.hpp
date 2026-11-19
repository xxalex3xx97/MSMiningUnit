// FILE: mods/MiningBenchMod/mb_shipctl.hpp
// WHAT: loads ShipControl (ue4ss/Mods/ShipControl/dlls/main.dll, sc_api.h API 1) and claims / releases the bench ships for owner "mining".
// WHY: one owner per ship. While the mining loop drives a ship, ShipControl records that mining owns it. SC1 only records the claim: a refused claim is logged and mining goes on (the policy flip is SC6).
// DEPENDS ON: mb_common.hpp (get_dll_dir, log_line, STR), sc_api.h (copied next to this file by build.ps1). mb_mining.hpp forward-declares the three sc_ functions below.
// USED BY: mb_mining.hpp (mine_cmd, sweep_cmd, mine_forget_ship, mining_tick), dllmain.cpp (sc_load in the ctor, sc_release_all in the dtor).
// GAME FACTS: none. Ship ids are the bench ship ids (plain ints for ShipControl).
#pragma once

#include <set>
#include <string>
#include "sc_api.h"

namespace
{
    // WHAT: resolved ShipControl entry points. h is the loaded module (nullptr = not loaded).
    struct ScApi
    {
        HMODULE h = nullptr;
        int (*api)() = nullptr;
        int (*claim)(const wchar_t*, int, wchar_t*, int) = nullptr;
        int (*release)(const wchar_t*, int, wchar_t*, int) = nullptr;
        int (*release_all)(const wchar_t*, wchar_t*, int) = nullptr;
    };
    ScApi g_sc;
    std::set<int> g_sc_held;            // ships this mod has claimed and not released yet
    const wchar_t* const kScOwner = L"mining";

    // WHAT: loads ShipControl once and resolves the exports (API 1). true when ready. A failed try is retried at most every 10 s.
    // WHY: ShipControl may be missing or older; mining must run without it. The retry window keeps a missing DLL from being probed on every call.
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
        g_sc = s;
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
}
