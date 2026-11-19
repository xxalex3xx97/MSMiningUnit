// FILE: mods/MiningBenchMod/mb_mscore.hpp
// WHAT: loads the MSCore DLL (ue4ss/Mods/MSCore/dlls/main.dll) and resolves its exports (API 2). Command plumbing, orders, autopilot, filters, warp primitives, sector list, docks, items and the map pin forward to it. Game class names (asteroid, station, containers) come from MSCore_class by logical name (ms_cls).
// WHY: the stateless game calls moved into MSCore so every mod shares one copy. Mining keeps its state machines and only forwards the primitives.
// DEPENDS ON: mb_common (get_dll_dir, log_line, STR).
// USED BY: mb_orders (add_input, orders, autopilot), mb_steer (view_send), mb_warpreal (throttle), mb_filters, mb_docks, mb_warp, mb_belt, mb_pin, mb_dockjob, dllmain.
// NOTE: MsFilterRow and MsSector must match F:\MSCore\cpp\MSCore\msc_api.hpp (same layout, same sizes).
#pragma once

#include <map>
#include <string>
#include "sc_flight_abi.h"   // MsFlight, FL_*, FF_* (SC6-D3a copy of msc_api.hpp, build.ps1 copies it)

namespace
{
    // one logistic filter row: item (UObject*), amount, action (0 LIMIT, 1 BUFFER)
    struct MsFilterRow
    {
        void* item;
        int32_t amount;
        int32_t action;
    };

    // one sector: save id, solar system id, ore count, chunk, half extents, warp arrival distance (raw), ore items (UObject*, max 40), display name
    struct MsSector
    {
        int32_t id;
        int32_t sys;
        int32_t nores;
        int32_t pad;
        long long ch[3];
        long long he[3];
        long long warp_dist;
        void* ores[40];
        wchar_t name[96];
    };

    static_assert(sizeof(MsFilterRow) == 16, "MsFilterRow layout must match msc_api.hpp");
    static_assert(sizeof(MsSector) == 584, "MsSector layout must match msc_api.hpp");

    // WHAT: resolved MSCore entry points. h is the loaded module (nullptr = not loaded).
    struct MsApi
    {
        HMODULE h = nullptr;
        int (*api)() = nullptr;
        int (*add_input)(int, void*, wchar_t*, int) = nullptr;
        int (*view_send)(int, long long, long long, wchar_t*, int) = nullptr;
        int (*throttle)(int, int, wchar_t*, int) = nullptr;
        void* (*find_item)(const char*, wchar_t*, int) = nullptr;
        int (*find_dock)(const char*, wchar_t*, int) = nullptr;
        int (*new_order)(int, const wchar_t*, wchar_t*, int) = nullptr;
        int (*update_order)(int, int, void*, int, int, wchar_t*, int) = nullptr;
        int (*delete_order)(int, int, wchar_t*, int) = nullptr;
        int (*autopilot)(int, int, wchar_t*, int) = nullptr;
        int (*skip_to)(int, int, int*, int*, wchar_t*, int) = nullptr;
        int (*filter_send)(int, int, void*, int, int, int, wchar_t*, int) = nullptr;
        int (*filters_read)(void*, MsFilterRow*, int) = nullptr;
        int (*warp_send)(int, int, const long long*, const long long*, int, int, wchar_t*, int) = nullptr;
        int (*arrival)(void*, int, const long long*, long long, long long*, wchar_t*, int) = nullptr;
        int (*jump_cost)(void*, int, const long long*, int*, wchar_t*, int) = nullptr;
        int (*chunk_of)(void*, long long*) = nullptr;
        int (*axes_of)(void*, const wchar_t*, long long*) = nullptr;
        int (*sys_id)(void*) = nullptr;
        int (*sec_warp_dist)(void*, long long*) = nullptr;
        int (*sectors)(MsSector*, int) = nullptr;
        int (*pin_ensure)() = nullptr;
        int (*pinned)() = nullptr;
        int (*chunk_hold2)(int, int, const long long*, int, wchar_t*, int) = nullptr;
        int (*chunk_release)(int) = nullptr;
        int (*chunk_ready)(wchar_t*, int) = nullptr;
        int (*cls)(const char*, char*, int) = nullptr;
        int (*ship_shields)(void*, int*, int*) = nullptr;
        int (*ship_flight)(int, int, MsFlight*) = nullptr;
        int (*ship_set_throttle)(int, int, wchar_t*, int) = nullptr;
        int (*ship_zero)(int, wchar_t*, int) = nullptr;
    };
    MsApi g_ms;

