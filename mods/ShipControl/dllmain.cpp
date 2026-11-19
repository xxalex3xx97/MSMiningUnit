// FILE: mods/ShipControl/dllmain.cpp
// WHAT: UE4SS C++ mod entry point for ShipControl. Holds the ship owner registry and exports the C ABI in sc_api.h. SC0: owner registry only, no engine hook.
// WHY separate: UE4SS needs start_mod and uninstall_mod in the DLL entry file.
// DEPENDS ON (exact, include order matters): sc_version.hpp, sc_common.hpp, sc_owner.hpp
// USED BY: the UE4SS loader (mods.txt line "ShipControl : 1" or enabled.txt), which calls start_mod and uninstall_mod. Other mods load this DLL by LoadLibrary and GetProcAddress (sc_api.h).
// GAME FACTS: none. Every export catches exceptions and returns a failure code; a failure never reaches the caller as a throw.
#include <atomic>
#include <mutex>
#include <string>
#include "sc_version.hpp"
#include "sc_common.hpp"
#include "sc_owner.hpp"

namespace
{
    std::mutex g_mx;                 // guards g_owners (exports may run on any thread)
    ScOwners g_owners;
    std::atomic<bool> g_killed{false};   // kill switch: refuses claim/release while true

    // WHAT: copies why into err (NUL-terminated, at most errlen wide chars). errlen <= 0 or err null = no copy.
    // WHY: the C ABI reports refusals as text in a caller buffer, like MSCore's exports.
    void put_err(const std::string& why, wchar_t* err, int errlen)
    {
        if (!err || errlen <= 0) return;
        std::wstring w = widen(why);
        int n = static_cast<int>(w.size()) < errlen - 1 ? static_cast<int>(w.size()) : errlen - 1;
        for (int i = 0; i < n; ++i) err[i] = w[i];
        err[n] = 0;
    }

    // WHAT: wide owner name to narrow, ASCII only, non-empty. false = why set.
    // WHY: owner names are short mod tags (mining, attack, ...); a non-ASCII name is refused rather than mangled.
    bool owner_arg(const wchar_t* owner, std::string& out, std::string& why)
    {
        if (!owner) { why = "owner is null"; return false; }
        out.clear();
        for (const wchar_t* p = owner; *p; ++p)
        {
            if (*p < 1 || *p > 127) { why = "owner must be ASCII"; return false; }
            out.push_back(static_cast<char>(*p));
        }
        if (out.empty()) { why = "owner is empty"; return false; }
        return true;
    }

    // WHAT: runs fn (sets why and returns ok) with the registry lock held; catches every exception. Returns 1 ok, 0 failed.
    // WHY: one place for try/catch and the err text, so each export stays a few lines.
    template <class F>
    int run_export(F fn, wchar_t* err, int errlen)
    {
        try
        {
            std::string why;
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load()) { put_err("ShipControl killed", err, errlen); return 0; }
            if (fn(why)) return 1;
            put_err(why, err, errlen);
            return 0;
        }
        catch (const std::exception& e) { put_err(std::string("exception: ") + e.what(), err, errlen); }
        catch (...) { put_err("exception: unknown", err, errlen); }
        return 0;
    }
}

class ShipControl : public RC::CppUserModBase
{
public:
    // WHAT: fills in the UE4SS mod metadata and logs the version.
    // WHY: UE4SS shows the name and version; the load line in UE4SS.log proves the DLL loaded.
    ShipControl() : CppUserModBase()
    {
        ModName = STR("ShipControl");
        ModVersion = widen(kVersion);
        ModDescription = STR("Ship control core: owner registry for ship behaviours");
        ModAuthors = STR("local");
        log_line(widen(std::string("ShipControl v") + kVersion + " loaded (SC0 owner registry)"));
    }
};

// Export marker: makes start_mod and uninstall_mod visible to the UE4SS loader by name, and the SC_ names to other mods.
#define SHIP_CONTROL_API __declspec(dllexport)
extern "C"
{
    // WHAT: UE4SS entry point; creates the mod object and returns it. nullptr on failure.
    // WHY: UE4SS loads a C++ mod by calling this exported C symbol.
    SHIP_CONTROL_API RC::CppUserModBase* start_mod()
    {
        try { return new ShipControl(); }
        catch (...) { return nullptr; }
    }

    // WHAT: UE4SS unload entry point; deletes the mod object.
    SHIP_CONTROL_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        try { delete mod; }
        catch (...) {}
    }

    // WHAT: API level (sc_api.h SC_API_LEVEL). Callers refuse to run below the level they need.
    SHIP_CONTROL_API int SC_api()
    {
        try { return 1; }
        catch (...) { return 0; }
    }

    // WHAT: capability bits. bit 0 = owner registry.
    SHIP_CONTROL_API int SC_caps()
    {
        try { return 1; }
        catch (...) { return 0; }
    }

    // WHAT: kill switch. on=1 refuses claim/release until on=0.
    // WHY: fail closed: a bad ShipControl state can be stopped without unloading the mod.
    SHIP_CONTROL_API int SC_kill(int on, wchar_t* err, int errlen)
    {
        try
        {
            g_killed.store(on != 0);
            log_line(on ? L"kill switch ON" : L"kill switch OFF");
            return 1;
        }
        catch (const std::exception& e) { put_err(std::string("exception: ") + e.what(), err, errlen); }
        catch (...) { put_err("exception: unknown", err, errlen); }
        return 0;
    }

    // WHAT: owner claims ship. 1 = ok, 0 = refused (err says who owns it).
    SHIP_CONTROL_API int SC_claim(const wchar_t* owner, int ship, wchar_t* err, int errlen)
    {
        return run_export([&](std::string& why) {
            std::string who;
            if (!owner_arg(owner, who, why)) return false;
            return g_owners.claim(ship, who, why);
        }, err, errlen);
    }

    // WHAT: owner name of ship into out (NUL-terminated). 1 = owned, 0 = free or failed.
    SHIP_CONTROL_API int SC_owner(int ship, wchar_t* out, int outlen)
    {
        try
        {
            std::string who;
            {
                std::lock_guard<std::mutex> lk(g_mx);
                who = g_owners.owner_of(ship);
            }
            if (who.empty()) { if (out && outlen > 0) out[0] = 0; return 0; }
            put_err(who, out, outlen);
            return 1;
        }
        catch (...) { if (out && outlen > 0) out[0] = 0; return 0; }
    }

    // WHAT: owner releases ship. 1 = released or was free, 0 = refused (another owner).
    SHIP_CONTROL_API int SC_release(const wchar_t* owner, int ship, wchar_t* err, int errlen)
    {
        return run_export([&](std::string& why) {
            std::string who;
            if (!owner_arg(owner, who, why)) return false;
            return g_owners.release(ship, who, why);
        }, err, errlen);
    }

    // WHAT: owner releases every ship it holds. >= 0 = count released, -1 = failed.
    SHIP_CONTROL_API int SC_release_all(const wchar_t* owner, wchar_t* err, int errlen)
    {
        try
        {
            std::string why, who;
            if (!owner_arg(owner, who, why)) { put_err(why, err, errlen); return -1; }
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load()) { put_err("ShipControl killed", err, errlen); return -1; }
            return g_owners.release_all(who);
        }
        catch (const std::exception& e) { put_err(std::string("exception: ") + e.what(), err, errlen); }
        catch (...) { put_err("exception: unknown", err, errlen); }
        return -1;
    }
}
