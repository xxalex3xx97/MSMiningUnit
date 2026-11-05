// FILE: mods/MiningBenchMod/mb_common.hpp
// WHAT: includes, shared globals, string/file/log helpers, tlog(), alive(). Nothing feature-specific.
// WHY separate: every other header needs these, so this must be included first.
// DEPENDS ON (exact): Windows.h; std headers (algorithm, atomic, cwctype, cctype, cmath, cstdlib, cstring, filesystem, fstream, iterator, map, unordered_set, set, stdexcept, string, vector); UE4SS headers Mod/CppUserModBase.hpp, DynamicOutput/DynamicOutput.hpp, Unreal/Property/FEnumProperty.hpp, Unreal/UObject.hpp, Unreal/UObjectGlobals.hpp, Unreal/UClass.hpp, Unreal/FString.hpp, Unreal/FText.hpp, Unreal/CoreUObject/UObject/UnrealType.hpp, Unreal/CoreUObject/UObject/Class.hpp, Unreal/Hooks/Hooks.hpp (RE-UE4SS built at cdbc0b66, same as the game UE4SS)
// USED BY: every later mb_*.hpp and dllmain.cpp
// GAME FACTS: UObjects are destroyed by the game at any time, so check alive() before using an object that may be stale. A native access violation kills the whole game (log 2026-10-27), which is why alive() exists.
#pragma once

// mb_logic.hpp: pure config/parse/rule helpers (trim, to_int, Config, eff_cfg, ...). Included first: no UE or Windows headers.
#include "mb_logic.hpp"

// Trim Windows.h, and NOMINMAX stops its min/max macros from clashing with std::min and std::max.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_set>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Property/FEnumProperty.hpp> // ViewInput.ControlMode is an enum; the steer command needs this header (brief Update 28)
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/Hooks/Hooks.hpp>

// RC and RC::Unreal are the UE4SS namespaces; UObject, FProperty, STR() and friends come from them.
using namespace RC;
using namespace RC::Unreal;

// crash guard: g_shutdown is set in ~MiningBenchMod. The engine tick callback checks it first, and the callback is unregistered there too.
// WHY: the engine can tick while the mod is unloading; without the flag the callback would run on half-destroyed state.
std::atomic<bool> g_shutdown{false};
// UE4SS handle of the engine tick callback; 0 = not registered. ~MiningBenchMod uses it to unregister.
uint64_t g_tick_cb = 0;

// Internal helpers and state. Anonymous namespace, so nothing here is exported from the DLL.
namespace
{
    // WHAT: true when o is non-null, has a class and is not unreachable (pending destruction).
    // WHY: the game frees UObjects at any time; calling into a dead one is a native access violation that kills the game (log 2026-10-27).
    bool alive(UObject* o) { return o && o->GetClassPrivate() && !o->IsUnreachable(); }

    // WHAT: content of ships.txt last written (sentinel 0x01 forces the first write). Units: text.
    // WHY: rewrite the file only when the content changes.
    std::string g_sig_ships_file = "\x01";      // ships.txt content last written

    // WHAT: seconds since the last tick_work scan (gate for log_interval_seconds).
    float g_accum = 0.0f;

    // WHAT: running sum of engine tick dt, in seconds. A game-time clock, not wall-clock time.
    double g_now = 0.0;

    // WHAT: true once the engine tick hook is registered.
    // WHY: on_unreal_init can run again; this stops the hook being registered twice.
    bool g_hook_registered = false;

    // WHAT: ship ID -> last skip reason. Cleared on config reload.
    // WHY: the same skip reason is logged once instead of on every scan.
    std::map<int, std::wstring> g_skip;

    // WHAT: MiningBench.ini last-write time at the last successful or failed load.
    // WHY: check_config() reloads only when this changes (INI hot reload).
    std::filesystem::file_time_type g_ini_mtime{};   // last_write_time of MiningBench.ini at last load

    // WHAT: docks.txt content last written (sentinel forces the first write).
    // WHY: skip the write when nothing changed.
    std::string g_sig_dock_file = "\x01";           // docks.txt content last written (sentinel forces first write)

    // WHAT: item count last written to items.txt (-1 = never written). Units: count.
    // WHY: items.txt is rewritten only when the count changes.
    int g_items_file_count = -1;                     // item count last written to items.txt

    // WHAT: set after the "lists written" line is logged once (write_lists in mb_docks.hpp).
    // WHY: keeps that line out of the log on every rewrite of docks.txt / items.txt.
    bool g_lists_logged = false;


    // WHAT: empty function. Its address identifies this DLL.
    // WHY: get_dll_dir() needs an address inside the module for GetModuleHandleEx.
    void anchor() {}

