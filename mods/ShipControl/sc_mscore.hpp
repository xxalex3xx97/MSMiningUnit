// FILE: mods/ShipControl/sc_mscore.hpp
// WHAT: loads MSCore (sibling ..\..\MSCore\dlls\main.dll) and binds the flight exports (MSCore API 10, MSCore 0.8.13).
//       The MsFlight layout the speed monitor reads lives in sc_flight_abi.h (included here).
// WHY: ShipControl reads ship state and writes Throttle / rotation only through MSCore, so the game names stay in MSCore.
//      Fail closed: when MSCore is missing or older than API 10, every speed feature refuses with sc_mscore_too_old().
// DEPENDS ON (exact): windows.h, <cstddef>, <cstdint>, <filesystem>, <string>, sc_flight_abi.h. No UE headers (tests include this file).
// USED BY: sc_speed.hpp (ms_load, ms_flight, ms_zero), dllmain.cpp (SC_caps bit 1).
// GAME FACTS: none here. The MsFlight block (sc_flight_abi.h) is a byte-for-byte copy of cpp/MSCore/msc_api.hpp (origin/main, lines 63-120).
// CAPS: MSCore_api() >= 10 is required; the flight exports are bound by name with GetProcAddress.
#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "sc_flight_abi.h"

namespace
{
    // WHAT: the MSCore API level and text ShipControl needs for the speed features.
    // WHY: the text is what the user sees in the refusal, so it names the version to install.
    constexpr int MS_NEED_API = 10;
    constexpr const wchar_t* MS_TOO_OLD = L"MSCore too old (needs 0.8.13)";

    // WHAT: MSCore exports bound by name. Null until ms_load succeeds.
    // WHY: one struct, so a failed bind can be reset as a whole.
    struct MsApi
    {
        HMODULE h = nullptr;
        int (*api)() = nullptr;                                                  // MSCore_api
        int (*ship_flight)(int sid, int mask, MsFlight* out) = nullptr;          // MSCore_ship_flight
        int (*ship_set_throttle)(int sid, int step, wchar_t* err, int errlen) = nullptr;  // MSCore_ship_set_throttle
        int (*ship_zero)(int sid, wchar_t* err, int errlen) = nullptr;           // MSCore_ship_zero
        int (*ship_console)(int sid) = nullptr;                                  // MSCore_ship_console
        int (*actor_loc)(void* actor, double* out3) = nullptr;                   // MSCore_actor_loc
        int (*view_send)(int cid, long long yaw_raw, long long pitch_raw, wchar_t* err, int errlen) = nullptr;  // MSCore_view_send (SC2b)
    };

    MsApi g_ms;                  // bound MSCore exports (null = not loaded)
    ULONGLONG g_ms_next = 0;     // GetTickCount64 before which no new LoadLibrary is tried

    // WHAT: anchor inside this module, for GetModuleHandleEx (finds the folder of this DLL).
    void ms_anchor() {}