    // WHAT: loads MSCore once and resolves the exports. true when ready. On failure err says why and the call is retried on the next use.
    // WHY: MSCore may be missing or older than this mod; the mod must report that instead of crashing. API 2 is needed for the order, warp and sector exports.
    bool ms_load(std::wstring& err)
    {
        if (g_ms.h) return true;
        static bool logged = false;
        auto path = (get_dll_dir() / L"../../MSCore/dlls/main.dll").lexically_normal();
        HMODULE h = LoadLibraryW(path.c_str());
        MsApi m;
        m.h = h;
        if (h)
        {
            m.api = reinterpret_cast<int (*)()>(GetProcAddress(h, "MSCore_api"));
            m.add_input = reinterpret_cast<int (*)(int, void*, wchar_t*, int)>(GetProcAddress(h, "MSCore_add_input"));
            m.view_send = reinterpret_cast<int (*)(int, long long, long long, wchar_t*, int)>(GetProcAddress(h, "MSCore_view_send"));
            m.throttle = reinterpret_cast<int (*)(int, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_throttle"));
            m.find_item = reinterpret_cast<void* (*)(const char*, wchar_t*, int)>(GetProcAddress(h, "MSCore_find_item"));
            m.find_dock = reinterpret_cast<int (*)(const char*, wchar_t*, int)>(GetProcAddress(h, "MSCore_find_dock"));
            m.new_order = reinterpret_cast<int (*)(int, const wchar_t*, wchar_t*, int)>(GetProcAddress(h, "MSCore_new_order"));
            m.update_order = reinterpret_cast<int (*)(int, int, void*, int, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_update_order"));
            m.delete_order = reinterpret_cast<int (*)(int, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_delete_order"));
            m.autopilot = reinterpret_cast<int (*)(int, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_autopilot"));
            m.skip_to = reinterpret_cast<int (*)(int, int, int*, int*, wchar_t*, int)>(GetProcAddress(h, "MSCore_skip_to"));
            m.filter_send = reinterpret_cast<int (*)(int, int, void*, int, int, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_filter_send"));
            m.filters_read = reinterpret_cast<int (*)(void*, MsFilterRow*, int)>(GetProcAddress(h, "MSCore_filters_read"));
            m.warp_send = reinterpret_cast<int (*)(int, int, const long long*, const long long*, int, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_warp_send"));
            m.arrival = reinterpret_cast<int (*)(void*, int, const long long*, long long, long long*, wchar_t*, int)>(GetProcAddress(h, "MSCore_arrival"));
            m.jump_cost = reinterpret_cast<int (*)(void*, int, const long long*, int*, wchar_t*, int)>(GetProcAddress(h, "MSCore_jump_cost"));
            m.chunk_of = reinterpret_cast<int (*)(void*, long long*)>(GetProcAddress(h, "MSCore_chunk_of"));
            m.axes_of = reinterpret_cast<int (*)(void*, const wchar_t*, long long*)>(GetProcAddress(h, "MSCore_axes_of"));
            m.sys_id = reinterpret_cast<int (*)(void*)>(GetProcAddress(h, "MSCore_sys_id"));
            m.sec_warp_dist = reinterpret_cast<int (*)(void*, long long*)>(GetProcAddress(h, "MSCore_sec_warp_dist"));
            m.sectors = reinterpret_cast<int (*)(MsSector*, int)>(GetProcAddress(h, "MSCore_sectors"));
            m.pin_ensure = reinterpret_cast<int (*)()>(GetProcAddress(h, "MSCore_pin_ensure"));
            m.pinned = reinterpret_cast<int (*)()>(GetProcAddress(h, "MSCore_pinned"));
            m.chunk_hold2 = reinterpret_cast<int (*)(int, int, const long long*, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_chunk_hold2"));
            m.chunk_release = reinterpret_cast<int (*)(int)>(GetProcAddress(h, "MSCore_chunk_release"));
            m.chunk_ready = reinterpret_cast<int (*)(wchar_t*, int)>(GetProcAddress(h, "MSCore_chunk_ready"));
            m.cls = reinterpret_cast<int (*)(const char*, char*, int)>(GetProcAddress(h, "MSCore_class"));
            m.ship_shields = reinterpret_cast<int (*)(void*, int*, int*)>(GetProcAddress(h, "MSCore_ship_shields"));
            m.ship_flight = reinterpret_cast<int (*)(int, int, MsFlight*)>(GetProcAddress(h, "MSCore_ship_flight"));
            m.ship_set_throttle = reinterpret_cast<int (*)(int, int, wchar_t*, int)>(GetProcAddress(h, "MSCore_ship_set_throttle"));
            m.ship_zero = reinterpret_cast<int (*)(int, wchar_t*, int)>(GetProcAddress(h, "MSCore_ship_zero"));
        }
        const bool full = m.api && m.add_input && m.view_send && m.throttle && m.find_item && m.find_dock && m.new_order &&
                          m.update_order && m.delete_order && m.autopilot && m.skip_to && m.filter_send && m.filters_read &&
                          m.warp_send && m.arrival && m.jump_cost && m.chunk_of && m.axes_of && m.sys_id && m.sec_warp_dist &&
                          m.sectors && m.pin_ensure && m.pinned;
        if (!h || !full || m.api() < 2)
        {
            if (h) FreeLibrary(h);
            err = L"MSCore not installed or too old (needs API 2)";
            if (!logged) { logged = true; log_line(std::wstring(STR("MSCORE load failed: ")) + path.wstring()); }
            return false;
        }
        g_ms = m;
        if (!logged) { logged = true; log_line(std::wstring(STR("MSCORE loaded: ")) + path.wstring()); }
        return true;
    }

    // WHAT: true when MSCore has the chunk hold exports (0.8.7+). Not part of ms_load's API 2 check: an older MSCore keeps the mod running with chunk hold off.
    bool ms_chunk_ok(std::wstring& why)
    {
        if (!ms_load(why)) return false;
        if (!g_ms.chunk_hold2 || !g_ms.chunk_release || !g_ms.chunk_ready)
        {
            why = L"MSCore too old for chunk hold (needs 0.8.7 or newer)";
            return false;
        }
        return true;
    }

    // WHAT: game class name for a logical name from MSCore (MSCore_class, 0.8.10+). Empty = MSCore missing/too old/unknown: caller turns its feature off. Cached once found. Game thread only.
    std::wstring ms_cls(const char* logical) {
        static std::map<std::string, std::wstring> cache;
        auto it = cache.find(logical); if (it != cache.end()) return it->second;
        std::wstring err; if (!ms_load(err)) return std::wstring();
        if (!g_ms.cls) { static bool warned = false; if (!warned) { warned = true; log_line(STR("MSCore too old (needs 0.8.10): asteroid, station and container features are off")); } return std::wstring(); }
        char buf[96] = {}; if (!g_ms.cls(logical, buf, static_cast<int>(sizeof(buf)))) return std::wstring();
        std::wstring w(buf, buf + strlen(buf)); cache[logical] = w; return w; }

    // WHAT: true when MSCore has the ship exports (0.8.12+: MSCore_class and MSCore_ship_shields). Not part of ms_load's API 2 check: an older MSCore keeps the mod running with ship features off.
    bool ms_ships_ok(std::wstring& why)
    {
        if (!ms_load(why)) return false;
        if (!g_ms.cls || !g_ms.ship_shields)
        {
            why = L"MSCore too old for ship shields (needs 0.8.12)";
            return false;
        }
        return true;
    }

    // WHAT: game class name of the player ship (logical 'ship'). Empty = MSCore missing or too old: callers skip their ship work (logged once here). Cached once found. Game thread only.
    // WHY: the class name is game-fragile; the mod never holds it.
    std::wstring ms_ship_cls()
    {
        static std::wstring cached;
        if (!cached.empty()) return cached;
        static bool warned = false;
        std::wstring why;
        if (ms_ships_ok(why)) cached = ms_cls("ship");
        if (cached.empty() && !warned) { warned = true; log_line(STR("MSCore ship class unavailable (needs 0.8.12): ship features are off")); }
        return cached;
    }

    // WHAT: shields of a ship from MSCore_ship_shields, whole units: cur / mx = -1 when unreadable. Returns the export's bitmask (1 = cur ok, 2 = max ok), 0 = none readable, -2 module off, -1 exception or MSCore missing / too old.
    int ms_shields(UObject* ship, int& cur, int& mx)
    {
        cur = mx = -1;
        std::wstring why;
        if (!ms_ships_ok(why)) return -1;
        int c = -1, m = -1;
        const int r = g_ms.ship_shields(static_cast<void*>(ship), &c, &m);
        if (r > 0 && (r & 1)) cur = c;
        if (r > 0 && (r & 2)) mx = m;
        return r;
    }

    // WHAT: MSCore_ship_flight of ship sid for the FL_* groups in mask, into f (f zeroed, f.size set here). Returns the export's code:
    //       1 ok (read f.flags for what worked), 0 ship gone, -1 fault, -2 flight module off, -3 bad size, -9 export missing or exception.
    // WHY: the one read of a ship's flight state (MSCore API 10). Mining reads no game property names for these values. Exceptions never leave.
    int ms_flight_read(int sid, int mask, MsFlight& f)
    {
        f = MsFlight{};
        f.size = static_cast<int32_t>(sizeof(MsFlight));
        std::wstring why;
        if (!ms_load(why) || !g_ms.ship_flight) return -9;
        try { return g_ms.ship_flight(sid, mask, &f); }
        catch (...) { return -9; }
    }

    // WHAT: MSCore_ship_zero for ship sid (Throttle 0, rotation 0). true = zeroed. Missing export or exception = false (caller uses the legacy write).
    // WHY: a missing export must not make Stop ineffective, so the caller falls back to the property writes.
    bool ms_zero_ship(int sid)
    {
        std::wstring why;
        if (!ms_load(why) || !g_ms.ship_zero) return false;
        wchar_t buf[256] = {0};
        try { return g_ms.ship_zero(sid, buf, 256) == 1; }
        catch (...) { return false; }
    }

    // WHAT: MSCore_ship_set_throttle (Throttle = absolute step). true = written. Missing export or exception = false, err says why.
    bool ms_set_throttle(int sid, int step, std::wstring& err)
    {
        std::wstring why;
        if (!ms_load(why) || !g_ms.ship_set_throttle) { err = L"MSCore flight unavailable"; return false; }
        wchar_t buf[256] = {0};
        try
        {
            const bool ok = g_ms.ship_set_throttle(sid, step, buf, 256) == 1;
            if (!ok) err = buf;
            return ok;
        }
        catch (...) { err = L"exception: MSCore_ship_set_throttle"; return false; }
    }

    // WHAT: log text of one FL_STATE flag bit: "1" / "0" when the state read worked (r == 1), "n/a" otherwise. A bool flag bit is set only when true.
    std::wstring ms_flag_text(int r, const MsFlight& f, int bit)
    {
        if (r != 1) return std::wstring(STR("n/a"));
        return (f.flags & bit) ? std::wstring(STR("1")) : std::wstring(STR("0"));
    }

    // WHAT: log text of Throttle: the value when the read gave it (FF_THROTTLE), "n/a" otherwise.
    std::wstring ms_throttle_text(int r, const MsFlight& f)
    {
        return (r == 1 && (f.flags & FF_THROTTLE)) ? std::to_wstring(f.throttle) : std::wstring(STR("n/a"));
    }

    // WHAT: copies the export's error text into err when it failed; returns the export's ok flag.
    // WHY: every forwarder reads its error the same way (wchar_t e[256] filled by the DLL).
    bool ms_err(int ok, const wchar_t* e, std::wstring& err)
    {
        if (!ok) err = e;
        return ok != 0;
    }
}
