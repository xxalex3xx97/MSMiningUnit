// FILE: mods/ShipControl/dllmain.cpp
// WHAT: UE4SS C++ mod entry point for ShipControl. Holds the ship owner registry and exports the C ABI in sc_api.h. SC0: owner registry only, no engine hook.
// WHY separate: UE4SS needs start_mod and uninstall_mod in the DLL entry file.
// DEPENDS ON (exact, include order matters): sc_api.h (types only, see below), sc_version.hpp, sc_common.hpp, sc_owner.hpp, sc_speed.hpp (includes sc_mscore.hpp, sc_math.hpp)
// USED BY: the UE4SS loader (mods.txt line "ShipControl : 1" or enabled.txt), which calls start_mod and uninstall_mod. Other mods load this DLL by LoadLibrary and GetProcAddress (sc_api.h).
// GAME FACTS: none. Every export catches exceptions and returns a failure code; a failure never reaches the caller as a throw.
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
// SC_API_NO_PROTOTYPES: this file defines the exports itself, so only the types of sc_api.h are used here.
#define SC_API_NO_PROTOTYPES
#include "sc_api.h"
#include "sc_version.hpp"
#include "sc_common.hpp"
#include "sc_owner.hpp"
#include "sc_speed.hpp"

namespace
{
    std::mutex g_mx;                 // guards g_owners (exports may run on any thread)
    ScOwners g_owners;
    std::atomic<bool> g_killed{false};   // kill switch: refuses claim/release while true
    ScHost g_host = {0, nullptr, nullptr};   // host callbacks (SC_set_host); guarded by g_mx

    // WHAT: writes one line to the host log (mining's tlog), or to the UE4SS log when no host is set.
    // WHY: speed and watch messages reach the mod that owns the ship.
    void host_say(const ScHost& h, const std::wstring& s)
    {
        if (h.log) { try { h.log(s.c_str()); } catch (...) {} return; }
        log_line(s);
    }

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
        try { return 2; }
        catch (...) { return 0; }
    }

    // WHAT: capability bits. bit 0 = owner registry. bit 1 = speed monitor (MSCore API >= 10 bound now).
    SHIP_CONTROL_API int SC_caps()
    {
        try
        {
            std::lock_guard<std::mutex> lk(g_mx);
            std::wstring why;
            return 1 | (ms_load(why) ? 2 : 0);
        }
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

    // WHAT: installs the host callbacks. size must equal sizeof(ScHost). 1 = ok, 0 = refused.
    // WHY: mining gets its log and its abort callback here; ShipControl never calls game code for them.
    SHIP_CONTROL_API int SC_set_host(const ScHost* host)
    {
        try
        {
            if (!host || host->size != static_cast<int32_t>(sizeof(ScHost))) return 0;
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load()) return 0;
            g_host = *host;
            return 1;
        }
        catch (...) { return 0; }
    }

    // WHAT: installs the tuning (speed_k). size must equal sizeof(ScTune). 1 = ok, 0 = refused.
    SHIP_CONTROL_API int SC_set_tune(const ScTune* tune)
    {
        try
        {
            if (!tune || tune->size != static_cast<int32_t>(sizeof(ScTune))) return 0;
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load()) return 0;
            g_spd.speed_k = tune->speed_k;
            return 1;
        }
        catch (...) { return 0; }
    }

    // WHAT: owner watches (on=1) or stops watching (on=0) the speed of ship. Refused when another owner holds the ship,
    //       or (on=1) when MSCore is too old. 1 = ok, 0 = refused.
    SHIP_CONTROL_API int SC_watch(const char* owner, int sid, int on)
    {
        try
        {
            if (!owner || !*owner) return 0;
            const std::string who = owner;
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load()) return 0;
            std::string why;
            if (!g_owners.may_act(sid, who, why)) { host_say(g_host, L"SC_watch refused ship=" + std::to_wstring(sid) + L": " + widen(why)); return 0; }
            if (!on) { g_spd.watch.erase(sid); g_spd.mon.erase(sid); return 1; }
            std::wstring e;
            if (!ms_load(e)) { host_say(g_host, L"SC_watch refused ship=" + std::to_wstring(sid) + L": " + e); return 0; }
            g_spd.watch[sid] = who;
            return 1;
        }
        catch (...) { return 0; }
    }

    // WHAT: one speed poll step for every watched ship (dt in s). Over-limit ships are zeroed; each first abort calls on_speed_abort once.
    //       >= 0 = fresh aborts this step, -1 = MSCore not usable or killed (nothing zeroed).
    SHIP_CONTROL_API int SC_tick_speed(double dt)
    {
        std::vector<SpeedAbort> fresh;
        int n = -1;
        ScHost h = {0, nullptr, nullptr};
        try
        {
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load()) return -1;
            n = speed_tick(dt, fresh);
            h = g_host;
        }
        catch (...) { return -1; }
        // callbacks run without the registry lock: the host may call back into SC_zero / SC_release.
        for (const SpeedAbort& a : fresh)
        {
            host_say(h, L"ABORT speed ship=" + std::to_wstring(a.sid) + L" v/lim=" + std::to_wstring(a.ratio));
            if (h.on_speed_abort) { try { h.on_speed_abort(a.sid, a.ratio); } catch (...) {} }
        }
        return n;
    }

    // WHAT: speed reading of a watched ship into out (size must equal sizeof(ScSpeed)). 1 = ok, 0 = not watched or refused.
    SHIP_CONTROL_API int SC_speed(int sid, ScSpeed* out)
    {
        try
        {
            if (!out || out->size != static_cast<int32_t>(sizeof(ScSpeed))) return 0;
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load() || !g_spd.watch.count(sid)) return 0;
            auto m = g_spd.mon.find(sid);
            if (m == g_spd.mon.end()) return 0;
            out->have = m->second.have ? 1 : 0;
            out->v_raw = m->second.v;
            out->lim_raw = m->second.lim;
            out->aborted = m->second.aborted ? 1 : 0;
            return 1;
        }
        catch (...) { return 0; }
    }

    // WHAT: zeroes Throttle and rotation of ship for owner (MSCore_ship_zero). Refused when another owner holds the ship. 1 = zeroed, 0 = refused or failed.
    // WHY: the owner check keeps one behaviour from stopping a ship another behaviour is flying.
    SHIP_CONTROL_API int SC_zero(const char* owner, int sid)
    {
        try
        {
            if (!owner || !*owner) return 0;
            const std::string who = owner;
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_killed.load()) return 0;
            std::string why;
            if (!g_owners.may_act(sid, who, why)) { host_say(g_host, L"SC_zero refused ship=" + std::to_wstring(sid) + L": " + widen(why)); return 0; }
            std::wstring e;
            if (!ms_load(e) || !ms_zero(sid, e)) { host_say(g_host, L"SC_zero failed ship=" + std::to_wstring(sid) + L": " + e); return 0; }
            return 1;
        }
        catch (...) { return 0; }
    }
}
