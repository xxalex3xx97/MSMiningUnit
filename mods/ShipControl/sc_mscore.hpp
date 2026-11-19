// FILE: mods/ShipControl/sc_mscore.hpp
// WHAT: loads MSCore (sibling ..\..\MSCore\dlls\main.dll) and binds the flight exports (MSCore API 10, MSCore 0.8.13).
//       Owns the MsFlight layout the speed monitor reads.
// WHY: ShipControl reads ship state and writes Throttle / rotation only through MSCore, so the game names stay in MSCore.
//      Fail closed: when MSCore is missing or older than API 10, every speed feature refuses with sc_mscore_too_old().
// DEPENDS ON (exact): windows.h, <cstddef>, <cstdint>, <filesystem>, <string>. No UE headers (tests include this file).
// USED BY: sc_speed.hpp (ms_load, ms_flight, ms_zero), dllmain.cpp (SC_caps bit 1).
// GAME FACTS: none here. The MsFlight block below is a byte-for-byte copy of cpp/MSCore/msc_api.hpp (origin/main, lines 63-120).
//             Keep the two copies identical. The static_asserts check the layout on both sides.
// CAPS: MSCore_api() >= 10 is required; the flight exports are bound by name with GetProcAddress.
#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// flight state of one ship (API 10, msc_flight.hpp). Caller sets size = sizeof(MsFlight) (mismatch -> -3). Each value is written only when its read worked; flags say which.
// Units: raw = game fixed point (65536 = 1 unit); uu = Unreal units (100 uu = 1 m); chunk = 2048 m cells.
struct MsFlight
{
    int32_t size;               // off 0: set by the caller
    int32_t flags;              // off 4: FF_* bits
    int32_t sid;                // off 8: ship id
    int32_t sys;                // off 12: solar system id (POSE)
    int32_t throttle;           // off 16: Throttle prop (STATE)
    int32_t steps;              // off 20: GetThrottleSteps (STATE)
    int32_t lp_thr;             // off 24: FlightModel LowPowerThrottle (POWER)
    int32_t pad;                // off 28
    int64_t ch[3];              // off 32: chunk location (POSE)
    int64_t max_mod_raw;        // off 56: GetModifiedMaxSpeed (LIMITS), raw
    int64_t max_base_raw;       // off 64: GetMaxSpeed (LIMITS), raw
    int64_t coll_radius_raw;    // off 72: CollisionRadius (STATE), raw
    double act_loc[3];          // off 80: actor location, uu (POSE)
    double fw[3];               // off 104: actor forward axis (POSE)
    double R[3];                // off 128: actor right axis (POSE)
    double U[3];                // off 152: actor up axis (POSE)
    double loc_raw[3];          // off 176: ship object Location, raw, chunk-relative (POSE)
    double quat[4];             // off 200: ship Quat W,X,Y,Z, already / 65536 (POSE)
    double grid_prod;           // off 232: PowerGrid.TotalProduction (POWER)
    double grid_cons;           // off 240: PowerGrid.TotalConsumption (POWER)
    double grid_eff;            // off 248: PowerGrid.ConsumerEfficiency (POWER)
    double lp_frac;             // off 256: FlightModel MaxLowPowerSpeed, fraction (POWER)
};

// MsFlight read groups (mask argument of MSCore_ship_flight)
constexpr int FL_POSE = 1;     // actor frame, ch, loc_raw, quat, sys
constexpr int FL_STATE = 2;    // throttle, steps, the 3 bool flags, collision radius
constexpr int FL_LIMITS = 4;   // both max speeds
constexpr int FL_POWER = 8;    // power grid, low-power flag, low-power limits

// MsFlight.flags bits
constexpr int FF_ALIVE = 1;        // ship found and alive
constexpr int FF_HAS_ACTOR = 2;    // Actor property non-null
constexpr int FF_ACTOR_FRAME = 4;  // act_loc / fw / R / U read
constexpr int FF_OBJ_POSE = 8;     // ch + loc_raw + quat read
constexpr int FF_AUTOPILOT = 16;   // bAutopilot true
constexpr int FF_DOCK_AP = 32;     // bDockingAutopilot true
constexpr int FF_TRAVELLING = 64;  // bTravelling true
constexpr int FF_GRID = 128;       // power grid numbers read
constexpr int FF_LP_KNOWN = 256;   // low-power flag readable
constexpr int FF_LP_ON = 512;      // low-power flag true
constexpr int FF_LP_LIMITS = 1024; // LowPowerThrottle and MaxLowPowerSpeed read
constexpr int FF_THROTTLE = 2048;  // Throttle read
constexpr int FF_STEPS = 4096;     // GetThrottleSteps read
constexpr int FF_COLL = 8192;      // CollisionRadius read and > 0

static_assert(sizeof(MsFlight) == 264, "MsFlight layout must match the MSCore native API 10 doc");
static_assert(offsetof(MsFlight, size) == 0 && offsetof(MsFlight, flags) == 4 && offsetof(MsFlight, sid) == 8, "MsFlight header offsets");
static_assert(offsetof(MsFlight, sys) == 12 && offsetof(MsFlight, throttle) == 16 && offsetof(MsFlight, steps) == 20, "MsFlight state offsets");
static_assert(offsetof(MsFlight, lp_thr) == 24 && offsetof(MsFlight, pad) == 28 && offsetof(MsFlight, ch) == 32, "MsFlight power/chunk offsets");
static_assert(offsetof(MsFlight, max_mod_raw) == 56 && offsetof(MsFlight, max_base_raw) == 64 && offsetof(MsFlight, coll_radius_raw) == 72, "MsFlight limit offsets");
static_assert(offsetof(MsFlight, act_loc) == 80 && offsetof(MsFlight, fw) == 104 && offsetof(MsFlight, R) == 128 && offsetof(MsFlight, U) == 152, "MsFlight frame offsets");
static_assert(offsetof(MsFlight, loc_raw) == 176 && offsetof(MsFlight, quat) == 200 && offsetof(MsFlight, grid_prod) == 232, "MsFlight pose/power offsets");
static_assert(offsetof(MsFlight, grid_cons) == 240 && offsetof(MsFlight, grid_eff) == 248 && offsetof(MsFlight, lp_frac) == 256, "MsFlight power offsets");

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
        }
        const bool ok = m.h && m.api && m.ship_flight && m.ship_set_throttle && m.ship_zero && m.ship_console && m.actor_loc && m.api() >= MS_NEED_API;
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
}