    // WHAT: folder of this DLL (ue4ss/Mods/MiningBenchMod/dlls in the game).
    // WHY: MiningBench.ini and the text files the Lua UI reads sit next to the DLL.
    std::filesystem::path get_dll_dir()
    {
        HMODULE h = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&anchor), &h);
        wchar_t buf[MAX_PATH]{};
        GetModuleFileNameW(h, buf, MAX_PATH);
        return std::filesystem::path(buf).parent_path();
    }


    // WHAT: narrow to wide by casting each byte. Units: none.
    // WHY: names and INI text are ASCII. This is NOT a UTF-8 decode, so non-ASCII text is mangled (use to_utf8 for the other direction).
    std::wstring widen(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());
    }

    // WHAT: lowercase copy (towlower per character).
    // WHY: case-insensitive matches for INI values such as dock names.
    std::wstring lower(std::wstring s)
    {
        for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
        return s;
    }

    // WHAT: Test-tab "Debug log" switch (cmd debug_on/debug_off). Runtime only: off after every start.
    // WHY: per-second watch/scan lines flooded UE4SS.log; by default only warnings and errors are written.
    inline bool g_debug = false;

    // WHAT: writes the text with a [MiningBenchMod] prefix to the UE4SS log (UE4SS.log in the ue4ss folder).
    // WHY: the user checks UE4SS.log for these lines, and every feature logs through here.
    void log_line(const std::wstring& s)
    {
        if (!g_debug)
        {
            std::wstring l = s;
            for (auto& c : l) c = static_cast<wchar_t>(std::towlower(c));
            static const wchar_t* keep[] = { L"error", L"fail", L"warn", L"exception", L"halt", L"stopped", L"loaded", L"mining loop", L"going to", L"dock retry", L"mine weapons", L"warpsector", L"sector ", L"fuelchk", L"fuelwarp", L"jumpcost", L"warpcur", L"warpreal", L"chunkhold", L"devstation", L"sweep", L"fault", L"compat", L"combat", L"attack", L"reg " };
            bool hit = false;
            for (auto k : keep) if (l.find(k) != std::wstring::npos) { hit = true; break; }
            if (!hit) return;
        }
        Output::send<LogLevel::Normal>(STR("[MiningBenchMod] {}\n"), s);
    }

    // WHAT: last SEH exception seen by seh_filter: code, ExceptionAddress, module of that address (base, offset, file name), access violation kind
    //       (0 read, 1 write, 8 DEP, -1 not an access violation) and target address. Game thread only.
    // WHY: a fault line with the code alone does not say where it hit. The filter runs before the stack unwinds, so the address is still valid there.
    struct SehInfo
    {
        unsigned long code = 0;
        uint64_t addr = 0;
        uint64_t mod_base = 0;
        uint64_t mod_off = 0;
        wchar_t mod[260] = {0};
        int av = -1;
        uint64_t av_target = 0;
    };
    SehInfo g_seh;
    // WHAT: label of the step running now (cmd_tick part, or mine ship + state). Set before each step; copied into every fault line.
    wchar_t g_last_step[160] = {0};

    // WHAT: sets the step label (g_last_step) that the next fault line shows.
    void set_last_step(const std::wstring& s) { wcsncpy_s(g_last_step, s.c_str(), _TRUNCATE); }

    // WHAT: __except filter. Fills g_seh from the exception record. C++ exceptions (0xE06D7363) are not taken: they go on to the C++ catches.
    int seh_filter(EXCEPTION_POINTERS* ep)
    {
        EXCEPTION_RECORD* r = ep->ExceptionRecord;
        if (r->ExceptionCode == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
        SehInfo s;
        s.code = r->ExceptionCode;
        s.addr = reinterpret_cast<uint64_t>(r->ExceptionAddress);
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        {
            s.av = static_cast<int>(r->ExceptionInformation[0]);
            s.av_target = static_cast<uint64_t>(r->ExceptionInformation[1]);
        }
        HMODULE hm = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(r->ExceptionAddress), &hm) && hm)
        {
            s.mod_base = reinterpret_cast<uint64_t>(hm);
            s.mod_off = s.addr - s.mod_base;
            GetModuleFileNameW(hm, s.mod, 260);
        }
        g_seh = s;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // WHAT: one-line text of a fault: code, address, module + offset, access violation kind and target.
    std::wstring seh_describe(const SehInfo& s)
    {
        wchar_t b[640];
        const wchar_t* av = s.av == 0 ? L"read" : s.av == 1 ? L"write" : s.av == 8 ? L"dep" : L"-";
        swprintf(b, 640, L"code=0x%08lx addr=0x%llx module=%ls base=0x%llx off=0x%llx av=%ls target=0x%llx",
                 s.code, static_cast<unsigned long long>(s.addr), s.mod[0] ? s.mod : L"?", static_cast<unsigned long long>(s.mod_base),
                 static_cast<unsigned long long>(s.mod_off), av, static_cast<unsigned long long>(s.av_target));
        return b;
    }

    // WHAT: fn(ctx) under SEH. false = an SEH fault (g_seh holds it). C++ exceptions pass through to the caller.
    // WHY: MSVC forbids __try in a function with C++ objects, so this function has none; the work (and its objects) sits in fn.
    bool seh_run(void (*fn)(void*), void* ctx)
    {
        __try { fn(ctx); return true; }
        __except (seh_filter(GetExceptionInformation())) { return false; }
    }

    // WHAT: UObjectGlobals::FindAllOf under SEH. false = fault (g_seh holds it); out may be partly filled.
    struct FindAllCtx { const wchar_t* cls; std::vector<UObject*>* out; };
    bool find_all_seh(const wchar_t* cls, std::vector<UObject*>& out)
    {
        FindAllCtx c{cls, &out};
        return seh_run([](void* p) { auto* x = static_cast<FindAllCtx*>(p); UObjectGlobals::FindAllOf(x->cls, *x->out); }, &c);
    }


    // WHAT: wide string to UTF-8 bytes (WideCharToMultiByte).
    // WHY: the text files (docks.txt, items.txt, ...) are read by the Lua UI, which expects UTF-8.
    std::string to_utf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    // WHAT: one list field as UTF-8, with '|' and newlines turned into spaces.
    // WHY: rows are '|'-separated, one row per line; a '|' or newline inside a name would break the Lua parser.
    std::string u8(const std::wstring& w)
    {
        std::string s = to_utf8(w);
        for (auto& c : s) if (c == '|' || c == '\r' || c == '\n') c = ' ';
        return s;
    }

    // WHAT: writes data to p through p.tmp, then renames it over p. Throws when the temp file cannot be written.
    // WHY: the Lua UI can read these files at any moment; the rename swaps in the complete file, so it never sees a half-written one.
    void write_atomic(const std::filesystem::path& p, const std::string& data)
    {
        auto tmp = p;
        tmp += L".tmp";
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            if (!o) throw std::runtime_error("cannot write temp file");
            o << data;
        }
        std::filesystem::rename(tmp, p);
    }

    // WHAT: one combat line: UE4SS log (log_line) and MiningCombat.txt in the dlls folder ("hh:mm:ss text", appended). Past 200 KB the older half is dropped.
    // WHY: the combat record must survive the UE4SS.log rotation; the file is the place to read after a loss. Lines are rare (flee, hull drop, destroyed, 30 s summary).
    void combat_log(const std::wstring& s)
    {
        log_line(s);
        try
        {
            const auto path = get_dll_dir() / "MiningCombat.txt";
            SYSTEMTIME st;
            GetLocalTime(&st);
            char stamp[16];
            std::snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u ", static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond));
            std::error_code ec;
            const auto sz = std::filesystem::file_size(path, ec);
            if (!ec && sz > 200 * 1024)
            {
                std::ifstream in(path, std::ios::binary);
                std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                in.close();
                write_atomic(path, combat_log_trim(old, 200 * 1024));
            }
            std::ofstream o(path, std::ios::binary | std::ios::app);
            o << stamp << to_utf8(s) << "\n";
        }
        catch (...) { }   // a log file that cannot be written must not stop the mining loop
    }

    // WHAT: int64 as a wide decimal string.
    // WHY: logs and text files need 64-bit raw values (PNum can be int64).
    std::wstring i64w(int64_t v) { return std::to_wstring(static_cast<long long>(v)); }

    // WHAT: pointer to an int32 array property by name; nullptr if missing or not an array.
    // WHY: game objects are read by reflection name (no SDK headers); the caller decides what the list means.
    TArray<int32_t>* int_array(UObject* o, const TCHAR* prop)
    {
        if (!alive(o)) return nullptr;
        FProperty* p = o->GetPropertyByNameInChain(prop);
        return (p && CastField<FArrayProperty>(p)) ? o->GetValuePtrByPropertyNameInChain<TArray<int32_t>>(prop) : nullptr;
    }

    // WHAT: log line with the prefix "test cmd: ".
    // WHY: Test-tab command results are found in UE4SS.log by that prefix.
    void tlog(const std::wstring& s) { log_line(STR("test cmd: ") + s); }
}
