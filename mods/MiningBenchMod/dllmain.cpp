// FILE: mods/MiningBenchMod/dllmain.cpp
// WHAT: UE4SS C++ mod entry point. Includes the mb_*.hpp parts in order, hooks the engine tick, runs the ship scan every log_interval_seconds, and exports start_mod / uninstall_mod.
// WHY separate: UE4SS needs start_mod and uninstall_mod in the DLL entry file; everything else lives in headers so it can be read in pieces.
// DEPENDS ON (exact, include order matters): mb_version.hpp, mb_common.hpp, mb_pattern.hpp, mb_compat.hpp, mb_config.hpp, mb_ue.hpp, mb_mscore.hpp, mb_math.hpp, mb_docks.hpp, mb_ships.hpp, mb_weapons.hpp, mb_orders.hpp, mb_filters.hpp, mb_cmdstate.hpp, mb_rocks.hpp, mb_steer.hpp, mb_dockjob.hpp, mb_pin.hpp, mb_warp.hpp, mb_warpreal.hpp, mb_belt.hpp, mb_mining.hpp, mb_natives.hpp, mb_chunkhold.hpp, mb_devcmd.hpp, mb_testcmd.hpp, mb_compat_run.hpp
// USED BY: the UE4SS loader (mods.txt line "MiningBenchMod : 1"), which calls start_mod and uninstall_mod.
// GAME FACTS: the engine tick hook (UE4SS RegisterEngineTickPostCallback) fires every frame with dt in seconds. Crash history (brief Update 21): a Lua reload (Ctrl+R) with this DLL loaded crashed inside the tick hook, so restart the game fully after any change.
#include "mb_version.hpp"
#include "mb_common.hpp"
#include "mb_pattern.hpp"
#include "mb_compat.hpp"
#include "mb_config.hpp"
#include "mb_ue.hpp"
#include "mb_mscore.hpp"
#include "mb_math.hpp"
#include "mb_docks.hpp"
#include "mb_ships.hpp"
#include "mb_weapons.hpp"
#include "mb_orders.hpp"
#include "mb_filters.hpp"
#include "mb_cmdstate.hpp"
#include "mb_rocks.hpp"
#include "mb_steer.hpp"
#include "mb_net.hpp"
#include "mb_dockjob.hpp"
#include "mb_pin.hpp"
#include "mb_warp.hpp"
#include "mb_warpreal.hpp"
#include "mb_belt.hpp"
#include "mb_mining.hpp"
#include "mb_natives.hpp"
#include "mb_chunkhold.hpp"
#include "mb_devcmd.hpp"
#include "mb_testcmd.hpp"
#include "mb_compat_run.hpp"

namespace
{
    // WHAT: one scan step, run from the tick hook every log_interval_seconds: config check, bench ship scan, lists, and skip records for disabled ships.
    // WHY: the heavy game queries (FindAllOf and friends) run here, not on every frame.
    void tick_work()
    {
        check_config();
        // WHY: check_config runs before this gate, so the mod can be re-enabled by editing the INI while it is disabled.
        if (!g_cfg.enabled) return;
        std::vector<std::pair<UObject*, int>> bench;
        std::vector<int> dead;
        scan_and_log(bench, dead);
        mine_set_bench(bench, dead);
        write_lists();
        if (bench.empty()) return;
        for (auto& [ship, id] : bench)
        {
            (void)ship;
            if (id <= 0) continue;
            if (eff_cfg(id).enabled != 1) skip(id, STR("ship disabled (effective enabled != 1)"));
        }
    }

    // WHAT: the command tick as a plain function for compat_run: the self-test while it is pending, then cmd_tick. false = an exception (logged here).
    // WHY: compat_run needs a plain function (SEH frame, no C++ objects); the C++ catches stay here.
    bool thunk_cmd(double dt)
    {
        try { if (!g_compat.done) compat_selftest(); cmd_tick(dt); return true; }
        catch (const std::exception& e) { Output::send<LogLevel::Normal>(STR("[MiningBenchMod] EXCEPTION cmd {}\n"), widen(e.what())); }
        catch (...) { Output::send<LogLevel::Normal>(STR("[MiningBenchMod] EXCEPTION cmd unknown\n")); }
        return false;
    }