    // WHAT: path of MSCore main.dll: the sibling folder ..\..\MSCore\dlls, seen from this DLL's dlls folder.
    // WHY: both mods live under the same ue4ss\Mods folder, so the relative path is the same on every install.
    std::filesystem::path ms_path()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&ms_anchor), &self);
        wchar_t buf[MAX_PATH] = {0};
        GetModuleFileNameW(self, buf, MAX_PATH);
        return (std::filesystem::path(buf).parent_path() / L"..\\..\\MSCore\\dlls\\main.dll").lexically_normal();
    }

    // WHAT: GetProcAddress typed as T (a function pointer type). Null when the export is missing.
    // WHY: one cast point instead of six FARPROC casts.
    template <class T>
    T ms_sym(HMODULE h, const char* name)
    {
        return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(h, name)));
    }

    // WHAT: loads MSCore and binds the exports when API >= 10. true = bound (or already bound). false = MS_TOO_OLD in err.
    // WHY: retry is throttled to one LoadLibrary per 10 s, so a missing MSCore does not cost a load attempt on every poll.
    bool ms_load(std::wstring& err)
    {
        if (g_ms.h && g_ms.api) return true;
        const ULONGLONG now = GetTickCount64();
        if (now < g_ms_next) { err = MS_TOO_OLD; return false; }
        g_ms_next = now + 10000;

        MsApi m;
        m.h = LoadLibraryW(ms_path().c_str());
        if (m.h)
        {
            m.api = ms_sym<decltype(m.api)>(m.h, "MSCore_api");
            m.ship_flight = ms_sym<decltype(m.ship_flight)>(m.h, "MSCore_ship_flight");
            m.ship_set_throttle = ms_sym<decltype(m.ship_set_throttle)>(m.h, "MSCore_ship_set_throttle");
            m.ship_zero = ms_sym<decltype(m.ship_zero)>(m.h, "MSCore_ship_zero");
            m.ship_console = ms_sym<decltype(m.ship_console)>(m.h, "MSCore_ship_console");
            m.actor_loc = ms_sym<decltype(m.actor_loc)>(m.h, "MSCore_actor_loc");
            m.view_send = ms_sym<decltype(m.view_send)>(m.h, "MSCore_view_send");
        }
        const bool ok = m.h && m.api && m.ship_flight && m.ship_set_throttle && m.ship_zero && m.ship_console && m.actor_loc && m.view_send && m.api() >= MS_NEED_API;
        if (!ok)
        {
            if (m.h) FreeLibrary(m.h);
            err = MS_TOO_OLD;
            return false;
        }
        g_ms = m;
        err.clear();
        return true;
    }

    // WHAT: MSCore_ship_flight with exceptions turned into -1. Other codes as MSCore: 1 ok, 0 ship gone, -2 module off, -3 bad size.
    // WHY: the speed monitor must never throw into the mining poll.
    int ms_flight(int sid, int mask, MsFlight* out)
    {
        try { return g_ms.ship_flight(sid, mask, out); }
        catch (...) { return -1; }
    }

    // WHAT: MSCore_ship_zero (Throttle = 0 and rotation = 0). true = zeroed. Exceptions = false.
    // WHY: the one stop the speed abort uses, the same stop mining's zero_all uses.
    bool ms_zero(int sid, std::wstring& err)
    {
        wchar_t buf[256] = {0};
        try
        {
            const bool ok = g_ms.ship_zero(sid, buf, 256) == 1;
            if (!ok) err = buf;
            return ok;
        }
        catch (...) { err = L"exception: MSCore_ship_zero"; return false; }
    }

    // WHAT: MSCore_ship_set_throttle (Throttle step). true = written. Exceptions or unbound export = false.
    // WHY: the move job writes the approach step every frame; a null export is refused, never called (fail closed).
    bool ms_set_throttle(int sid, int step, std::wstring& err)
    {
        wchar_t buf[256] = {0};
        try
        {
            if (!g_ms.ship_set_throttle) { err = L"MSCore flight unavailable"; return false; }
            const bool ok = g_ms.ship_set_throttle(sid, step, buf, 256) == 1;
            if (!ok) err = buf;
            return ok;
        }
        catch (...) { err = L"exception: MSCore_ship_set_throttle"; return false; }
    }

    // WHAT: MSCore_ship_console: PilotConsoleObject id of ship sid. <= 0 = not found (or MSCore unusable / exception, -1).
    int ms_console(int sid)
    {
        try { return g_ms.ship_console ? g_ms.ship_console(sid) : -1; }
        catch (...) { return -1; }
    }

    // WHAT: MSCore_view_send (aim offset to console cid, raw units). true = sent. Exceptions or unbound export = false, err set.
    // WHY: the aim offset is the only rotation actuator of a move job (same call as mining view_send).
    bool ms_view_send(int cid, long long yaw_raw, long long pitch_raw, std::wstring& err)
    {
        wchar_t buf[256] = {0};
        try
        {
            if (!g_ms.view_send) { err = L"MSCore flight unavailable"; return false; }
            const bool ok = g_ms.view_send(cid, yaw_raw, pitch_raw, buf, 256) == 1;
            if (!ok) err = buf;
            return ok;
        }
        catch (...) { err = L"exception: MSCore_view_send"; return false; }
    }
}
