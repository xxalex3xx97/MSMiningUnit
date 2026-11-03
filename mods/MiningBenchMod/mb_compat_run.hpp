// FILE: mods/MiningBenchMod/mb_compat_run.hpp
// WHAT: game-update compatibility at run time (package E). compat_selftest() runs once per session when the player's controller exists:
//       build info (informational), the contract table (every game name the mod uses: class, property + type, function, parameter, command field),
//       the command type id check (+0x28), and the native pattern lookup. Results go to g_compat (per feature) and MiningCompat.txt.
//       compat_run() runs one tick step under SEH and counts faults (SafeMode). Five faults in a row -> SAFE MODE: ships stopped, chunk holds
//       released, the mod idle until restart. compat_killed() = kill_switch=1 in MiningBench.ini: the mod does nothing until it is set back to 0.
// WHY: a game patch can rename or retype what the mod reads, or move the natives. The check turns that into a DISABLED feature line in the log
//      instead of a crash or a wrong write. Limits: a same-name, same-type change in behaviour is not detected; the natives are confirmed only by
//      the chunk-hold canary (mb_chunkhold.hpp).
// DEPENDS ON: mb_common.hpp (log_line, alive, STR, widen, u8, get_dll_dir, write_atomic), mb_compat.hpp (g_compat, SafeMode, feature_name),
//             mb_mscore.hpp (ms_load, g_ms: the chunk-hold natives are MSCore's), mb_version.hpp (kKnownBuildSize), mb_warpreal.hpp (kCmdTypeOff,
//             kThrottleTypeId), mb_ue.hpp (first_of, get_obj_prop), mb_mining.hpp (g_bench_ids, mine_halt),
//             mb_chunkhold.hpp (chunk_hold_release_all), mb_config.hpp (check_config, g_cfg.kill_switch).
// USED BY: dllmain.cpp (tick lambda: compat_killed, compat_run, compat_selftest through the thunks).
// GAME FACTS: the contract table names come from tools/compat_names.py (run over the mod headers). Names read through a computed path, names
//             only seen in dev/test code, and BP classes are not in the table (see the report of the slice).
// Log keyword: 'COMPAT'. Files: MiningCompat.txt (dll dir): the self-test text, first line "SAFE MODE" after safe mode.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{
    // WHAT: "0x<hex>" of a value (build info and fault codes).
    std::wstring nat_hex(uint64_t v) { wchar_t b[32]; swprintf(b, 32, L"0x%llx", static_cast<unsigned long long>(v)); return b; }

    std::atomic<bool> g_safe{false};    // SAFE MODE: no more ticks of the mod until restart
    SafeMode g_sm_cmd;                  // fault counter of the command tick (cmd_tick)
    SafeMode g_sm_scan;                 // fault counter of the scan tick (tick_work)
    std::string g_compat_text;          // text of the last self-test (written again with SAFE MODE on top)

    // ---- contract table ----

    // WHAT: kind of a contract row. CLASS = class exists. PROP = property of a class (instance field). SFIELD = field of a command class (set via make_cmd / NewObject).
    //       FUNC = function on the class default object. FPARAM = parameter of FUNC (sub = parameter name).
    enum RowKind { RK_CLASS, RK_PROP, RK_FUNC, RK_FPARAM, RK_SFIELD };

    // WHAT: one contract row. owner = class name without /Script/MainSequence. member = property or function name. sub = parameter name (FPARAM).
    //       type = expected FProperty class ("IntProperty", ...). typed = check the type (only when the accessor in the code type-checks it);
    //       otherwise presence only.
    struct CRow
    {
        Feature f;
        RowKind k;
        const char* owner;
        const char* member;
        const char* sub;
        const char* type;
        bool typed;
    };

    const CRow kRows[] = {
        // CORE
        { F_CORE, RK_PROP, "DMSPlayerController", "PlayerObject", "", "ObjectProperty", true },
        { F_CORE, RK_PROP, "DMSPlayerController", "PlayerCachedChunkLocation", "", "StructProperty", true },
        { F_CORE, RK_FUNC, "DMSPlayerController", "AddInputCommand", "", "", false },
        { F_CORE, RK_FPARAM, "DMSPlayerController", "AddInputCommand", "InReceiverID", "IntProperty", false },
        { F_CORE, RK_FPARAM, "DMSPlayerController", "AddInputCommand", "InCommand", "ObjectProperty", false },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "ID", "", "IntProperty", false },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "PilotID", "", "IntProperty", true },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "Dock", "", "ObjectProperty", true },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "DockID", "", "IntProperty", true },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "Health", "", "IntProperty", true },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "Inventory", "", "ObjectProperty", true },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "InventorySize", "", "IntProperty", true },
        { F_CORE, RK_PROP, "DMSPlayerShipObject", "CurrentOrderIndex", "", "IntProperty", true },
        { F_CORE, RK_PROP, "SolarSystem", "ID", "", "IntProperty", false },
        { F_CORE, RK_PROP, "SolarSystem", "ChunkMap", "", "MapProperty", true },
        { F_CORE, RK_CLASS, "Dock", "", "", "", false },
        { F_CORE, RK_CLASS, "Item", "", "", "", false },
        // STEER
        { F_STEER, RK_PROP, "DMSPlayerShipObject", "Throttle", "", "IntProperty", true },
        { F_STEER, RK_FUNC, "DMSPlayerShipObject", "GetThrottleSteps", "", "", false },
        { F_STEER, RK_FUNC, "DMSPlayerShipObject", "GetMaxSpeed", "", "", false },
        { F_STEER, RK_FUNC, "DMSPlayerShipObject", "GetModifiedMaxSpeed", "", "", false },
        { F_STEER, RK_CLASS, "ViewInput", "", "", "", false },
        { F_STEER, RK_FUNC, "ViewInput", "CreateInputView", "", "", false },
        { F_STEER, RK_CLASS, "PilotConsoleObject", "", "", "", false },
        { F_STEER, RK_PROP, "DMSPlayerController", "InputView", "", "StructProperty", true },
        { F_STEER, RK_PROP, "DMSPlayerShipObject", "RotationImpulseInput", "", "StructProperty", false },
        // WEAPONS
        { F_WEAPONS, RK_CLASS, "ShipFiringInput", "", "", "", false },
        { F_WEAPONS, RK_SFIELD, "ShipFiringInput", "bPrimary", "", "BoolProperty", false },
        { F_WEAPONS, RK_SFIELD, "ShipFiringInput", "bSecondary", "", "BoolProperty", false },
        { F_WEAPONS, RK_FUNC, "ShipFiringInput", "CreateInputShipFiring", "", "", false },
        { F_WEAPONS, RK_PROP, "DMSPlayerShipObject", "Weapons", "", "ArrayProperty", true },
        // DOCK
        { F_DOCK, RK_PROP, "DMSPlayerShipObject", "bAutopilot", "", "BoolProperty", true },
        { F_DOCK, RK_PROP, "DMSPlayerShipObject", "AutopilotExitDockTimer", "", "IntProperty", true },
        { F_DOCK, RK_FUNC, "DMSPlayerController", "SetShipAutopilot", "", "", false },
        { F_DOCK, RK_FUNC, "DMSPlayerController", "DeleteOrder", "", "", false },
        { F_DOCK, RK_FUNC, "DMSPlayerController", "SkipToOrder", "", "", false },
        { F_DOCK, RK_PROP, "DMSPlayerShipObject", "Orders", "", "ArrayProperty", true },
        { F_DOCK, RK_PROP, "DMSPlayerShipObject", "LogisticFilters", "", "ArrayProperty", true },
        { F_DOCK, RK_CLASS, "NewShipOrderInput", "", "", "", false },
        { F_DOCK, RK_CLASS, "UpdateShipOrderInput", "", "", "", false },
        { F_DOCK, RK_CLASS, "ConfigureShipOrdersInput", "", "", "", false },
        { F_DOCK, RK_CLASS, "LogisticFilterInput", "", "", "", false },
        { F_DOCK, RK_FUNC, "NewShipOrderInput", "CreateInputNewShipOrder", "", "", false },
        { F_DOCK, RK_FUNC, "UpdateShipOrderInput", "CreateInputUpdateShipOrder", "", "", false },
        { F_DOCK, RK_FUNC, "LogisticFilterInput", "CreateInputLogisticFilter", "", "", false },
        // WARP
        { F_WARP, RK_CLASS, "SolarSystemSector", "", "", "", false },
        { F_WARP, RK_CLASS, "SetPilotingInput", "", "", "", false },
        { F_WARP, RK_CLASS, "ShipTeleportInput", "", "", "", false },
        { F_WARP, RK_CLASS, "MapPinInputCommand", "", "", "", false },
        { F_WARP, RK_SFIELD, "MapPinInputCommand", "ObjectId", "", "IntProperty", true },
        { F_WARP, RK_SFIELD, "MapPinInputCommand", "bShow", "", "BoolProperty", true },
        { F_WARP, RK_SFIELD, "ShipTeleportInput", "bSetTarget", "", "BoolProperty", true },
        { F_WARP, RK_PROP, "SolarSystemSector", "ID", "", "IntProperty", false },
        { F_WARP, RK_PROP, "SolarSystemSector", "ParentID", "", "IntProperty", true },
        { F_WARP, RK_PROP, "SolarSystemSector", "ChunkLocation", "", "StructProperty", true },
        { F_WARP, RK_PROP, "SolarSystemSector", "HalfExtents", "", "StructProperty", true },
        { F_WARP, RK_PROP, "SolarSystemSector", "GenParams", "", "ObjectProperty", true },
        { F_WARP, RK_FUNC, "SolarSystemSector", "GetDisplayName", "", "", false },
        { F_WARP, RK_FUNC, "SolarSystemSector", "GetSolarSystem", "", "", false },
        { F_WARP, RK_FUNC, "DMSPlayerShipObject", "GetAvailableJumpFuel", "", "", false },
        { F_WARP, RK_FUNC, "DMSPlayerShipObject", "GetJumpArrivalLocation", "", "", false },
        { F_WARP, RK_FUNC, "DMSPlayerShipObject", "GetJumpCost", "", "", false },
        // WARPREAL
        { F_WARPREAL, RK_CLASS, "ShipMovementInputCommand", "", "", "", false },
        { F_WARPREAL, RK_SFIELD, "ShipMovementInputCommand", "DeltaThrottle", "", "IntProperty", true },
        { F_WARPREAL, RK_PROP, "DMSPlayerShipObject", "bJumpTargetSet", "", "BoolProperty", true },
        { F_WARPREAL, RK_PROP, "DMSPlayerShipObject", "bAlignedWithJumpTarget", "", "BoolProperty", true },
        { F_WARPREAL, RK_PROP, "DMSPlayerShipObject", "JumpTargetLocation", "", "StructProperty", true },
        { F_WARPREAL, RK_PROP, "DMSPlayerShipObject", "JumpTargetChunkLocation", "", "StructProperty", true },
        { F_WARPREAL, RK_PROP, "DMSPlayerShipObject", "JumpProgress", "", "IntProperty", true },
        { F_WARPREAL, RK_PROP, "DMSPlayerShipObject", "JumpAnimProgress", "", "IntProperty", true },
        { F_WARPREAL, RK_PROP, "DMSPlayerShipObject", "ChargeToJump", "", "IntProperty", true },
        // CHUNKHOLD
        { F_CHUNKHOLD, RK_PROP, "DMSPlayerController", "PlayerObject", "", "ObjectProperty", true },
        { F_CHUNKHOLD, RK_PROP, "SolarSystem", "ChunkMap", "", "MapProperty", true },
        // MINING
        { F_MINING, RK_PROP, "DMSPlayerController", "ScannableHUDMarkers", "", "ArrayProperty", true },
    };

    // ---- checks ----

    // WHAT: UClass of a contract owner: /Script/MainSequence.<name>, else the class of the first instance. Cached per self-test.
    UClass* row_class(const std::string& owner, std::map<std::string, UClass*>& cache)
    {
        auto hit = cache.find(owner);
        if (hit != cache.end()) return hit->second;
        const std::wstring name = widen(owner);
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (std::wstring(STR("/Script/MainSequence.")) + name).c_str());
        if (!c)
        {
            UObject* o = first_of(name.c_str());
            c = alive(o) ? o->GetClassPrivate() : nullptr;
        }
        cache[owner] = c;
        return c;
    }

    // WHAT: the property name in c or its super classes (own properties per UStruct, FindProperty does not walk the chain). null when missing.
    FProperty* row_prop(UStruct* c, const std::wstring& name)
    {
        for (UStruct* s = c; s; s = s->GetSuperStruct())
            if (FProperty* p = s->FindProperty(FName(name.c_str(), FNAME_Add))) return p;
        return nullptr;
    }

    // WHAT: true when p is the FProperty class named t (IntProperty, BoolProperty, ObjectProperty, StructProperty, MapProperty, ArrayProperty).
    bool prop_type_is(FProperty* p, const std::string& t)
    {
        if (t == "IntProperty") return CastField<FIntProperty>(p) != nullptr;
        if (t == "BoolProperty") return CastField<FBoolProperty>(p) != nullptr;
        if (t == "ObjectProperty") return CastField<FObjectProperty>(p) != nullptr;
        if (t == "StructProperty") return CastField<FStructProperty>(p) != nullptr;
        if (t == "MapProperty") return CastField<FMapProperty>(p) != nullptr;
        if (t == "ArrayProperty") return CastField<FArrayProperty>(p) != nullptr;
        return false;
    }

    // WHAT: checks one contract row. false + why when the class, member, parameter or (typed) type is missing.
    bool check_row(const CRow& r, std::map<std::string, UClass*>& cache, std::string& why)
    {
        UClass* c = row_class(r.owner, cache);
        if (!c) { why = "class not found"; return false; }
        if (r.k == RK_CLASS) return true;
        if (r.k == RK_PROP || r.k == RK_SFIELD)
        {
            FProperty* p = row_prop(c, widen(r.member));
            if (!p)
            {
                // a Blueprint subclass can own the property: try the class of a live instance
                UObject* inst = first_of(widen(r.owner).c_str());
                if (alive(inst)) p = row_prop(inst->GetClassPrivate(), widen(r.member));
            }
            if (!p) { why = "property missing"; return false; }
            if (r.typed && !prop_type_is(p, r.type)) { why = std::string("not ") + r.type; return false; }
            return true;
        }
        UObject* cdo = c->GetClassDefaultObject();
        UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(widen(r.member).c_str()) : nullptr;
        if (!fn) { why = "function missing"; return false; }
        if (r.k == RK_FPARAM)
        {
            FProperty* p = fn->FindProperty(FName(widen(r.sub).c_str(), FNAME_Add));
            if (!p) { why = "parameter missing"; return false; }
            if (r.typed && !prop_type_is(p, r.type)) { why = std::string("not ") + r.type; return false; }
        }
        return true;
    }

    // WHAT: the contract's map key check: SolarSystem.ChunkMap key element size must be 12 bytes (chunk key, MSCore msc_chunks.hpp). ok false + why otherwise.
    bool chunkmap_key_ok(UClass* solar, std::string& why)
    {
        FProperty* p = solar ? row_prop(solar, STR("ChunkMap")) : nullptr;
        auto* mp = p ? CastField<FMapProperty>(p) : nullptr;
        if (!mp || !mp->GetKeyProp()) { why = "ChunkMap is not a map"; return false; }
        if (mp->GetKeyProp()->GetElementSize() != 12) { why = "key size " + std::to_string(mp->GetKeyProp()->GetElementSize()) + " != 12"; return false; }
        return true;
    }

    // WHAT: the command type id check (WARPREAL). Every command class the code sends has its type id (int32 at cmd+kCmdTypeOff) in 1..0x400,
    //       all distinct and different from kThrottleTypeId; the UObject base is kCmdTypeOff bytes, and no own property of the command classes
    //       starts before kCmdTypeOff+4. Creates three command objects (factory made, not sent). false + why on a mismatch.
    bool compat_tid_check(std::wstring& why)
    {
        why.clear();   // input commands are built and sent by MSCore now; this DLL no longer creates command objects, nothing to check
        return true;
    }

    // WHAT: build info for the log (file size, PE timestamp, SizeOfImage, checksum). Informational: a different size only adds a line.
    void compat_build_info(const std::function<void(const std::wstring&)>& out)
    {
        wchar_t path[1024] = {0};
        GetModuleFileNameW(nullptr, path, 1024);
        std::error_code ec;
        const uintmax_t size = std::filesystem::file_size(path, ec);
        const uint8_t* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) { out(STR("COMPAT build info unavailable: no module base")); return; }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        out(STR("COMPAT build size=") + std::to_wstring(size) + STR(" timestamp=") + nat_hex(nt->FileHeader.TimeDateStamp) +
            STR(" sizeofimage=") + std::to_wstring(nt->OptionalHeader.SizeOfImage) + STR(" checksum=") + nat_hex(nt->OptionalHeader.CheckSum));
        if (ec || size != kKnownBuildSize)
            out(STR("COMPAT build differs from the tested build (size ") + std::to_wstring(kKnownBuildSize) + STR("): full self-test runs (info only)"));
    }

    // WHAT: the self-test. Needs the player's controller and PlayerObject (else false; retried every 2 s). Runs every row, the type id check and the
    //       natives once, then sets g_compat.done. Logs one line per disabled feature and the summary; the same text goes to MiningCompat.txt.
    // WHY: the table is checked once per session, not every tick (the lookups walk every UObject).
    bool compat_selftest()
    {
        static ULONGLONG next_try = 0;
        if (g_compat.done) return true;
        const ULONGLONG now = GetTickCount64();
        if (now < next_try) return false;
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UObject* pobj = alive(ctrl) ? get_obj_prop(ctrl, STR("PlayerObject")) : nullptr;
        if (!alive(pobj)) { next_try = now + 2000; return false; }

        std::wstring txt;
        const auto out = [&txt](const std::wstring& s) { log_line(s); txt += s + STR("\n"); };
        compat_build_info(out);

        std::map<std::string, UClass*> cache;
        for (const CRow& r : kRows)
        {
            std::string why;
            const bool ok = check_row(r, cache, why);
            std::string what = r.owner;
            if (r.k != RK_CLASS) what += std::string(".") + r.member;
            if (r.k == RK_FPARAM) what += std::string(".") + r.sub;
            g_compat.add(r.f, what, ok, why);
        }
        {
            std::string why;
            const bool ok = chunkmap_key_ok(row_class("SolarSystem", cache), why);
            g_compat.add(F_CHUNKHOLD, "SolarSystem.ChunkMap key size", ok, why);
        }
        {
            std::wstring why;
            const bool ok = compat_tid_check(why);
            g_compat.add(F_WARPREAL, "command type id @0x28", ok, u8(why));
        }
        {
            std::wstring e;
            wchar_t w[256] = {0};
            const bool ok = ms_load(e) && g_ms.chunk_ready(w, 256) != 0;
            g_compat.add(F_CHUNKHOLD, "natives (MSCore)", ok, ok ? std::string() : u8(e.empty() ? std::wstring(w) : e));
        }

        for (const CompatRow& r : g_compat.rows)
            if (!r.ok) out(STR("COMPAT ") + widen(feature_name(r.f)) + STR(" disabled: ") + widen(r.what) + STR(": ") + widen(r.why));
        out(STR("COMPAT summary:\n") + widen(g_compat.summary()));
        g_compat.done = true;
        g_compat_text = u8(txt);
        try { write_atomic(get_dll_dir() / "MiningCompat.txt", g_compat_text); } catch (...) {}
        return true;
    }

    // ---- guard ----

    // WHAT: runs fn(dt) under SEH. false when fn returns false or an SEH exception hits (*code = the exception code, else 0 is left).
    // WHY: POD arguments only: MSVC forbids __try in a function with C++ objects. C++ exceptions inside fn are caught by fn itself.
    bool compat_seh(bool (*fn)(double), double dt, unsigned long* code)
    {
        bool r = false;
        __try { r = fn(dt); }
        __except (EXCEPTION_EXECUTE_HANDLER) { *code = GetExceptionCode(); return false; }
        return r;
    }

    // WHAT: mine_halt(sid) under SEH. false when it faults.
    bool compat_seh_halt(int sid, unsigned long* code)
    {
        __try { mine_halt(sid); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { *code = GetExceptionCode(); return false; }
    }

    // WHAT: SAFE MODE: ships stopped (mine_halt each bench ship), every chunk hold released, the file gets SAFE MODE on top. Until restart.
    void compat_safe_enter(const char* name)
    {
        g_safe = true;
        log_line(STR("COMPAT SAFE MODE: repeated faults in ") + widen(name) + STR(". Ships stopped, chunk holds released, mod idle until restart."));
        for (int sid : g_bench_ids)
        {
            unsigned long code = 0;
            if (!compat_seh_halt(sid, &code)) log_line(STR("COMPAT safe mode: stop of ship ") + std::to_wstring(sid) + STR(" faulted code=") + nat_hex(code));
        }
        try { chunk_hold_release_all(); } catch (...) {}
        try { write_atomic(get_dll_dir() / "MiningCompat.txt", std::string("SAFE MODE\n") + g_compat_text); } catch (...) {}
    }

    // WHAT: one tick step under SEH and the fault counter. Ok resets the streak. A fault is logged with its code; the limit trips safe mode.
    void compat_run(const char* name, bool (*fn)(double), double dt, SafeMode& sm)
    {
        unsigned long code = 0;
        if (compat_seh(fn, dt, &code)) { sm.ok(); return; }
        log_line(STR("COMPAT fault in ") + widen(name) + STR(" code=") + nat_hex(code) + (code == 0 ? STR(" (C++ exception, logged above)") : STR("")));
        if (sm.fault()) compat_safe_enter(name);
    }

    // WHAT: true while kill_switch=1. The INI is re-read at most every second here (the tick skips tick_work while killed, so check_config
    //       would never see the change back to 0). First time on: chunk holds released, one log line.
    bool compat_killed()
    {
        static ULONGLONG next_cfg = 0;
        static bool seen = false;
        const ULONGLONG now = GetTickCount64();
        if (now >= next_cfg) { next_cfg = now + 1000; check_config(); }
        const bool on = g_cfg.kill_switch;
        if (on && !seen)
        {
            seen = true;
            try { chunk_hold_release_all(); } catch (...) {}
            log_line(STR("COMPAT kill_switch on: mod idle"));
        }
        else if (!on && seen)
        {
            seen = false;
            log_line(STR("COMPAT kill_switch off: mod resumes"));
        }
        return on;
    }
}