    // WHAT: the scan tick as a plain function for compat_run (tick_work). false = an exception (logged here). dt is unused.
    bool thunk_scan(double)
    {
        try { tick_work(); return true; }
        catch (const std::exception& e) { Output::send<LogLevel::Normal>(STR("[MiningBenchMod] EXCEPTION {}\n"), widen(e.what())); }
        catch (...) { Output::send<LogLevel::Normal>(STR("[MiningBenchMod] EXCEPTION unknown\n")); }
        return false;
    }
}

class MiningBenchMod : public RC::CppUserModBase
{
public:
    // WHAT: fills in the UE4SS mod metadata and logs the version.
    // WHY: UE4SS shows the name and version, and the load line in UE4SS.log proves the DLL loaded.
    MiningBenchMod() : CppUserModBase()
    {
        ModName = STR("MiningBenchMod");
        ModVersion = widen(kVersion);
        ModDescription = STR("Ship mining module (dev tool: Test tab)");
        ModAuthors = STR("local");
        Output::send<LogLevel::Normal>(STR("[MiningBenchMod] loaded (MiningBenchMod M3 mining loop)\n"));
        log_line(widen(std::string("MiningBenchMod v") + kVersion));
        std::wstring ms_err;
        ms_load(ms_err);   // logs MSCORE loaded / load failed once; each call site retries if it failed
    }

    // WHAT: sets g_shutdown and unregisters the tick callback.
    // WHY: UE4SS can unload the mod while the engine keeps ticking; the callback must not run after this object is gone.
    ~MiningBenchMod() override
    {
        g_shutdown = true;
        if (g_tick_cb) { Hook::UnregisterCallback(g_tick_cb); g_tick_cb = 0; }
        g_hook_registered = false;
        try { pin_hook_release(); } catch (...) {}
        try { rock_listener_release(); } catch (...) {}
        try { chunk_hold_release_all(); } catch (...) {}   // no chunk hold outlives the mod (failures are logged in mb_chunkhold.hpp)
    }

    // WHAT: runs once Unreal is up. Reloads the config, sets up the Test-tab command bus (cmd_init), and registers the engine tick hook once.
    // WHY: game objects and hooks do not exist before Unreal init. The hook is always registered and tick_work checks cfg.enabled, so the INI can switch the mod off without unloading it.
    auto on_unreal_init() -> void override
    {
        reload_config(true);
        cmd_init();
        if (g_hook_registered) return;
        g_hook_registered = true;   // hook always registered; tick_work honours cfg.enabled (hot-reloaded)
        Hook::FCallbackOptions opts;
        opts.OwnerModName = STR("MiningBenchMod");
        opts.HookName = STR("MiningBenchScan");
        // WHAT: per-frame callback. Runs cheap work every frame (Test-tab command poll, trace timer) and the heavy scan only every log_interval_seconds.
        // WHY: exceptions are caught and logged, so one bad frame cannot take the engine tick down.
        g_tick_cb = Hook::RegisterEngineTickPostCallback(
                [](Hook::TCallbackIterationData<void>&, UEngine*, float dt, bool) {
                    if (g_shutdown.load()) return;
                    // WHY: SAFE MODE (repeated faults) and kill_switch=1 both leave the game untouched; checked every frame.
                    if (g_safe.load() || compat_killed()) return;
                    g_now += dt; g_accum += dt;
                    compat_run("cmd_tick", thunk_cmd, static_cast<double>(dt), g_sm_cmd);
                    // WHY: g_accum counts seconds since the last scan; the scan waits until log_interval_seconds have passed.
                    if (g_accum < static_cast<float>(g_cfg.log_interval_seconds)) return;
                    g_accum = 0.0f;
                    if (g_safe.load()) return;   // the command step may have just entered SAFE MODE
                    compat_run("tick_work", thunk_scan, 0.0, g_sm_scan);
                },
                opts);
    }
};

// Export marker: makes start_mod and uninstall_mod visible to the UE4SS loader by name.
#define MINING_BENCH_MOD_API __declspec(dllexport)
extern "C"
{
    // WHAT: UE4SS entry point; creates the mod object and returns it.
    // WHY: UE4SS loads a C++ mod by calling this exported C symbol (extern "C" keeps the name unmangled).
    MINING_BENCH_MOD_API RC::CppUserModBase* start_mod()
    {
        return new MiningBenchMod();
    }

    // WHAT: UE4SS unload entry point; deletes the mod object.
    // WHY: the destructor runs first and stops the tick hook.
    MINING_BENCH_MOD_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
