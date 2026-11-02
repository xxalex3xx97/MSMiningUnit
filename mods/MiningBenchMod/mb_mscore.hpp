// FILE: mods/MiningBenchMod/mb_mscore.hpp
// WHAT: loads the MSCore DLL (ue4ss/Mods/MSCore/dlls/main.dll) and resolves its exports (API 2). Command plumbing, orders, autopilot, filters, warp primitives, sector list, docks, items and the map pin forward to it.
// WHY: the stateless game calls moved into MSCore so every mod shares one copy. Mining keeps its state machines and only forwards the primitives.
// DEPENDS ON: mb_common (get_dll_dir, log_line, STR).
// USED BY: mb_orders (add_input, orders, autopilot), mb_steer (view_send), mb_warpreal (throttle), mb_filters, mb_docks, mb_warp, mb_belt, mb_pin, mb_dockjob, dllmain.
// NOTE: MsFilterRow and MsSector must match F:\MSCore\cpp\MSCore\msc_api.hpp (same layout, same sizes).
#pragma once

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
        }
        const bool full = m.api && m.add_input && m.view_send && m.throttle && m.find_item && m.find_dock && m.new_order &&
                          m.update_order && m.delete_order && m.autopilot && m.skip_to && m.filter_send && m.filters_read &&
                          m.warp_send && m.arrival && m.jump_cost && m.chunk_of && m.axes_of && m.sys_id && m.sec_warp_dist &&
                          m.sectors && m.pin_ensure && m.pinned;
        if (!h || !full || m.api() != 2)
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

    // WHAT: copies the export's error text into err when it failed; returns the export's ok flag.
    // WHY: every forwarder reads its error the same way (wchar_t e[256] filled by the DLL).
    bool ms_err(int ok, const wchar_t* e, std::wstring& err)
    {
        if (!ok) err = e;
        return ok != 0;
    }
}
