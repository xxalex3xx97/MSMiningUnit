// FILE: mods/MiningBenchMod/mb_pin.hpp
// WHAT: the last map pin, read from MSCore (pinned_now). pin_file_tick writes MiningPinned.txt (the id, 0 = none) from the command poll when the value changed.
//       pin_hook_ensure arms the MSCore map-pin hook (MSCore_pin_ensure) and is called until it works.
// WHY separate: the warp command (warp_sector:pinned) reads the pin. The hook itself lives in MSCore now (one copy for all mods).
// DEPENDS ON: mb_common.hpp (write_atomic, get_dll_dir), mb_mscore.hpp (ms_load, g_ms).
// USED BY: mb_warp.hpp, mb_warpreal.hpp (pinned_now), mb_testcmd.hpp (pin_hook_ensure and pin_file_tick from cmd_tick).
// GAME FACTS (probe 2026-10-29): map pin = AddInputCommand(receiver 2325, MapPinInputCommand{ObjectId = sector id, bShow, bHide, bDestroy}).
#pragma once

namespace
{
    int g_pinned_written = -1;     // value last written to MiningPinned.txt (-1 = never)

    // WHAT: the last map-pinned sector id from MSCore (0 = none, or MSCore not loaded).
    int pinned_now()
    {
        return g_ms.h ? g_ms.pinned() : 0;
    }

    // WHAT: arms the MSCore map-pin hook. Call every tick until it stays armed (MSCore returns 1 when armed).
    // WHY: the controller does not exist at mod load; MSCore retries inside pin_ensure.
    void pin_hook_ensure()
    {
        std::wstring e;
        if (ms_load(e)) g_ms.pin_ensure();
    }

    // WHAT: nothing to release: the AddInputCommand hook lives in MSCore, which owns its unload.
    void pin_hook_release() {}

    // WHAT: writes MiningPinned.txt when the pinned sector changed (called from the command poll).
    void pin_file_tick()
    {
        const int id = pinned_now();
        if (id == g_pinned_written) return;
        g_pinned_written = id;
        try { write_atomic(get_dll_dir() / "MiningPinned.txt", std::to_string(id) + "\n"); }
        catch (...) {}
    }
}
