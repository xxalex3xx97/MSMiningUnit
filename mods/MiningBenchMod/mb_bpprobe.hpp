// FILE: mods/MiningBenchMod/mb_bpprobe.hpp
// WHAT: M0 probe logger for the factory blueprint work. Off by default. MiningCmd.txt commands bp_arm, bp_dump, bp_disarm, bp_sig (read only: function signatures + struct layouts), bp_place <fn> <class> <x> <y> <h> <rot> [stationId] (WRITES one build command, only on user command), bp_cls (read only: InputCommand + Build/Place/Machine/Blueprint classes), bp_send <InputClass> <receiverId> k=v... (WRITES one command),
//       bp_bld (read only: every BuilderBase/MachineBuilder/ConveyorBuilder instance fully + MachineBuilder property layout)
//       bp_paste <path|clip> [station <id>] [replace] (WRITES: Factory Blueprint JSON (website export) -> BuildInput per machine, all or
//       nothing: every block must be on the station grid, machines inside one block floor, parity X~w Y~h, no overlaps. replace = SYNC (see
//       SYNC below): removes conflicting machines and stale ones (DisassembleInput, never Dock/SolarPanel/Console) and places after the removal. Result line in
//       dlls\PasteStatus.txt. bp_paste_selftest (read only: parser + footprint/floor/overlap checks, PASS/FAIL in BPProbe.txt).
//       bp_import <path> [station <id>] (dry run: plans like bp_paste replace, prices it; WRITES only dlls\BlueprintImport.cost + .status; places, removes, sends nothing).
//       bp_setpath accepts hex text for ArrayProperty<byte> (machineconfigdata=<hex>): copied into a new TArray with FMemory::Malloc.
//       Machine config bytes (SerializedMachineConfig, vault rea-machine-config.md s2/s3) are written by bp_paste: recipe machines
//       (Recipe + Ports, layout 01 00 FString i32 n bytes), undergrounds (PROVEN in game: a pair = ONE machine at the entrance,
//       01 00 i32 Length = cells entrance..exit inclusive, exit cell and gap get no machine; see pp_pair_underground).
//       Recipe (site id "item_a+item_b>item_out") -> REC_* by the offline table (gen_recipe_table.py, vault main-sequence/tasks) -> live Recipe
//       asset Name property, verbatim (the config string). Plain game name: used if a live Recipe Name matches. No match: refused whole,
//       "recipe not found: <site id>". Logged: PASTE RECIPE site=<id> -> REC_xxx -> name=<string>. Recipe property list logged once (RECIPE LAYOUT).
//       Every placed machine is re-read after +3 s: PASTE CFG <id> <class> sent=<hex> live=<hex> MATCH|DIFF (+ Length for undergrounds).
//       ConfigHex (string on a JSON machine, even-length hex, <= 4096 bytes, else refused naming the machine): used verbatim as the config,
//       recipe/ports encoder and recipe lookup skipped for that machine (log PASTE CFG ... source=ConfigHex). Underground: ignored when the
//       pair folds it (log note); a single unpaired underground with ConfigHex 01 00 i32 Length (Length 1..tier max) = stand-alone tunnel.
//       Cost check before placing (not in creative): Buildable ItemCost (ItemStack Item/Amount) summed per machine vs the player inventory
//       from the bp_snap pass ("player|" keys). Greedy in input order: a machine whose cost cannot be paid is skipped (not placed, not
//       verified). Status: "ok placed N, skipped M (not enough items: item xK, ...)". Creative flag: DMSPlayerObject.bCreative, else
//       BP_PlayerController_C.bCreative (first readable; logged as PASTE COST creative: ...). Missing machines after +3 s: PASTE VERIFY MISSING.
//       Refund space check (refund_check, before bp_kill / bp_run kill_new / bp_paste replace send anything): the removed machines' ItemCost
//       comes back to the player inventory; if it does not fit (free = empty slots + room in partial stacks of the same item, max =
//       Item MaxStackSize; HasSpaceForItems per item too when readable) the whole removal is refused: "not enough inventory space for
//       refunds: need N more slots". Logged as REFUND need <who>: ... Creative players are checked too. Selftest: refund_fit cases.
//       KEEP (user rule 2026-11-07, never remove+re-place what is already right): a pasted machine with an existing machine of the same class,
//       GridLocation, Height and rotation is identical: NOT placed, NOT paid, NOT refunded, NOT removed (log PASTE KEEP <cls> @(x,y,h) id=N).
//       Its config is compared with its live SerializedMachineConfig (Blueprint.Machines, same class+grid+height+rot; no entry = unknown, kept as is).
//       Recipe machines only: recipe differs -> SetRecipeInput (receiver = machine id, Recipe = live asset by Name, Amount/Config = factory default 0)
//       (PASTE RECIPE CHANGE id=N old=<name> new=<name>); each differing port -> MachinePortInput PortIndex (PASTE PORT TOGGLE). Undergrounds and
//       other classes: kept, never changed. Different class or rotation/height overlap = conflict as before (replace). After +3 s the changed
//       kept machines get PASTE CFG MATCH|DIFF too. Status: "ok placed N, kept K, recipes changed R, [skipped ...,] refused F".
//       SYNC (bp_paste ... replace; user rule 2026-11-07: the import mirrors the blueprint): STALE = every existing machine on the target station
//       that is not KEPT (identical; an underground with a different Length is not identical), whose class has a live Buildable, and that is not
//       Dock/SolarPanel/Console/ItemCrate/Elevator/Teleporter. Stale machines are removed with the conflicts (refund_check over all of them first,
//       nothing removed on refusal), then stage 1 waits until they are gone, then the new ones are placed. A conflict that is not stale refuses
//       ("sync refused"). Empty blueprint is allowed (sync then cleans all player-built machines). Log: PASTE STALE <cls> @(x,y,h) id=N per removal,
//       PASTE SYNC removed R stale, kept K, placed P, recipes changed C. Status: "ok sync: placed N, kept K, removed R stale, recipes changed C, ...".
//       Selftest: 'sync:' asserts in bp_paste_selftest.
//       bp_recipes (read only): every live Recipe asset -> dlls\RecipeNames.json {"REC_x":{"name","crafter"?,"output"?}}, 'RECIPES n written'.
//       bp_obj <machineId> (read only: full property dump of that live object into BPProbe.txt under "=== bp_obj <id>").
//       bp_run <absolute script path> | bp_run stop (WRITES: a text script read at call time, run one op per bp_tick without blocking.
//       Lines: station <id> | place <DAT_name> <x> <y> <h> <rot> [cfg=<hex>] | snap | kill_new | wait <ms> | report <label> | repeat <n> ... endrepeat
//       (nesting up to 2). Bad file = refused whole at load. Every op logged to BPProbe.txt as RUN <line> ...; end: RUN DONE ops= refused=)
//       Probe 7 (builder placement test): bp_aim <x> <y> <z> | bp_aim off (WRITES: while set, the builder UpdatePreview pre-hook
//       overwrites the HitResult Location/ImpactPoint, bBlockingHit=1, Normal/ImpactNormal=(0,0,1); works unarmed, hook registered once),
//       bp_confirm (WRITES: ConfirmBuild on the live builder, refused unless bValid=1, then +1.5 s / +5 s diffs),
//       bp_enter <BuildableName> [machine] (WRITES: EnterBuildingMode / EnterMachineBuildMode on BP_PlayerController_C, machine: 2nd arg null),
//       HITLOG while armed: builder UpdatePreview HitResult (nested numbers, weak refs resolved), BuilderLocation/Rotation, builder info, aim readback.
//       Up to 60 HITLOG lines per arming, 1 per 200 ms.
//       bp_cd <ClassName> [ClassName...] (read only: classes matched by exact name or contains:xxx; per class up the super chain
//       to the engine stop classes: own properties + every own function). Names are space separated.
//       bp_arm hooks the build/place/construct functions of the player controller and the station-side classes, captures their
//       raw parameters on the hook (no logging there), and writes a decoded dump plus inventory snapshot diffs to dlls\BPProbe.txt.
//       bp_dump writes one station dump (allowed while unarmed). Output capped at 64 MB.
// WHY separate: the probe is a read-only research tool; it must not touch the mining loop. Everything it does is reflection reads
//       and log writes. It never calls RemoveItems / AddItems / any game function that changes state.
// DEPENDS ON: mb_common.hpp (alive, seh_run, g_seh, seh_describe, get_dll_dir, to_utf8, lower, log_line), mb_ue.hpp (obj_id, first_of,
//             get_obj_prop, get_int_prop, type_text, any_prop, pvec_text, find_by_id), mb_devcmd.hpp (station_ids, stack_read),
//             mb_rocks.hpp (item_kind).
// USED BY: dllmain.cpp (include), mb_testcmd.hpp (run_cmd -> bp_cmd for "bp_*" commands; cmd_tick -> bp_tick every 0.25 s).
// GAME FACTS: the hook callback runs inside the game's ProcessEvent path. A native fault there kills the game, so the callback only
//       copies bytes (under seh_run). Every object read happens under seh_run too (dangling pointers are possible).
#pragma once

#include <mutex>
#include <deque>
#include <sstream>
#include <fstream>
#include <charconv>
#include <system_error>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>

namespace
{
    // WHAT: a hooked function and its capture state. Lives in g_bp.hooks (a deque, so the address given to RegisterHook stays valid).
    struct BpHook { std::wstring name; UFunction* fn = nullptr; bool input = false; bool selfdump = false; bool upd = false, bld = false; int hits = 0; int cap = 40; std::vector<size_t> objoffs;
                    int logged = 0, dup = 0, stat_hits = 0; std::vector<uint8_t> last, last_selfb; int last_self = -2; };
    // WHAT: one object parameter of a capture: raw bytes of the object, plus the first 64 elements of each array property.
    struct BpObj { size_t off = 0; UClass* cls = nullptr; int id = -1; std::vector<uint8_t> bytes; std::vector<std::pair<size_t, std::vector<uint8_t>>> arrs; };
    // WHAT: one hook call: time, hook, self object, raw parameter block, object parameters.
    struct BpCap { ULONGLONG ms = 0; BpHook* h = nullptr; int hit = 0; std::wstring selfcls; int selfid = -1; std::vector<uint8_t> parms; std::vector<BpObj> objs; };
    // WHAT: inventory totals keyed "owner|item" plus machine ids (id -> class) at one time.
    struct BpSnap { ULONGLONG ms = 0; std::map<std::wstring, int64_t> inv; std::map<int, std::wstring> ids; };
    // WHAT: all probe state. Written by the hook (armed, queue, hits), the tick and the commands (game thread).
    struct BpState
    {
        bool armed = false, hooked_in = false;
        std::mutex mu;                       // guards q only
        std::deque<BpCap> q;                 // captures waiting for the tick
        std::deque<BpHook> hooks;            // deque: pointers stay stable
        std::set<std::wstring> hooked;       // function paths already hooked
        std::map<std::wstring, int> skipped; // skipped command classes -> count
        int faults = 0;                      // SEH faults in the hook callback
        std::string buf;                     // text waiting for bp_flush
        size_t file_bytes = 0;               // size of BPProbe.txt as we know it
        bool full = false;                   // 64 MB reached: output dropped
        std::deque<BpSnap> base;             // baseline snapshots, newest last (keep 6)
        ULONGLONG next_base = 0;             // GetTickCount64 when the next baseline is taken
        double period_s = 3.0;               // baseline period: max(3 s, 30 * snapshot ms)
        bool pending = false;                // a triggered +1.5 s / +5 s check is running
        ULONGLONG trig_ms = 0;               // time of the trigger capture
        BpSnap ref;                          // baseline the trigger is diffed against
        std::wstring trig_what;              // hook names that triggered this check
        int stage = 0;                       // 0 = waiting for +1.5 s, 1 = waiting for +5 s
        bool base_logged = false;            // BASELINE line written once
        int faults_logged = 0;               // last fault count written
        ULONGLONG next_skip_log = 0;         // next SKIPPED summary
        int arr_lim = 64;                    // array elements shown per array in a dump (1000 during bp_dump)
        int dropped = 0;                     // captures dropped (queue full)
        std::map<const void*, uint64_t> eh_sig; // EditHistory signature per owner object
        ULONGLONG next_eh = 0, next_des = 0, next_stat = 0;
        int hitlog_n = 0;                    // Probe 7: HITLOG lines this arming (cap 60)
        ULONGLONG hitlog_ms = 0;             // Probe 7: time of the last HITLOG line (1 per 200 ms)
        std::deque<std::wstring> hitq;       // Probe 7: HITLOG text waiting for bp_tick (guarded by mu)
    };
    BpState g_bp;

    // WHAT: bp_aim state. v = override point; wrote = the pre-hook wrote it (the next hit's readback compares against v);
    //       writes = count; hooked = UpdatePreview hooks registered (once).
    struct BpAim { bool on = false; double v[3] = { 0, 0, 0 }; bool wrote = false; int writes = 0; bool hooked = false; };
    BpAim g_aim;

    constexpr size_t kBpCapBytes = 64u * 1024u * 1024u;  // BPProbe.txt cap
    constexpr size_t kBpMaxHooks = 800;
    constexpr int kBpCapPerFn = 300;

    // WHAT: command classes whose captures are dropped (player controls, not factory work). Lowercase match, see bp_has.
    const wchar_t* const kBpSkip[] = { L"ShipMovement", L"ShipFiring", L"InputView", L"ViewTarget", L"Rotation", L"Throttle", L"Steering", L"Camera", L"CharacterMove", L"SetPiloting" };
    constexpr size_t kBpSkipN = sizeof(kBpSkip) / sizeof(kBpSkip[0]);
    // WHAT: function-name words that make a function a hook candidate (lowercase).
    const wchar_t* const kBpMatch[] = { L"build", L"place", L"construct", L"machine", L"disassemble", L"belt", L"conveyor",
                                        L"blueprint", L"designer", L"edit", L"confirm", L"apply", L"pending", L"undo", L"redo", L"cancel", L"frame" };
    constexpr size_t kBpMatchN = sizeof(kBpMatch) / sizeof(kBpMatch[0]);
    // WHAT: function-name prefixes that are never hooked (lowercase).
    const wchar_t* const kBpExcl[] = { L"get", L"is", L"can", L"has", L"find", L"calc", L"update", L"tick" };
    constexpr size_t kBpExclN = sizeof(kBpExcl) / sizeof(kBpExcl[0]);

    // WHAT: true when s contains one of the list words (case-insensitive).
    bool bp_has(const std::wstring& s, const wchar_t* const* list, size_t n)
    {
        const std::wstring l = lower(s);
        for (size_t i = 0; i < n; ++i) if (l.find(lower(list[i])) != std::wstring::npos) return true;
        return false;
    }

    // WHAT: true when the lowercase name starts with one of the list words.
    bool bp_starts(const std::wstring& s, const wchar_t* const* list, size_t n)
    {
        for (size_t i = 0; i < n; ++i) if (s.rfind(list[i], 0) == 0) return true;
        return false;
    }

    // WHAT: "hh:mm:ss.mmm " from the local clock.
    std::string bp_stamp()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char b[32];
        std::snprintf(b, sizeof(b), "%02u:%02u:%02u.%03u ", static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute),
                      static_cast<unsigned>(st.wSecond), static_cast<unsigned>(st.wMilliseconds));
        return b;
    }

    // WHAT: queues text for BPProbe.txt: every line gets a time stamp. Written by bp_flush.
    // WHY: keeps the file append-only and cheap; nothing touches the disk on the game thread except bp_flush.
    void bp_out(const std::wstring& s)
    {
        if (s.empty()) return;
        size_t i = 0;
        while (i < s.size())
        {
            size_t nl = s.find(L'\n', i);
            if (nl == std::wstring::npos) nl = s.size();
            g_bp.buf += bp_stamp() + to_utf8(s.substr(i, nl - i)) + "\n";
            i = nl + 1;
        }
    }

    // WHAT: writes the queued text to dlls\BPProbe.txt (append). Past 64 MB: one FILE FULL line, then nothing.
    void bp_flush()
    {
        if (g_bp.buf.empty()) return;
        std::string out;
        out.swap(g_bp.buf);
        if (g_bp.full) return;
        if (g_bp.file_bytes + out.size() > kBpCapBytes)
        {
            g_bp.full = true;
            out = bp_stamp() + "FILE FULL (64 MB) - output dropped\n";
        }
        std::ofstream o(get_dll_dir() / L"BPProbe.txt", std::ios::binary | std::ios::app);
        o << out;
        g_bp.file_bytes += out.size();
    }

    // WHAT: hook callback context: the UE4SS call context and our hook record.
    struct BpHookCtx { RC::Unreal::UnrealScriptFunctionCallableContext* c; BpHook* h; };
    // Probe 7: builder UpdatePreview pre-hook body (HITLOG while armed, aim write). Defined with the Probe 7 helpers below.
    void bp_upd_pre(BpHookCtx* x);

    // WHAT: copies one object for a capture: class, id, property bytes, first elements of each array. off = SIZE_MAX for the self object. Under seh_run.
    void bp_copy_obj(UObject* o, size_t off, BpObj& b)
    {
        UClass* c = o->GetClassPrivate();
        b.off = off;
        b.cls = c;
        b.id = obj_id(o);
        const size_t sz = std::min<size_t>(static_cast<size_t>(c->GetPropertiesSize()), 65536);
        b.bytes.assign(reinterpret_cast<uint8_t*>(o), reinterpret_cast<uint8_t*>(o) + sz);
        for (UStruct* u = c; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* pr : u->ForEachProperty())
                if (auto* ap = CastField<FArrayProperty>(pr))
                {
                    auto* ta = reinterpret_cast<TArray<uint8_t>*>(reinterpret_cast<uint8_t*>(o) + pr->GetOffset_Internal());
                    const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
                    const int n = std::min<int>(ta->Num(), es == 1 ? 256 : 64);
                    if (n > 0 && ta->GetData())
                        b.arrs.push_back({ static_cast<size_t>(pr->GetOffset_Internal()), std::vector<uint8_t>(ta->GetData(), ta->GetData() + n * es) });
                }
    }

    // WHAT: runs inside seh_run on every hooked call while armed. Copies the parameter block and the object parameters, queues the capture.
    // WHY: nothing is logged or formatted here (the game thread is inside the game's own call). bp_format_fn does that later.
    void bp_capture_fn(void* p)
    {
        auto* x = static_cast<BpHookCtx*>(p);
        BpHook* h = x->h;
        if (h->bld) bp_upd_pre(x);   // Probe 7: HITLOG and aim, before the armed check
        if (!g_bp.armed) return;
        ++h->hits;
        if (!h->input && h->logged >= h->cap) return;
        uint8_t* base = x->c->TheStack.Locals();
        if (!base) return;
        std::vector<uint8_t> parms(base, base + h->fn->GetParmsSize());
        UObject* self = x->c->Context;
        const int sid = alive(self) ? obj_id(self) : -1;
        BpObj sb;   // self copy (selfdump hooks only), off = SIZE_MAX; sb.bytes stays empty otherwise
        if (h->selfdump && alive(self)) bp_copy_obj(self, SIZE_MAX, sb);
        if (!h->input && h->objoffs.empty()) { if (h->last_self == sid && h->last == parms && h->last_selfb == sb.bytes) { ++h->dup; return; } h->last = parms; h->last_self = sid; h->last_selfb = sb.bytes; }
        ++h->logged;
        BpCap cap;
        cap.ms = GetTickCount64(); cap.h = h; cap.hit = h->hits; cap.parms = std::move(parms);
        if (alive(self)) { cap.selfcls = self->GetClassPrivate()->GetName(); cap.selfid = sid; }
        if (sb.cls) cap.objs.push_back(std::move(sb));
        for (size_t off : h->objoffs)
        {
            UObject* o = *reinterpret_cast<UObject* const*>(base + off);
            if (!alive(o)) continue;
            const std::wstring cn = o->GetClassPrivate()->GetName();
            if (bp_has(cn, kBpSkip, kBpSkipN)) { ++g_bp.skipped[cn]; return; }
            BpObj b;
            bp_copy_obj(o, off, b);
            cap.objs.push_back(std::move(b));
        }
        std::lock_guard<std::mutex> lk(g_bp.mu);
        if (g_bp.q.size() < 5000) g_bp.q.push_back(std::move(cap)); else ++g_bp.dropped;
    }

    // WHAT: pre-hook: the SEH-guarded capture. A fault is counted, never thrown into the game.
    void bp_hook_pre(RC::Unreal::UnrealScriptFunctionCallableContext& ctx, void* custom)
    {
        BpHookCtx c{ &ctx, static_cast<BpHook*>(custom) };
        if (!g_bp.armed && !(g_aim.on && c.h->bld)) return;
        if (!seh_run(bp_capture_fn, &c)) ++g_bp.faults;
    }

    // WHAT: post-hook placeholder (RegisterHook needs both callbacks). Does nothing.
    void bp_hook_post(RC::Unreal::UnrealScriptFunctionCallableContext&, void*) {}

    // WHAT: item display names per snapshot (item object -> "kind/name"). Cleared at the start of every snapshot.
    std::map<UObject*, std::wstring> g_bp_names;

    // WHAT: reads one ItemTotals array (inventory object) into m as "key|item" -> amount. Read only.
    void bp_inv_read(UObject* inv, const std::wstring& key, std::map<std::wstring, int64_t>& m)
    {
        if (!alive(inv)) return;
        FProperty* p = inv->GetPropertyByNameInChain(STR("ItemTotals"));
        auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
        auto* ta = ap ? inv->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("ItemTotals")) : nullptr;
        auto* sp = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
        if (!ta || !sp) return;
        const int es = ap->GetInner()->GetElementSize();
        for (int i = 0; i < ta->Num(); ++i)
        {
            const uint8_t* e = reinterpret_cast<const uint8_t*>(ta->GetData()) + static_cast<size_t>(i) * es;
            uint64_t item = 0;
            int32_t amt = 0;
            int off = 0;
            if (!stack_read(sp->GetStruct(), e, item, amt, off) || amt <= 0 || item == 0) continue;
            UObject* it = reinterpret_cast<UObject*>(static_cast<uintptr_t>(item));
            std::wstring name;
            if (alive(it)) { auto f = g_bp_names.find(it); if (f == g_bp_names.end()) { std::wstring dn = item_kind(it); const std::wstring on = it->GetName(); if (dn != on) dn += L"/" + on; f = g_bp_names.emplace(it, dn).first; } name = f->second; }
            else { wchar_t hx[32]; swprintf(hx, 32, L"0x%llx", static_cast<unsigned long long>(item)); name = hx; }
            m[key + L"|" + name] += amt;
        }
    }

    // WHAT: one snapshot pass, under seh_run. ItemTotals of machine/construction inventories (via InventoryID), the player and every ship;
    //       the machine ids per station (id -> class).
    struct BpSnapCtx { BpSnap* out; };
    void bp_snap_fn(void* p)
    {
        g_bp_names.clear();
        BpSnap& o = *static_cast<BpSnapCtx*>(p)->out;
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
        std::map<int, UObject*> byid;
        for (UObject* x : all)
            if (alive(x) && x->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(x) > 0) byid[obj_id(x)] = x;
        std::vector<UObject*> stations;
        UObjectGlobals::FindAllOf(STR("StationObject"), stations);
        for (UObject* st : stations)
        {
            if (!alive(st) || st->GetName().rfind(STR("Default__"), 0) == 0) continue;
            const int stid = obj_id(st);
            int32_t sinv = 0;
            if (get_int_prop(st, STR("InventoryID"), sinv) && sinv > 0) { auto iv = byid.find(sinv); if (iv != byid.end()) bp_inv_read(iv->second, L"stationinv" + std::to_wstring(stid) + L"/inv" + std::to_wstring(sinv), o.inv); }
            std::vector<int> ids = station_ids(st, STR("MachineIDs"));
            for (int id : station_ids(st, STR("ConstructionObjectIDs"))) ids.push_back(id);
            for (int id : station_ids(st, STR("PlatformIDs"))) ids.push_back(id);
            for (int id : ids)
            {
                auto it = byid.find(id);
                if (it == byid.end() || !alive(it->second)) continue;
                UObject* m = it->second;
                const std::wstring cls = m->GetClassPrivate()->GetName();
                o.ids[id] = cls;
                int32_t invid = 0;
                if (get_int_prop(m, STR("InventoryID"), invid) && invid > 0)
                {
                    auto iv = byid.find(invid);
                    if (iv != byid.end())
                        bp_inv_read(iv->second, L"st" + std::to_wstring(stid) + L"/" + cls + L"#" + std::to_wstring(id) + L"/inv" + std::to_wstring(invid), o.inv);
                }
            }
        }
        if (UObject* pl = first_of(STR("DMSPlayerObject")))
            if (alive(pl))
            {
                UObject* pinv = get_obj_prop(pl, STR("Inventory"));
                if (!alive(pinv)) { int32_t pid = 0; if (get_int_prop(pl, STR("InventoryID"), pid) && byid.count(pid)) pinv = byid[pid]; }
                bp_inv_read(pinv, STR("player"), o.inv);
            }
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* sh : ships)
            if (alive(sh) && sh->GetName().rfind(STR("Default__"), 0) != 0)
                bp_inv_read(get_obj_prop(sh, STR("Inventory")), L"ship" + std::to_wstring(obj_id(sh)), o.inv);
    }

    // WHAT: takes a snapshot (under seh_run) and sets g_bp.period_s = max(1 s, 30 x snapshot ms). false on a fault (logged).
    bool bp_snap(BpSnap& out)
    {
        out = BpSnap{};
        out.ms = GetTickCount64();
        const ULONGLONG t0 = GetTickCount64();
        BpSnapCtx c{ &out };
        const bool ok = seh_run(bp_snap_fn, &c);
        const double ms = static_cast<double>(GetTickCount64() - t0);
        g_bp.period_s = std::max(1.0, ms * 0.03);
        if (!ok) bp_out(L"FAULT snapshot " + seh_describe(g_seh));
        return ok;
    }

    // WHAT: "INV" and "MACHINE" lines between two snapshots. Nothing changed -> "no change".
    void bp_diff(const BpSnap& a, const BpSnap& b)
    {
        int lines = 0;
        std::set<std::wstring> keys;
        for (auto& kv : a.inv) keys.insert(kv.first);
        for (auto& kv : b.inv) keys.insert(kv.first);
        for (auto& k : keys)
        {
            const int64_t va = a.inv.count(k) ? a.inv.at(k) : 0;
            const int64_t vb = b.inv.count(k) ? b.inv.at(k) : 0;
            if (va == vb) continue;
            bp_out(L"INV " + k + L": " + i64w(va) + L" -> " + i64w(vb) + L" (" + i64w(vb - va) + L")");
            ++lines;
        }
        for (auto& kv : b.ids) if (!a.ids.count(kv.first)) { bp_out(L"MACHINE + " + std::to_wstring(kv.first) + L" " + kv.second); ++lines; }
        for (auto& kv : a.ids) if (!b.ids.count(kv.first)) { bp_out(L"MACHINE - " + std::to_wstring(kv.first) + L" " + kv.second); ++lines; }
        if (lines == 0) bp_out(L"no change");
    }

    // WHAT: full inventory list (station inventories and player) of one snapshot, with its age. Used around blueprint applies.
    void bp_inv_full(const BpSnap& sn, const std::wstring& label)
    {
        bp_out(L"INVFULL " + label + L" (snapshot age ms=" + std::to_wstring(GetTickCount64() - sn.ms) + L")");
        for (auto& kv : sn.inv)
            if (kv.first.rfind(L"stationinv", 0) == 0 || kv.first.rfind(L"player", 0) == 0) bp_out(L"  " + kv.first + L" = " + i64w(kv.second));
    }

    // WHAT: lines for one value. Struct / array / object / name types supported; anything else is "?type".
    // WHY: the capture has raw bytes only, so every read is by property type. Object pointers are followed only when alive.
    std::wstring bp_val(const uint8_t* d, FProperty* p, const uint8_t* cb, int depth, int top, const std::vector<uint8_t>* arr_buf = nullptr);

    // WHAT: scalar properties of a live object, one level only (no objects, no arrays). Used for the top-level object in a dump.
    std::wstring bp_live_scalars(const uint8_t* ob, UStruct* cls, int depth)
    {
        std::wstring r = L" {";
        bool first = true;
        for (UStruct* u = cls; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* pr : u->ForEachProperty())
            {
                if (CastField<FObjectProperty>(pr) || CastField<FArrayProperty>(pr) || CastField<FSoftObjectProperty>(pr)) continue;
                if (!first) r += L" ";
                first = false;
                r += pr->GetName() + L"=" + bp_val(ob + pr->GetOffset_Internal(), pr, ob, depth + 1, 0);
            }
        return r + L"}";
    }

    std::wstring bp_val(const uint8_t* d, FProperty* p, const uint8_t* cb, int depth, int top, const std::vector<uint8_t>* arr_buf)
    {
        if (!d || !p) return STR("null");
        if (auto* bp = CastField<FBoolProperty>(p)) return bp->GetPropertyValueInContainer(const_cast<uint8_t*>(cb)) ? STR("1") : STR("0");
        if (CastField<FIntProperty>(p)) return std::to_wstring(*reinterpret_cast<const int32_t*>(d));
        if (CastField<FInt64Property>(p)) return std::to_wstring(*reinterpret_cast<const int64_t*>(d));
        if (CastField<FFloatProperty>(p)) return std::to_wstring(*reinterpret_cast<const float*>(d));
        if (CastField<FDoubleProperty>(p)) return std::to_wstring(*reinterpret_cast<const double*>(d));
        if (CastField<FByteProperty>(p) || CastField<FEnumProperty>(p))
        {
            switch (p->GetSize())
            {
            case 1: return std::to_wstring(*d);
            case 2: return std::to_wstring(*reinterpret_cast<const uint16_t*>(d));
            case 4: return std::to_wstring(*reinterpret_cast<const int32_t*>(d));
            default: return std::to_wstring(*reinterpret_cast<const int64_t*>(d));
            }
        }
        if (CastField<FNameProperty>(p)) return reinterpret_cast<const FName*>(d)->ToString();
        if (CastField<FStrProperty>(p))
        {
            const auto& a = reinterpret_cast<const FString*>(d)->GetCharArray();
            return (a.Num() > 0 && a.GetData()) ? std::wstring(a.GetData()) : std::wstring();
        }
        if (CastField<FTextProperty>(p)) return STR("<text>");
        if (auto* sp = CastField<FStructProperty>(p))
        {
            UStruct* st = sp->GetStruct();
            if (!st) return STR("?struct");
            if (depth >= 10) return STR("{...}");
            std::wstring r = L"{";
            bool first = true;
            for (FProperty* sub : st->ForEachProperty())
            {
                if (!first) r += L" ";
                first = false;
                r += sub->GetName() + L"=" + bp_val(d + sub->GetOffset_Internal(), sub, d, depth + 1, 0);
            }
            return r + L"}";
        }
        if (CastField<FSoftObjectProperty>(p)) return STR("<soft>");   // not dereferenced: the layout is a path, not a pointer
        if (CastField<FObjectProperty>(p))
        {
            UObject* o = *reinterpret_cast<UObject* const*>(d);
            if (!o) return STR("null");
            if (!alive(o)) return STR("dead");
            if (Cast<UClass>(o)) return L"class:" + o->GetName();
            const int oid = obj_id(o);
            const std::wstring cn = o->GetClassPrivate()->GetName();
            std::wstring r = cn + L"#" + std::to_wstring(oid);
            if (oid <= 0) { r += L"<" + o->GetName() + L">"; if (lower(cn).find(L"item") != std::wstring::npos) r += L"<" + item_kind(o) + L">"; }
            if (top == 1) r += bp_live_scalars(reinterpret_cast<const uint8_t*>(o), o->GetClassPrivate(), depth);
            return r;
        }
        if (auto* ap = CastField<FArrayProperty>(p))
        {
            const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(d);
            const int num = ta->Num();
            FProperty* in = ap->GetInner();
            const size_t es = static_cast<size_t>(in->GetElementSize());
            const uint8_t* src = nullptr;
            int avail = 0;
            if (arr_buf) { src = arr_buf->data(); avail = es ? static_cast<int>(arr_buf->size() / es) : 0; }
            else if (ta->GetData()) { src = reinterpret_cast<const uint8_t*>(ta->GetData()); avail = num; }
            if (es == 1 && CastField<FByteProperty>(in))
            {
                const int nb = std::min(std::min(num, 256), avail);
                std::wstring hx, as;
                wchar_t t[4];
                for (int i = 0; i < nb; ++i) { swprintf(t, 4, L"%02x", src[i]); hx += t; as += (src[i] >= 32 && src[i] < 127) ? static_cast<wchar_t>(src[i]) : L'.'; }
                return L"bytes=" + std::to_wstring(num) + L" hex=" + hx + L" ascii=" + as;
            }
            std::wstring r = L"count=" + std::to_wstring(num) + L" [";
            const int n = std::min(std::min(num, g_bp.arr_lim), avail);
            for (int i = 0; src && i < n; ++i)
            {
                if (i) r += L",";
                const uint8_t* e = src + static_cast<size_t>(i) * es;
                if (CastField<FArrayProperty>(in)) r += arr_buf ? L"count=" + std::to_wstring(reinterpret_cast<const TArray<uint8_t>*>(e)->Num()) : bp_val(e, in, e, depth + 1, 0);
                else r += bp_val(e, in, e, depth + 1, 0);
            }
            if (num > n) r += L",...";
            return r + L"]";
        }
        return STR("?") + type_text(p);
    }

    // WHAT: property lines of one struct / class / function, owner class per line. snap (optional) gives the captured arrays.
    // WHY: lim bounds the bytes read (a function's property list can include locals past the copied parameter block).
    void bp_dump(std::wstring& s, const uint8_t* base, UStruct* st, const BpObj* snap, size_t lim = SIZE_MAX)
    {
        for (UStruct* u = st; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                const size_t off = static_cast<size_t>(p->GetOffset_Internal());
                if (off + static_cast<size_t>(p->GetSize()) > lim) continue;
                const std::vector<uint8_t>* ab = nullptr;
                if (snap && CastField<FArrayProperty>(p))
                    for (auto& a : snap->arrs) if (a.first == off) { ab = &a.second; break; }
                s += L"  " + p->GetName() + L" [" + type_text(p) + L"] owner=" + u->GetName() + L" = " + bp_val(base + off, p, base, 0, 1, ab) + L"\n";
            }
    }

    // ===== Probe 7 helpers: HITLOG and the bp_aim pre-hook override (called from bp_capture_fn; under seh_run) =====
    // WHAT: property by name over a struct / class chain (super chain included), or nullptr.
    FProperty* bp_find(UStruct* st, const wchar_t* name)
    {
        for (UStruct* u = st; u; u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
                if (p->GetName() == name) return p;
        return nullptr;
    }

    // WHAT: X/Y/Z of a vector struct at d, doubles or floats by reflection (write=false reads into v, write=true stores v).
    // WHY: FVector_NetQuantize has no reflected fields (the old dump printed {}), its layout is FVector: 3 raw doubles. false = layout unknown.
    bool bp_vec_io(uint8_t* d, UStruct* st, double* v, bool write)
    {
        if (!d || !st) return false;
        const wchar_t* nm[3] = { L"X", L"Y", L"Z" };
        int found = 0;
        for (int i = 0; i < 3; ++i)
        {
            FProperty* f = bp_find(st, nm[i]);
            if (!f) continue;
            uint8_t* fd = d + f->GetOffset_Internal();
            if (CastField<FDoubleProperty>(f))
            {
                double* x = reinterpret_cast<double*>(fd);
                if (write) *x = v[i]; else v[i] = *x;
            }
            else if (CastField<FFloatProperty>(f))
            {
                float* x = reinterpret_cast<float*>(fd);
                if (write) *x = static_cast<float>(v[i]); else v[i] = *x;
            }
            else return false;
            ++found;
        }
        if (found == 3) return true;
        if (found != 0 || st->GetPropertiesSize() < 24) return false;
        double* raw = reinterpret_cast<double*>(d);
        for (int i = 0; i < 3; ++i) { if (write) raw[i] = v[i]; else v[i] = raw[i]; }
        return true;
    }

    // WHAT: vector struct field `name` of the struct at base (bp_vec_io). false when missing.
    bool bp_vec_prop(uint8_t* base, UStruct* st, const wchar_t* name, double* v, bool write)
    {
        FProperty* f = bp_find(st, name);
        auto* sp = f ? CastField<FStructProperty>(f) : nullptr;
        return sp && sp->GetStruct() && bp_vec_io(base + f->GetOffset_Internal(), sp->GetStruct(), v, write);
    }

    // WHAT: where a struct out / ref parameter lives. A CPF_ReferenceParm slot holding a plausible user-space pointer is followed to the
    //       caller's struct (how = "ptr"); otherwise the struct is inline (how = "inline").
    // WHY: unknown which one the game uses. The first hit's slots were all zero (inline), so both are handled and the choice is logged.
    uint8_t* bp_parm_addr(uint8_t* base, FProperty* p, const wchar_t*& how)
    {
        uint8_t* slot = base + p->GetOffset_Internal();
        how = L"inline";
        if (!p->HasAnyPropertyFlags(CPF_ReferenceParm)) return slot;
        const uint64_t v = *reinterpret_cast<const uint64_t*>(slot);
        if (v < 0x10000ull || v >= 0x800000000000ull) return slot;
        how = L"ptr";
        return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(v));
    }

    // WHAT: a TWeakObjectPtr field as "Class#id<Name>" or "null". Resolved by FWeakObjectPtr::Get.
    std::wstring bp_weak_text(const uint8_t* d)
    {
        UObject* o = reinterpret_cast<const FWeakObjectPtr*>(d)->Get();
        if (!alive(o)) return STR("null");
        return o->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(o)) + L"<" + o->GetName() + L">";
    }

    // WHAT: a struct as "{Field=value ...}" over its whole super chain. Scalars via bp_val, nested structs recursed, weak refs resolved.
    //       A struct with no reflected fields (>= 24 bytes) is printed as its 3 raw doubles.
    std::wstring bp_hr_struct(const uint8_t* base, UStruct* st, int depth)
    {
        if (!base || !st) return STR("null");
        if (depth >= 6) return STR("{...}");
        std::wstring r = L"{";
        bool first = true;
        for (UStruct* u = st; u; u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                if (!first) r += L" ";
                first = false;
                const uint8_t* d = base + p->GetOffset_Internal();
                std::wstring v;
                if (auto* sp = CastField<FStructProperty>(p)) v = bp_hr_struct(d, sp->GetStruct(), depth + 1);
                else if (CastField<FWeakObjectProperty>(p)) v = bp_weak_text(d);
                else v = bp_val(d, p, base, depth + 1, 0);
                r += p->GetName() + L"=" + v;
            }
        if (first && st->GetPropertiesSize() >= 24)
        {
            double raw[3] = { 0, 0, 0 };
            bp_vec_io(const_cast<uint8_t*>(base), st, raw, false);
            r += L"raw=(" + std::to_wstring(raw[0]) + L"," + std::to_wstring(raw[1]) + L"," + std::to_wstring(raw[2]) + L")";
        }
        return r + L"}";
    }

    // WHAT: world location and rotation of an actor (K2_GetActorLocation / K2_GetActorRotation via ProcessEvent). n/a when null.
    std::wstring bp_actor_xf(UObject* a)
    {
        if (!alive(a)) return STR("n/a");
        std::wstring r;
        for (const wchar_t* fnn : { L"K2_GetActorLocation", L"K2_GetActorRotation" })
        {
            UFunction* f = a->GetFunctionByNameInChain(fnn);
            FProperty* rp = nullptr;
            if (f) for (FProperty* q : f->ForEachProperty()) if (q->HasAnyPropertyFlags(CPF_ReturnParm)) rp = q;
            auto* rs = rp ? CastField<FStructProperty>(rp) : nullptr;
            if (!rs) return std::wstring(fnn) + L" missing";
            std::vector<uint8_t> buf(f->GetParmsSize() + 16, 0);
            a->ProcessEvent(f, buf.data());
            r += (r.empty() ? L"" : L" ") + bp_hr_struct(buf.data() + rp->GetOffset_Internal(), rs->GetStruct(), 0);
        }
        return r;
    }

    // WHAT: true when cls is Buildable or a subclass of it.
    bool bp_is_buildable(UStruct* cls)
    {
        for (UStruct* u = cls; u; u = u->GetSuperStruct()) if (u->GetName() == L"Buildable") return true;
        return false;
    }

    // WHAT: one builder line: class#id, bValid, object properties with "buildable" in the name, PreviewActor world location / rotation.
    std::wstring bp_builder_info(UObject* self)
    {
        UStruct* cs = self->GetClassPrivate();
        bool valid = false;
        get_bool_prop(self, STR("bValid"), valid);
        std::wstring r = cs->GetName() + L"#" + std::to_wstring(obj_id(self)) + L" bValid=" + (valid ? L"1" : L"0");
        for (UStruct* u = cs; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
                if (CastField<FObjectProperty>(p) && lower(p->GetName()).find(L"buildable") != std::wstring::npos)
                {
                    UObject* o = *reinterpret_cast<UObject* const*>(reinterpret_cast<uint8_t*>(self) + p->GetOffset_Internal());
                    r += L" " + p->GetName() + L"=" + (alive(o) ? o->GetName() : std::wstring(STR("null")));
                }
        r += L" preview=" + bp_actor_xf(get_obj_prop(self, STR("PreviewActor")));
        return r;
    }

    // WHAT: builder UpdatePreview pre-hook body (bld hooks only). Under seh_run.
    //       (1) HITLOG while armed, up to 60 lines, 1 per 200 ms: HitResult at entry (= last result when the slot is the caller's struct),
    //           BuilderLocation / BuilderRotation, builder info, aim readback.
    //       (2) while g_aim.on: writes the aim point into HitResult Location / ImpactPoint, bBlockingHit = 1, Normal / ImpactNormal = (0,0,1).
    // NOTE: a pre-hook write only survives if the native body does not overwrite HitResult afterwards. The readback line shows which.
    void bp_upd_pre(BpHookCtx* x)
    {
        UFunction* fn = x->h->fn;
        uint8_t* base = x->c->TheStack.Locals();
        if (!fn || !base) return;
        auto sst = [](FProperty* p) -> UStruct* { auto* s = p ? CastField<FStructProperty>(p) : nullptr; return s ? s->GetStruct() : nullptr; };
        FProperty* pl = nullptr; FProperty* pr = nullptr; FProperty* ph = nullptr;
        for (FProperty* p : fn->ForEachProperty())
        {
            const std::wstring pn = p->GetName();
            if (pn == L"BuilderLocation") pl = p;
            else if (pn == L"BuilderRotation") pr = p;
            else if (sst(p) && sst(p)->GetName() == L"HitResult") ph = p;
        }
        UStruct* hst = sst(ph);
        if (!hst) return;
        const wchar_t* hhow = L"inline";
        uint8_t* hit = bp_parm_addr(base, ph, hhow);
        double ent[3] = { 0, 0, 0 };
        const bool have_ent = bp_vec_prop(hit, hst, L"Location", ent, false);
        const wchar_t* rb = L"n/a";
        if (g_aim.wrote && have_ent)
        {
            auto close_to = [](double a, double b) { return (a > b ? a - b : b - a) < 0.01; };
            const bool same = close_to(ent[0], g_aim.v[0]) && close_to(ent[1], g_aim.v[1]) && close_to(ent[2], g_aim.v[2]);
            rb = same ? L"MATCH" : L"DIFF";
        }
        const wchar_t* lhow = L"inline";
        const wchar_t* rhow = L"inline";
        uint8_t* locb = pl ? bp_parm_addr(base, pl, lhow) : nullptr;
        uint8_t* rotb = pr ? bp_parm_addr(base, pr, rhow) : nullptr;

        const ULONGLONG now = GetTickCount64();
        if (g_bp.armed && g_bp.hitlog_n < 60 && now >= g_bp.hitlog_ms + 200)
        {
            g_bp.hitlog_ms = now;
            ++g_bp.hitlog_n;
            UObject* self = x->c->Context;
            std::wstring s = L"HITLOG " + std::to_wstring(g_bp.hitlog_n) + L" hook=" + x->h->name + L"\n";
            s += L"  self " + (alive(self) ? bp_builder_info(self) : std::wstring(STR("dead"))) + L"\n";
            s += L"  HitResult[" + std::wstring(hhow) + L"] " + bp_hr_struct(hit, hst, 0) + L"\n";
            if (locb && sst(pl)) s += L"  BuilderLocation[" + std::wstring(lhow) + L"] " + bp_hr_struct(locb, sst(pl), 0) + L"\n";
            if (rotb && sst(pr)) s += L"  BuilderRotation[" + std::wstring(rhow) + L"] " + bp_hr_struct(rotb, sst(pr), 0) + L"\n";
            s += L"  AIM on=" + std::wstring(g_aim.on ? L"1" : L"0") + L" target=(" + std::to_wstring(g_aim.v[0]) + L"," + std::to_wstring(g_aim.v[1]) +
                 L"," + std::to_wstring(g_aim.v[2]) + L") readback(entry Location vs last aim)=" + rb + L" writes=" + std::to_wstring(g_aim.writes) + L"\n";
            std::lock_guard<std::mutex> lk(g_bp.mu);
            g_bp.hitq.push_back(s);
        }

        if (g_aim.on)
        {
            double a[3] = { g_aim.v[0], g_aim.v[1], g_aim.v[2] };
            double n[3] = { 0, 0, 1 };
            const bool ok = bp_vec_prop(hit, hst, L"Location", a, true) && bp_vec_prop(hit, hst, L"ImpactPoint", a, true) &&
                            bp_vec_prop(hit, hst, L"Normal", n, true) && bp_vec_prop(hit, hst, L"ImpactNormal", n, true);
            FProperty* fb = bp_find(hst, L"bBlockingHit");
            auto* bb = fb ? CastField<FBoolProperty>(fb) : nullptr;
            if (bb) bb->SetPropertyValueInContainer(hit, true);
            g_aim.wrote = ok && bb != nullptr;
            if (g_aim.wrote) ++g_aim.writes;
        }
    }

    // WHAT: dump context: one string filled under seh_run.
    struct BpDumpCtx { std::wstring out; };

    // WHAT: BuildBlueprint structs of one owner (station/frame): every sub-property, arrays of structs listed per element.
    //       Collects the class of each machine entry into buildables.
    void bp_blueprint_dump(std::wstring& s, UObject* owner, std::set<UObject*>& buildables)
    {
        const std::wstring oc = owner->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(owner));
        for (UStruct* u = owner->GetClassPrivate(); u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                auto* sp = CastField<FStructProperty>(p);
                if (!sp || !sp->GetStruct() || sp->GetStruct()->GetName().find(L"BuildBlueprint") == std::wstring::npos) continue;
                const uint8_t* sb = reinterpret_cast<const uint8_t*>(owner) + p->GetOffset_Internal();
                s += L"BLUEPRINT " + p->GetName() + L" owner=" + oc + L"\n";
                for (FProperty* sub : sp->GetStruct()->ForEachProperty())
                {
                    const uint8_t* sd = sb + sub->GetOffset_Internal();
                    auto* ap = CastField<FArrayProperty>(sub);
                    auto* ip = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
                    if (!ip) { s += L"  " + sub->GetName() + L" = " + bp_val(sd, sub, sb, 2, 0) + L"\n"; continue; }
                    const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(sd);
                    const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
                    s += L"  " + sub->GetName() + L" count=" + std::to_wstring(ta->Num()) + L"\n";
                    for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
                    {
                        const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
                        s += L"  " + sub->GetName() + L"[" + std::to_wstring(i) + L"] " + bp_val(e, ip, e, 2, 0) + L"\n";
                        for (FProperty* f : ip->GetStruct()->ForEachProperty())
                            if (f->GetName() == STR("Class") && CastField<FObjectProperty>(f))
                                if (UObject* c = *reinterpret_cast<UObject* const*>(e + f->GetOffset_Internal())) buildables.insert(c);
                    }
                }
            }
    }

    // WHAT: full dump of every buildable class (or object) collected from the blueprints (class default object for classes).
    void bp_buildable_dump(std::wstring& s, const std::set<UObject*>& b)
    {
        for (UObject* o : b)
        {
            if (!alive(o)) continue;
            UObject* t = o;
            if (UClass* c = Cast<UClass>(o)) { s += L"BUILDABLE class " + c->GetName() + L"\n"; t = c->GetClassDefaultObject(); }
            else s += L"BUILDABLE obj " + o->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(o)) + L"\n";
            if (alive(t)) bp_dump(s, reinterpret_cast<const uint8_t*>(t), t->GetClassPrivate(), nullptr);
        }
    }

    // WHAT: live station / building frame objects (no Default__ objects), each once.
    void bp_frames(std::vector<UObject*>& out)
    {
        std::set<UObject*> seen;
        for (const wchar_t* cn : { L"StationObject", L"BuildingFrame" })
        { std::vector<UObject*> v; UObjectGlobals::FindAllOf(cn, v); for (UObject* o : v) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && seen.insert(o).second) out.push_back(o); }
    }

    void bp_bpdump_fn(void* p)
    {
        std::wstring& s = static_cast<BpDumpCtx*>(p)->out;
        std::vector<UObject*> fr; bp_frames(fr);
        std::set<UObject*> bl;
        for (UObject* o : fr) bp_blueprint_dump(s, o, bl);
        bp_buildable_dump(s, bl);
    }

    // WHAT: blueprint dump of all frames plus their buildables. Under seh_run.
    void bp_bpdump_all()
    {
        BpDumpCtx c;
        if (!seh_run(bp_bpdump_fn, &c)) bp_out(L"FAULT blueprint dump " + seh_describe(g_seh));
        bp_out(c.out);
    }

    // WHAT: EditHistory of every frame / station. Logged when the signature (count + bytes) changed; the newest 40 entries.
    void bp_edit_fn(void* p)
    {
        std::wstring& s = static_cast<BpDumpCtx*>(p)->out;
        std::vector<UObject*> fr; bp_frames(fr);
        for (UObject* o : fr)
        {
            FProperty* pr = o->GetPropertyByNameInChain(STR("EditHistory"));
            auto* ap = pr ? CastField<FArrayProperty>(pr) : nullptr;
            if (!ap) continue;
            const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(reinterpret_cast<const uint8_t*>(o) + pr->GetOffset_Internal());
            const int n = ta->Num();
            const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
            uint64_t h = 1469598103934665603ull ^ static_cast<uint64_t>(n);
            for (size_t i = 0; ta->GetData() && i < static_cast<size_t>(n) * es && i < 65536; ++i) h = (h ^ ta->GetData()[i]) * 1099511628211ull;
            auto it = g_bp.eh_sig.find(o);
            if (it != g_bp.eh_sig.end() && it->second == h) continue;
            const bool first = (it == g_bp.eh_sig.end());
            g_bp.eh_sig[o] = h;
            s += L"EDITHISTORY owner=" + o->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(o)) + L" count=" + std::to_wstring(n) + (first ? L" (first)" : L" (changed)") + L"\n";
            for (int i = std::max(0, n - 40); ta->GetData() && i < n; ++i)
            {
                const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
                if (CastField<FObjectProperty>(ap->GetInner()))
                {
                    UObject* eo = *reinterpret_cast<UObject* const*>(e);
                    if (!alive(eo)) { s += L"  ENTRY[" + std::to_wstring(i) + L"] null/dead\n"; continue; }
                    s += L"  ENTRY[" + std::to_wstring(i) + L"] class=" + eo->GetClassPrivate()->GetName() + L"\n";
                    bp_dump(s, reinterpret_cast<const uint8_t*>(eo), eo->GetClassPrivate(), nullptr);
                }
                else s += L"  ENTRY[" + std::to_wstring(i) + L"] " + bp_val(e, ap->GetInner(), e, 1, 0) + L"\n";
            }
        }
    }

    void bp_edit_run()
    {
        BpDumpCtx c;
        if (!seh_run(bp_edit_fn, &c)) bp_out(L"FAULT edit history " + seh_describe(g_seh));
        bp_out(c.out);
    }

    // WHAT: dump of the designer controller classes (instances and their properties). Under seh_run.
    void bp_designer_fn(void* p)
    {
        std::wstring& s = static_cast<BpDumpCtx*>(p)->out;
        std::set<UObject*> seen;
        for (const wchar_t* cn : { L"BP_DesignerController_C", L"BPC_Designer_C", L"BP_DesignerController", L"BPC_Designer" })
        { std::vector<UObject*> v; UObjectGlobals::FindAllOf(cn, v);
          for (UObject* o : v)
          { if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0 || !seen.insert(o).second) continue;
            s += L"DESIGNER " + o->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(o)) + L"\n";
            bp_dump(s, reinterpret_cast<const uint8_t*>(o), o->GetClassPrivate(), nullptr); } }
    }

    // WHAT: formats one queued capture, under seh_run: header, parameters, object parameters (full dump).
    struct BpFmtCtx { BpCap* c; std::wstring out; };
    void bp_format_fn(void* p)
    {
        auto* x = static_cast<BpFmtCtx*>(p);
        BpCap& c = *x->c;
        std::wstring& s = x->out;
        s += L"HOOK " + c.h->name + L" self=" + c.selfcls + L"#" + std::to_wstring(c.selfid) + L" (hit " + std::to_wstring(c.hit) + L")\n";
        bp_dump(s, c.parms.data(), c.h->fn, nullptr, c.parms.size());
        for (auto& b : c.objs)
        {
            s += L"  OBJ " + (b.off == SIZE_MAX ? std::wstring(L"self") : L"param@" + std::to_wstring(b.off)) + L" class=" + b.cls->GetName() + L" id=" + std::to_wstring(b.id) + L"\n";
            bp_dump(s, b.bytes.data(), b.cls, &b, b.bytes.size());
        }
    }

    // WHAT: hook names of one capture: the hook plus the class of each object parameter.
    std::wstring bp_trig_name(const BpCap& c) { std::wstring r = c.h->name; for (auto& o : c.objs) r += L" " + o.cls->GetName(); return r; }

    // WHAT: the trigger: a capture starts a +1.5 s / +5 s check (first capture only); later captures add their hook name to the same check.
    // WHY: the diff needs a baseline older than the capture, so the newest baseline with ms <= trigger time is used.
    void bp_trigger(const BpCap& cap)
    {
        if (g_bp.pending) { g_bp.trig_what += L" + " + bp_trig_name(cap); return; }
        g_bp.pending = true;
        g_bp.trig_ms = cap.ms;
        g_bp.stage = 0;
        g_bp.trig_what = bp_trig_name(cap);
        if (g_bp.base.empty())
        {
            BpSnap s;
            if (bp_snap(s)) g_bp.ref = s;
            bp_out(L"no baseline: snapshot now");
        }
        else
        {
            const BpSnap* pick = &g_bp.base.front();
            for (auto& b : g_bp.base) if (b.ms <= cap.ms) pick = &b;
            g_bp.ref = *pick;
        }
        if (g_bp.trig_what.find(L"PendingBlueprint") != std::wstring::npos) bp_inv_full(g_bp.ref, L"BEFORE apply");
    }

    // WHAT: new machines (cap 5): live object dump, its Location / ChunkLocation and that of its station. Under seh_run.
    struct BpNewCtx { const std::vector<int>* ids; std::wstring out; };
    void bp_new_fn(void* p)
    {
        auto* x = static_cast<BpNewCtx*>(p);
        std::wstring& s = x->out;
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
        std::map<int, UObject*> byid;
        for (UObject* o : all) if (alive(o)) byid[obj_id(o)] = o;
        std::vector<UObject*> stations;
        UObjectGlobals::FindAllOf(STR("StationObject"), stations);
        int shown = 0;
        for (int id : *x->ids)
        {
            if (shown >= 5) break;
            auto it = byid.find(id);
            if (it == byid.end() || !alive(it->second)) continue;
            UObject* o = it->second;
            ++shown;
            s += L"NEW MACHINE " + std::to_wstring(id) + L" " + o->GetClassPrivate()->GetName() + L"\n";
            bp_dump(s, reinterpret_cast<const uint8_t*>(o), o->GetClassPrivate(), nullptr);
            s += L"  Location=" + pvec_text(o, STR("Location")) + L" ChunkLocation=" + pvec_text(o, STR("ChunkLocation")) + L"\n";
            for (UObject* st : stations)
            {
                if (!alive(st)) continue;
                std::vector<int> mids = station_ids(st, STR("MachineIDs"));
                if (std::find(mids.begin(), mids.end(), id) == mids.end()) continue;
                s += L"  STATION " + std::to_wstring(obj_id(st)) + L" Location=" + pvec_text(st, STR("Location")) + L" ChunkLocation=" + pvec_text(st, STR("ChunkLocation")) + L"\n";
            }
        }
    }

    // WHAT: dumps the new machines of one event (see bp_new_fn), then rescans the hooks once (a new class may bring new functions).
    void bp_new_machine_dump(const std::vector<int>& added)
    {
        if (added.empty()) return;
        BpNewCtx c{ &added, L"" };
        const bool ok = seh_run(bp_new_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT new machine dump " + seh_describe(g_seh));
    }

    // WHAT: rescan under seh_run: hooks the player controller's AddInputCommand (input) and the build-like functions of the controller,
    //       station and machine classes (walk each class chain up to the engine). Returns the number of new hooks, -1 when the controller is not ready.
    struct BpScanCtx { int r = -1; bool light = false; };
    bool bp_add_hook(UFunction* fn, bool input, int cap = kBpCapPerFn, bool selfdump = false)
    {
        if (!fn || g_bp.hooks.size() >= kBpMaxHooks) return false;
        std::wstring path = fn->GetFullName();   // "Function /Script/...:Name" -> strip the type word
        { size_t sp = path.find(L' '); if (sp != std::wstring::npos) path = path.substr(sp + 1); }
        if (g_bp.hooked.count(path)) { if (selfdump) for (BpHook& e : g_bp.hooks) if (e.name == path) e.selfdump = true; return false; }
        g_bp.hooks.emplace_back();
        BpHook& h = g_bp.hooks.back();
        h.name = path;
        h.fn = fn;
        h.input = input;
        h.cap = input ? 0 : cap;
        h.selfdump = selfdump;
        h.upd = lower(fn->GetName()) == L"updatepreview";
        h.bld = h.upd && path.rfind(L"/Script/MainSequence.", 0) == 0 && path.find(L"Builder") != std::wstring::npos;
        for (FProperty* pr : fn->ForEachProperty())
            if (CastField<FObjectProperty>(pr) && !pr->HasAnyPropertyFlags(CPF_ReturnParm)) h.objoffs.push_back(static_cast<size_t>(pr->GetOffset_Internal()));
        try { UObjectGlobals::RegisterHook(path, bp_hook_pre, bp_hook_post, &h); }
        catch (...) { bp_out(L"HOOK FAIL " + path); g_bp.hooks.pop_back(); return false; }
        g_bp.hooked.insert(path);
        bp_out(L"HOOK + " + path);
        return true;
    }

    // ===== Probe 5: builder hooks (self dump on every hit), bp_bld (read only) =====
    // WHAT: hooks every function of BuilderBase / MachineBuilder / ConveyorBuilder, inherited ones included (super chain up to Object).
    //       GetBuildCost, IsUpgrading and Refresh* are skipped (too noisy). UpdatePreview: cap 5. Every hit dumps the self object.
    void bp_builder_hooks()
    {
        for (const wchar_t* cn : { L"BuilderBase", L"MachineBuilder", L"ConveyorBuilder" })
        {
            UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (std::wstring(L"/Script/MainSequence.") + cn).c_str());
            if (!alive(c)) continue;
            for (UStruct* u = c; u && u->GetName() != STR("Object") && g_bp.hooks.size() < kBpMaxHooks; u = u->GetSuperStruct())
            {
                UClass* uc = Cast<UClass>(u);
                if (!uc) continue;
                for (UFunction* f : uc->ForEachFunction())
                {
                    const std::wstring ln = lower(f->GetName());
                    if (ln.rfind(L"tick", 0) == 0 || ln.rfind(L"receivetick", 0) == 0 || ln.rfind(L"executeubergraph", 0) == 0) continue;
                    if (ln == L"getbuildcost" || ln == L"isupgrading" || ln.rfind(L"refresh", 0) == 0) continue;
                    bp_add_hook(f, false, ln == L"updatepreview" ? 5 : kBpCapPerFn, true);
                }
            }
        }
    }

    void bp_scan_fn(void* p)
    {
        auto* c = static_cast<BpScanCtx*>(p);
        const bool light = c->light;
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { c->r = -1; return; }
        const size_t before = g_bp.hooks.size();
        bp_add_hook(ctrl->GetFunctionByNameInChain(STR("AddInputCommand")), true);
        std::set<UClass*> classes;
        std::set<UClass*> allfn;   // classes whose every function is hooked (not just the name matches)
        classes.insert(ctrl->GetClassPrivate());
        std::vector<UObject*> stations;
        UObjectGlobals::FindAllOf(STR("StationObject"), stations);
        std::vector<UObject*> all;
        if (!light) UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
        std::map<int, UObject*> byid;
        for (UObject* o : all) if (alive(o)) byid[obj_id(o)] = o;
        for (UObject* st : stations)
        {
            if (!alive(st) || st->GetName().rfind(STR("Default__"), 0) == 0) continue;
            classes.insert(st->GetClassPrivate());
            allfn.insert(st->GetClassPrivate()); if (light) continue;
            std::vector<int> ids = station_ids(st, STR("MachineIDs"));
            for (int id : station_ids(st, STR("ConstructionObjectIDs"))) ids.push_back(id);
            for (int id : station_ids(st, STR("PlatformIDs"))) ids.push_back(id);
            for (int id : ids)
            {
                auto it = byid.find(id);
                if (it != byid.end() && alive(it->second)) classes.insert(it->second->GetClassPrivate());
            }
        }
        std::vector<UObject*> bf;
        UObjectGlobals::FindAllOf(STR("BuildingFrame"), bf);
        for (UObject* o : bf) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0) { classes.insert(o->GetClassPrivate()); allfn.insert(o->GetClassPrivate()); }
        for (const wchar_t* cn : { L"BP_DesignerController_C", L"BPC_Designer_C", L"BP_DesignerController", L"BPC_Designer" })
        { std::vector<UObject*> dv; UObjectGlobals::FindAllOf(cn, dv); for (UObject* o : dv) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0) { classes.insert(o->GetClassPrivate()); allfn.insert(o->GetClassPrivate()); } }
        for (UClass* c : classes)
        {
            const bool every = allfn.count(c) != 0;
            for (UStruct* u = c; u && g_bp.hooks.size() < kBpMaxHooks; u = u->GetSuperStruct())
            {
                if (u->GetFullName().find(L"/Script/Engine.") != std::wstring::npos) break;
                if (UClass* uc = Cast<UClass>(u))
                    for (UFunction* f : uc->ForEachFunction())
                    {
                        const std::wstring n = lower(f->GetName());
                        if (n.rfind(L"tick", 0) == 0 || n.rfind(L"receivetick", 0) == 0 || n.rfind(L"executeubergraph", 0) == 0) continue;
                        if (!every && (!bp_has(n, kBpMatch, kBpMatchN) || bp_starts(n, kBpExcl, kBpExclN))) continue;
                        bp_add_hook(f, false);
                    }
            }
        }
        bp_builder_hooks();
        c->r = static_cast<int>(g_bp.hooks.size() - before);
        if (c->r > 0 || !light) bp_out(L"HOOK total " + std::to_wstring(g_bp.hooks.size()) + L" new " + std::to_wstring(c->r));
        if (g_bp.hooks.size() >= kBpMaxHooks) bp_out(L"HOOK LIMIT reached");
    }

    int bp_scan_hooks(bool light = false)
    {
        BpScanCtx c;
        c.light = light;
        if (!seh_run(bp_scan_fn, &c)) { bp_out(L"FAULT hook scan " + seh_describe(g_seh)); return -1; }
        return c.r;
    }

    // WHAT: formats one capture under seh_run. A fault is written as a FAULT line and the next capture goes on.
    void bp_format_one(BpCap& cap)
    {
        BpFmtCtx fc{ &cap, L"" };
        if (!seh_run(bp_format_fn, &fc)) bp_out(L"FAULT format " + seh_describe(g_seh));
        bp_out(fc.out);
    }

    // WHAT: one line per machine id of every station plus full dumps (first 3 per class per station, 80 objects max per station).
    void bp_station_fn(void* p)
    {
        std::wstring& s = static_cast<BpDumpCtx*>(p)->out;
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
        std::map<int, UObject*> byid;
        for (UObject* o : all) if (alive(o)) byid[obj_id(o)] = o;
        std::vector<UObject*> stations;
        UObjectGlobals::FindAllOf(STR("StationObject"), stations);
        for (UObject* st : stations)
        {
            if (!alive(st) || st->GetName().rfind(STR("Default__"), 0) == 0) continue;
            s += L"STATION " + std::to_wstring(obj_id(st)) + L" " + st->GetClassPrivate()->GetName() +
                 L" Location=" + pvec_text(st, STR("Location")) + L" ChunkLocation=" + pvec_text(st, STR("ChunkLocation")) + L"\n";
            bp_dump(s, reinterpret_cast<const uint8_t*>(st), st->GetClassPrivate(), nullptr);
            std::vector<int> ids = station_ids(st, STR("MachineIDs"));
            for (int id : station_ids(st, STR("ConstructionObjectIDs"))) ids.push_back(id);
            for (int id : station_ids(st, STR("PlatformIDs"))) ids.push_back(id);
            std::map<std::wstring, int> full;
            int count = 0;
            for (int id : ids)
            {
                if (count >= 80) break;
                auto it = byid.find(id);
                if (it == byid.end() || !alive(it->second)) continue;
                UObject* o = it->second;
                ++count;
                const std::wstring cls = o->GetClassPrivate()->GetName();
                s += L"  " + cls + L" " + std::to_wstring(id) +
                     L" GridTransform=" + any_prop(o, STR("GridTransform")) + L" MachineID=" + any_prop(o, STR("MachineID")) +
                     L" PaletteID=" + any_prop(o, STR("PaletteID")) + L" Location=" + pvec_text(o, STR("Location")) +
                     L" ChunkLocation=" + pvec_text(o, STR("ChunkLocation")) + L"\n";
                if (full[cls]++ < 3)
                {
                    s += L"  FULL " + cls + L" " + std::to_wstring(id) + L"\n";
                    bp_dump(s, reinterpret_cast<const uint8_t*>(o), o->GetClassPrivate(), nullptr);
                }
            }
        }
    }

    // WHAT: file size check for BPProbe.txt: truncates it when over 64 MB, else sets file_bytes from disk. Called by bp_arm and bp_dump.
    void bp_file_init()
    {
        const auto path = get_dll_dir() / L"BPProbe.txt";
        std::error_code ec;
        const auto sz = std::filesystem::file_size(path, ec);
        if (!ec && sz > kBpCapBytes) { std::ofstream t(path, std::ios::binary | std::ios::trunc); g_bp.file_bytes = 0; }
        else g_bp.file_bytes = ec ? 0 : static_cast<size_t>(sz);
    }

    // WHAT: bp_dump command: one station dump now, allowed while the probe is not armed.
    std::wstring bp_dump_cmd()
    {
        bp_file_init();
        g_bp.arr_lim = 1000;
        bp_out(L"=== STATION DUMP");
        BpDumpCtx c;
        const bool ok = seh_run(bp_station_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT station dump " + seh_describe(g_seh));
        bp_bpdump_all();
        { BpDumpCtx d; if (!seh_run(bp_designer_fn, &d)) bp_out(L"FAULT designer dump " + seh_describe(g_seh)); bp_out(d.out); }
        g_bp.eh_sig.clear(); bp_edit_run();
        { BpSnap sn; if (bp_snap(sn)) bp_inv_full(sn, L"bp_dump"); }
        g_bp.arr_lim = 64;
        bp_flush();
        return ok ? STR("ok dump written to BPProbe.txt") : STR("error: station dump fault");
    }

    // WHAT: bp_arm: file check (bp_file_init), arms the hooks (rescan), next baseline now.
    std::wstring bp_arm_cmd()
    {
        bp_file_init();
        g_bp.full = false;
        g_bp.armed = true;
        bp_out(L"=== bp_arm");
        const int n = bp_scan_hooks();
        if (n < 0)
        {
            g_bp.armed = false;
            bp_out(L"error: controller not ready");
            bp_flush();
            return STR("error: controller not ready");
        }
        g_bp.eh_sig.clear(); g_bp.next_eh = 0; g_bp.next_des = GetTickCount64() + 5000; g_bp.next_stat = GetTickCount64() + 60000;
        g_bp.next_base = 0;
        g_bp.next_skip_log = GetTickCount64() + 60000;
        g_bp.hitlog_n = 0; g_bp.hitlog_ms = 0;
        return L"ok hooks=" + std::to_wstring(g_bp.hooks.size());
    }

    // ===== Probe 3: bp_sig (read only) and bp_place (WRITES: one build command, only when the user types it) =====
    const wchar_t* const kBpSigWords[] = { L"build", L"place", L"machine", L"belt", L"conveyor", L"command" };
    constexpr size_t kBpSigWordsN = sizeof(kBpSigWords) / sizeof(kBpSigWords[0]);

    // WHAT: property lines of a struct / class chain, struct (and array-of-struct) members recursively. seen = structs on the current path (stops cycles).
    void bp_layout(std::wstring& s, UStruct* st, const std::wstring& ind, int depth, std::set<UStruct*>& seen, bool own_only = false)
    {
        if (!st || depth > 6) return;
        if (!seen.insert(st).second) { s += ind + L"(recursive " + st->GetName() + L")\n"; return; }
        for (UStruct* u = st; u && u->GetName() != STR("Object"); u = own_only ? nullptr : u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                s += ind + p->GetName() + L" [" + type_text(p) + L"] off=" + std::to_wstring(p->GetOffset_Internal()) + L" size=" + std::to_wstring(p->GetSize()) + L"\n";
                FProperty* q = p;
                if (auto* ap = CastField<FArrayProperty>(p)) q = ap->GetInner();
                if (auto* sp = CastField<FStructProperty>(q)) bp_layout(s, sp->GetStruct(), ind + L"    ", depth + 1, seen);
            }
        seen.erase(st);
    }

    // WHAT: one function signature: every property with name, type, offset, size, dir (in/out/return/local), flags; struct layouts below.
    void bp_sig_one(std::wstring& s, UFunction* f)
    {
        s += L"SIG FN " + f->GetName() + L" ParmsSize=" + std::to_wstring(f->GetParmsSize()) + L"\n";
        for (FProperty* p : f->ForEachProperty())
        {
            const bool out = p->HasAnyPropertyFlags(CPF_OutParm), rv = p->HasAnyPropertyFlags(CPF_ReturnParm);
            const wchar_t* dir = rv ? L"return" : out ? L"out" : p->HasAnyPropertyFlags(CPF_Parm) ? L"in" : L"local";
            wchar_t fl[32];
            swprintf(fl, 32, L"0x%llx", static_cast<unsigned long long>(p->GetPropertyFlags()));
            s += L"  PARM " + p->GetName() + L" [" + type_text(p) + L"] off=" + std::to_wstring(p->GetOffset_Internal()) + L" size=" + std::to_wstring(p->GetSize()) + L" dir=" + dir + L" flags=" + fl + L"\n";
            FProperty* q = p;
            if (auto* ap = CastField<FArrayProperty>(p)) q = ap->GetInner();
            if (auto* sp = CastField<FStructProperty>(q)) { std::set<UStruct*> seen; bp_layout(s, sp->GetStruct(), L"      ", 0, seen); }
        }
    }

    void bp_sig_fn(void* p)
    {
        std::wstring& s = static_cast<BpDumpCtx*>(p)->out;
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { s += L"SIG controller not ready\n"; return; }
        for (const wchar_t* n : { L"AddBuildMachineCommand", L"AddBuildCommand", L"AddBlueprintBuildCommand", L"AddBuildDecorationCommand",
                                  L"AddConfigureMachineCommand", L"AddDisassembleCommand", L"EnterMachineBuildMode", L"ConfirmBuild" })
        {
            UFunction* f = ctrl->GetFunctionByNameInChain(n);
            if (!f) s += L"SIG FN " + std::wstring(n) + L" missing\n"; else bp_sig_one(s, f);
        }
        s += L"SIG --- every matching function of the controller chain\n";
        std::set<std::wstring> done;
        for (UStruct* u = ctrl->GetClassPrivate(); u; u = u->GetSuperStruct())
        {
            if (u->GetFullName().find(L"/Script/Engine.") != std::wstring::npos) break;
            if (UClass* uc = Cast<UClass>(u))
                for (UFunction* f : uc->ForEachFunction())
                    if (bp_has(f->GetName(), kBpSigWords, kBpSigWordsN) && done.insert(f->GetName()).second) bp_sig_one(s, f);
        }
        s += L"SIG --- classes / structs with 'build' in the name\n";
        int shown = 0;
        for (const wchar_t* kind : { L"Class", L"ScriptStruct" })
        {
            std::vector<UObject*> v;
            UObjectGlobals::FindAllOf(kind, v);
            for (UObject* o : v)
            {
                if (shown >= 80) break;
                if (!alive(o)) continue;
                UStruct* us = Cast<UStruct>(o);
                if (!us) continue;
                const std::wstring n = us->GetName(), ln = lower(n);
                if (ln.find(L"build") == std::wstring::npos || ln.find(L"widget") != std::wstring::npos || n.rfind(L"SKEL_", 0) == 0 || n.rfind(L"REINST_", 0) == 0) continue;
                ++shown;
                s += L"SIG TYPE " + std::wstring(kind) + L" " + n + L" super=" + (us->GetSuperStruct() ? us->GetSuperStruct()->GetName() : L"none") + L"\n";
                std::set<UStruct*> seen;
                bp_layout(s, us, L"  ", 0, seen);
            }
        }
    }

    // WHAT: bp_sig command: writes all signatures to BPProbe.txt. Read only.
    std::wstring bp_sig_cmd()
    {
        bp_file_init();
        bp_out(L"=== bp_sig");
        BpDumpCtx c;
        const bool ok = seh_run(bp_sig_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT bp_sig " + seh_describe(g_seh));
        bp_flush();
        return ok ? STR("ok signatures written to BPProbe.txt") : STR("error: bp_sig fault");
    }

    // WHAT: values for bp_place and counters of how many reflected slots they landed in.
    struct BpPlaceArgs { UClass* cls = nullptr; UObject* bdat = nullptr; int x = 0, y = 0, h = 0, rot = 0, pinv = 0, sinv = 0, sid = 0; int hit_cls = 0, hit_xy = 0; std::wstring used; };

    // WHAT: fills one parameter (recursing through structs) by lowercase property path. Unknown slots stay zero. Numbers go into int32/int64/float/double/byte.
    void bp_fill(FProperty* p, uint8_t* d, const std::wstring& path, BpPlaceArgs& a)
    {
        if (auto* sp = CastField<FStructProperty>(p))
        {
            if (!sp->GetStruct()) return;
            for (FProperty* sub : sp->GetStruct()->ForEachProperty()) bp_fill(sub, d + sub->GetOffset_Internal(), path + L"." + lower(sub->GetName()), a);
            return;
        }
        const size_t dot = path.rfind(L'.');
        const std::wstring leaf = dot == std::wstring::npos ? path : path.substr(dot + 1);
        auto has = [&](const wchar_t* w) { return leaf.find(w) != std::wstring::npos; };
        auto num = [&](int64_t v) -> bool
        {
            if (CastField<FIntProperty>(p)) *reinterpret_cast<int32_t*>(d) = static_cast<int32_t>(v);
            else if (CastField<FInt64Property>(p)) *reinterpret_cast<int64_t*>(d) = v;
            else if (CastField<FFloatProperty>(p)) *reinterpret_cast<float*>(d) = static_cast<float>(v);
            else if (CastField<FDoubleProperty>(p)) *reinterpret_cast<double*>(d) = static_cast<double>(v);
            else if (CastField<FByteProperty>(p) && p->GetSize() == 1) *d = static_cast<uint8_t>(v);
            else return false;
            a.used += path + L"=" + i64w(v) + L"; ";
            return true;
        };
        if (CastField<FObjectProperty>(p))
        {
            if (a.bdat && has(L"buildable")) { *reinterpret_cast<UObject**>(d) = a.bdat; a.used += path + L"=asset:" + a.bdat->GetName() + L"; "; ++a.hit_cls; }
            else if (has(L"class") || has(L"buildable")) { *reinterpret_cast<UObject**>(d) = a.cls; a.used += path + L"=class:" + a.cls->GetName() + L"; "; ++a.hit_cls; }
            return;
        }
        if (leaf == L"value" && path.find(L"quat.w") != std::wstring::npos) { num(65536); return; }   // identity quaternion (fixed point 1.0)
        if ((leaf == L"x" || leaf == L"y") && path.find(L"grid") != std::wstring::npos) { if (num(leaf == L"x" ? a.x : a.y)) ++a.hit_xy; return; }
        if (has(L"height")) { num(a.h); return; }
        if (has(L"rotation") || leaf == L"rot") { num(a.rot); return; }
        if (has(L"inventory")) { if (has(L"player")) num(a.pinv); else if (has(L"primary") || has(L"station") || has(L"building")) num(a.sinv); return; }
        if (has(L"station") || has(L"receiver") || has(L"attach")) num(a.sid);
    }

    // WHAT: state of the running place test (diff checks at +1.5 s and +5 s). Ticked by bp_place_tick.
    struct BpPlaceRun { bool pending = false; ULONGLONG t0 = 0; int stage = 0; BpSnap ref; };
    BpPlaceRun g_bpp;

    struct BpPlaceCtx { std::vector<std::string> tok; std::wstring out; bool called = false; };

    // WHAT: bp_place body under seh_run. tok = {bp_place, fn, class, x, y, h, rot, [stationId]}. Refuses (out = "REFUSE ...") instead of guessing.
    void bp_place_fn(void* p)
    {
        auto* c = static_cast<BpPlaceCtx*>(p);
        std::wstring& s = c->out;
        const std::wstring fname = widen(c->tok[1]), cname = widen(c->tok[2]);
        if (fname != L"AddBuildMachineCommand" && fname != L"AddBuildCommand" && fname != L"AddBlueprintBuildCommand" && fname != L"AddBuildDecorationCommand")
        { s += L"REFUSE function not allowed: " + fname + L"\n"; return; }
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { s += L"REFUSE controller not ready\n"; return; }
        UFunction* f = ctrl->GetFunctionByNameInChain(fname.c_str());
        if (!f) { s += L"REFUSE function missing: " + fname + L"\n"; return; }
        BpPlaceArgs a;
        a.x = std::atoi(c->tok[3].c_str()); a.y = std::atoi(c->tok[4].c_str()); a.h = std::atoi(c->tok[5].c_str()); a.rot = std::atoi(c->tok[6].c_str());
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
        if (cname.rfind(L"DAT_", 0) == 0)
        {
            std::vector<UObject*> bs; UObjectGlobals::FindAllOf(STR("Buildable"), bs);
            for (UObject* o : bs) if (alive(o) && o->GetName() == cname) { a.bdat = o; break; }
            if (!a.bdat) { s += L"REFUSE no Buildable asset " + cname + L"\n"; return; }
            a.cls = a.bdat->GetClassPrivate();
        }
        else
        for (UObject* o : all)
            if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && o->GetClassPrivate()->GetName() == cname) { a.cls = o->GetClassPrivate(); break; }
        if (!a.cls) { s += L"REFUSE no live instance of class " + cname + L" (cannot get its UClass)\n"; return; }
        std::vector<UObject*> sts;
        UObjectGlobals::FindAllOf(STR("StationObject"), sts);
        UObject* st = nullptr; int nst = 0;
        const int want = c->tok.size() > 7 ? std::atoi(c->tok[7].c_str()) : 0;
        for (UObject* o : sts)
            if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(o) > 0 && (want == 0 || obj_id(o) == want)) { if (!st) st = o; ++nst; }
        if (!st) { s += L"REFUSE no station found\n"; return; }
        if (nst > 1) { s += L"REFUSE " + std::to_wstring(nst) + L" stations, give the station id as the 7th argument\n"; return; }
        a.sid = obj_id(st);
        int32_t iv = 0;
        if (get_int_prop(st, STR("InventoryID"), iv)) a.sinv = iv;
        if (UObject* pl = first_of(STR("DMSPlayerObject")))
            if (alive(pl)) { UObject* pi = get_obj_prop(pl, STR("Inventory")); if (alive(pi)) a.pinv = obj_id(pi); else if (get_int_prop(pl, STR("InventoryID"), iv)) a.pinv = iv; }
        std::vector<uint8_t> buf(f->GetParmsSize() + 16, 0);
        for (FProperty* pr : f->ForEachProperty())
        {
            if (!pr->HasAnyPropertyFlags(CPF_Parm) || pr->HasAnyPropertyFlags(CPF_ReturnParm) || (pr->HasAnyPropertyFlags(CPF_OutParm) && fname != L"AddBuildCommand")) continue;
            if (static_cast<size_t>(pr->GetOffset_Internal()) + static_cast<size_t>(pr->GetSize()) > static_cast<size_t>(f->GetParmsSize())) { s += L"REFUSE param out of range: " + pr->GetName() + L"\n"; return; }
            bp_fill(pr, buf.data() + pr->GetOffset_Internal(), lower(pr->GetName()), a);
        }
        s += L"PLACE fn=" + fname + L" station=" + std::to_wstring(a.sid) + L" sinv=" + std::to_wstring(a.sinv) + L" pinv=" + std::to_wstring(a.pinv) + L"\n";
        s += L"PLACE filled: " + a.used + L"\n";
        if (a.hit_cls < 1 || a.hit_xy < 2) { s += L"REFUSE signature not as expected (class slots=" + std::to_wstring(a.hit_cls) + L", x/y slots=" + std::to_wstring(a.hit_xy) + L"); see bp_sig\n"; return; }
        s += L"PLACE param block before call:\n";
        bp_dump(s, buf.data(), f, nullptr, f->GetParmsSize());
        ctrl->ProcessEvent(f, buf.data());
        c->called = true;
        s += L"PLACE called; param block after call (return/out values):\n";
        bp_dump(s, buf.data(), f, nullptr, f->GetParmsSize());
    }

    // WHAT: bp_place <fn> <class> <x> <y> <h> <rot> [stationId]. Writes (one command). Only on user command.
    std::wstring bp_place_cmd(const std::string& cmd)
    {
        BpPlaceCtx c;
        { std::istringstream is(cmd); std::string t; while (is >> t) c.tok.push_back(t); }
        if (c.tok.size() < 7) return STR("error: usage bp_place <fn> <class> <x> <y> <h> <rot> [stationId]");
        if (g_bpp.pending) return STR("error: previous bp_place still being checked, wait 5 s");
        bp_file_init();
        bp_out(L"=== bp_place " + widen(cmd));
        BpSnap before;
        if (!bp_snap(before)) { bp_out(L"REFUSE before snapshot failed"); bp_flush(); return STR("error: snapshot failed"); }
        bp_inv_full(before, L"BEFORE place");
        const bool ok = seh_run(bp_place_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT bp_place " + seh_describe(g_seh));
        if (c.called) { g_bpp.pending = true; g_bpp.t0 = GetTickCount64(); g_bpp.stage = 0; g_bpp.ref = before; }
        bp_flush();
        return !ok ? STR("error: bp_place fault (see BPProbe.txt)") : c.called ? STR("ok called, diffs follow in BPProbe.txt") : STR("refused, see BPProbe.txt");
    }

    // WHAT: +1.5 s and +5 s inventory / MACHINE diff after a bp_place call. Runs from bp_tick even when the probe is not armed.
    void bp_place_tick()
    {
        if (!g_bpp.pending) return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_bpp.t0 + (g_bpp.stage == 0 ? 1500 : 5000)) return;
        BpSnap s;
        const bool ok = bp_snap(s);
        bp_out(std::wstring(L"PLACE SNAPSHOT +") + (g_bpp.stage == 0 ? L"1.5s" : L"5s"));
        if (ok) { bp_diff(g_bpp.ref, s); bp_inv_full(s, L"AFTER place"); }
        if (g_bpp.stage == 0) g_bpp.stage = 1; else g_bpp.pending = false;
        bp_flush();
    }

    // ===== Probe 4: bp_cls (read only), bp_send (WRITES one command, only on user command) =====
    bool bp_is_input(UStruct* s) { for (UStruct* u = s; u; u = u->GetSuperStruct()) if (u->GetName() == STR("InputCommand")) return true; return false; }

    void bp_cls_fn(void* p)
    {
        std::wstring& s = static_cast<BpDumpCtx*>(p)->out;
        std::vector<UObject*> all;
        UObjectGlobals::ForEachUObject([&](UObject* o, int32_t, int32_t) { if (alive(o) && Cast<UClass>(o)) all.push_back(o); return LoopAction::Continue; });
        s += L"CLS total classes " + std::to_wstring(all.size()) + L"\n";
        int n = 0;
        for (UObject* o : all)
        {
            UClass* c = Cast<UClass>(o);
            if (!c) continue;
            const std::wstring nm = c->GetName(), ln = lower(nm);
            if (nm.rfind(L"SKEL_",0)==0 || nm.rfind(L"REINST_",0)==0 || ln.find(L"widget")!=std::wstring::npos) continue;
            const bool inp = bp_is_input(c);
            if (!inp && ln.find(L"build")==std::wstring::npos && ln.find(L"place")==std::wstring::npos && ln.find(L"machine")==std::wstring::npos && ln.find(L"blueprint")==std::wstring::npos) continue;
            if (++n > 250) { s += L"CLS LIMIT 250\n"; break; }
            s += L"CLS " + nm + (inp ? L" [InputCommand]" : L"") + L" super=" + (c->GetSuperStruct() ? c->GetSuperStruct()->GetName() : L"none") + L"\n";
            std::set<UStruct*> seen; bp_layout(s, c, L"  ", 0, seen);
            for (UFunction* f : c->ForEachFunction()) bp_sig_one(s, f);
        }
        s += L"CLS --- station/frame/machine functions taking an object param named like build/apply/place/process fn\n";
        for (const wchar_t* cn : { L"StationObject", L"BuildingFrame", L"MachineObject" })
        {
            UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (std::wstring(L"/Script/MainSequence.") + cn).c_str());
            if (!c) { s += L"CLS missing " + std::wstring(cn) + L"\n"; continue; }
            s += L"CLSFN class " + std::wstring(cn) + L"\n";
            for (UStruct* u = c; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
                if (UClass* uc = Cast<UClass>(u))
                    for (UFunction* f : uc->ForEachFunction())
                    {
                        bool objp = false;
                        for (FProperty* pr : f->ForEachProperty()) if (CastField<FObjectProperty>(pr) && pr->HasAnyPropertyFlags(CPF_Parm) && !pr->HasAnyPropertyFlags(CPF_ReturnParm)) objp = true;
                        const std::wstring ln = lower(f->GetName());
                        if (objp && (ln.find(L"build")!=std::wstring::npos || ln.find(L"apply")!=std::wstring::npos || ln.find(L"place")!=std::wstring::npos || ln.find(L"process")!=std::wstring::npos || ln.find(L"try")!=std::wstring::npos || ln.find(L"machine")!=std::wstring::npos || ln.find(L"command")!=std::wstring::npos || ln.find(L"input")!=std::wstring::npos))
                            bp_sig_one(s, f);
                    }
        }
    }

    // WHAT: bp_cls command: writes the InputCommand / build class layouts and the station-side function signatures. Read only.
    std::wstring bp_cls_cmd()
    {
        bp_file_init(); bp_out(L"=== bp_cls");
        BpDumpCtx c; const bool ok = seh_run(bp_cls_fn, &c);
        bp_out(c.out); if (!ok) bp_out(L"FAULT bp_cls " + seh_describe(g_seh));
        bp_flush();
        return ok ? STR("ok bp_cls written to BPProbe.txt") : STR("error: bp_cls fault");
    }

    // ===== Probe 5: bp_cd (read only): named classes, own properties and own functions up the super chain =====
    const wchar_t* const kBpCdStop[] = { L"Object", L"Actor", L"SceneComponent", L"ActorComponent", L"PrimitiveComponent", L"MeshComponent", L"StaticMeshComponent", L"Pawn" };
    struct BpCdCtx { std::vector<std::string> names; std::wstring out; };

    // WHAT: one class: CD header with super chain (to Object), then per class up the chain until a stop class: own props (bp_layout, own only) and every own function.
    void bp_cd_one(std::wstring& s, UClass* c)
    {
        std::wstring chain;
        for (UStruct* u = c->GetSuperStruct(); u; u = u->GetSuperStruct())
        {
            chain += (chain.empty() ? L"" : L">") + u->GetName();
            if (u->GetName() == L"Object") break;
        }
        s += L"CD class " + c->GetName() + L" super=" + (chain.empty() ? std::wstring(L"none") : chain) + L"\n";
        for (UStruct* u = c; u; u = u->GetSuperStruct())
        {
            const std::wstring nm = u->GetName();
            bool stop = false;
            for (const wchar_t* sn : kBpCdStop) if (nm == sn) stop = true;
            UClass* uc = Cast<UClass>(u);
            if (stop || !uc) break;
            s += L"CD ---- " + nm + L"\n";
            std::set<UStruct*> seen; bp_layout(s, u, L"  ", 2, seen, true);
            for (UFunction* f : uc->ForEachFunction()) bp_sig_one(s, f);
        }
    }

    // WHAT: bp_cd body under seh_run. One pass over all classes; each name is an exact GetName() or contains:xxx (lowercase substring). SKEL_/REINST_ skipped.
    void bp_cd_fn(void* p)
    {
        auto* c = static_cast<BpCdCtx*>(p);
        std::wstring& s = c->out;
        std::vector<UClass*> all;
        UObjectGlobals::ForEachUObject([&](UObject* o, int32_t, int32_t) { if (alive(o) && Cast<UClass>(o)) all.push_back(Cast<UClass>(o)); return LoopAction::Continue; });
        std::set<UClass*> done;
        for (const std::string& tok : c->names)
        {
            const std::wstring want = widen(tok);
            const bool sub = want.rfind(L"contains:", 0) == 0;
            const std::wstring key = sub ? lower(want.substr(9)) : want;
            int matched = 0;
            for (UClass* uc : all)
            {
                const std::wstring nm = uc->GetName();
                if (nm.rfind(L"SKEL_", 0) == 0 || nm.rfind(L"REINST_", 0) == 0) continue;
                if (sub ? lower(nm).find(key) == std::wstring::npos : nm != key) continue;
                ++matched;
                if (done.insert(uc).second) bp_cd_one(s, uc);
            }
            if (!matched) s += L"CD no class match: " + want + L"\n";
        }
    }

    // WHAT: bp_cd <ClassName> [ClassName...] command: writes the class blocks to BPProbe.txt. Read only.
    std::wstring bp_cd_cmd(const std::string& cmd)
    {
        BpCdCtx c;
        { std::istringstream is(cmd); std::string t; is >> t; while (is >> t) c.names.push_back(t); }
        if (c.names.empty()) return STR("error: usage bp_cd <ClassName> [ClassName...] or contains:xxx");
        bp_file_init(); bp_out(L"=== bp_cd " + widen(cmd));
        const bool ok = seh_run(bp_cd_fn, &c);
        bp_out(c.out); if (!ok) bp_out(L"FAULT bp_cd " + seh_describe(g_seh));
        bp_flush();
        return ok ? STR("ok bp_cd written to BPProbe.txt") : STR("error: bp_cd fault");
    }

    // WHAT: bp_bld body under seh_run: every live BuilderBase / MachineBuilder / ConveyorBuilder instance (full dump), then the MachineBuilder layout.
    void bp_bld_fn(void* p)
    {
        std::wstring& s = static_cast<BpDumpCtx*>(p)->out;
        std::set<UObject*> seen;
        for (const wchar_t* cn : { L"BuilderBase", L"MachineBuilder", L"ConveyorBuilder" })
        {
            std::vector<UObject*> v;
            UObjectGlobals::FindAllOf(cn, v);
            for (UObject* o : v)
            {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0 || !seen.insert(o).second) continue;
                s += L"BLD " + o->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(o)) + L"\n";
                bp_dump(s, reinterpret_cast<const uint8_t*>(o), o->GetClassPrivate(), nullptr);
            }
        }
        UClass* mc = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/MainSequence.MachineBuilder");
        if (!alive(mc)) { s += L"BLD LAYOUT MachineBuilder missing\n"; return; }
        s += L"BLD LAYOUT MachineBuilder (every property, super chain included)\n";
        std::set<UStruct*> seenst;
        bp_layout(s, mc, L"  ", 0, seenst);
    }

    // WHAT: bp_bld command: builder instances and the MachineBuilder layout into BPProbe.txt. Read only.
    std::wstring bp_bld_cmd()
    {
        bp_file_init();
        g_bp.arr_lim = 1000;
        bp_out(L"=== bp_bld");
        BpDumpCtx c;
        const bool ok = seh_run(bp_bld_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT bp_bld " + seh_describe(g_seh));
        g_bp.arr_lim = 64;
        bp_flush();
        return ok ? STR("ok bp_bld written to BPProbe.txt") : STR("error: bp_bld fault");
    }

    // WHAT: hex text (pairs of 0-9 a-f A-F, no separators) to bytes. false when the length is odd or a character is not hex. Empty text = no bytes.
    bool bp_hex_bytes(const std::wstring& h, std::vector<uint8_t>& out)
    {
        out.clear();
        if (h.size() % 2) return false;
        for (size_t i = 0; i < h.size(); i += 2)
        {
            int v = 0;
            for (size_t k = i; k < i + 2; ++k)
            {
                const wchar_t c = h[k];
                const int d = (c >= L'0' && c <= L'9') ? c - L'0' : (c >= L'a' && c <= L'f') ? c - L'a' + 10 : (c >= L'A' && c <= L'F') ? c - L'A' + 10 : -1;
                if (d < 0) return false;
                v = v * 16 + d;
            }
            out.push_back(static_cast<uint8_t>(v));
        }
        return true;
    }
    // WHAT: raw layout of a UE TArray (data pointer, ArrayNum, ArrayMax). Used only to write a TArray<uint8_t> field of a fresh object.
    struct BpRawArr { uint8_t* data; int32_t num; int32_t max; };
    static_assert(sizeof(BpRawArr) == sizeof(TArray<uint8_t>), "TArray layout is not {ptr, num, max}; fix BpRawArr");

    // WHAT: values for bp_send: tokens, key=value map (lowercase keys), keys consumed by a factory, output, called flag.
    struct BpSend { std::vector<std::string> tok; std::map<std::wstring, std::wstring> kv; std::set<std::wstring> used; std::wstring out; bool called = false; };

    UObject* pp_recipe_obj(const std::string& name);   // defined with the paste recipe helpers (below)
    // WHAT: first segment of a dotted lowercase key ("command.gridlocation.x" -> "command").
    std::wstring bp_head(const std::wstring& path) { return path.substr(0, path.find(L'.')); }

    // WHAT: sets one leaf by dotted lowercase path inside a container (struct, class, function parm block). Numbers, bool, object (null or class by name).
    //       Anything else (enum, string, array) -> false. Appends "path=val" to out on success. false when nothing matched.
    bool bp_setpath(UStruct* st, uint8_t* base, const std::wstring& path, const std::wstring& val, std::wstring& out)
    {
        if (!st || !base) return false;
        const size_t dot = path.find(L'.');
        const std::wstring head = path.substr(0, dot), tail = dot == std::wstring::npos ? L"" : path.substr(dot + 1);
        for (UStruct* u = st; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                if (lower(p->GetName()) != head) continue;
                uint8_t* d = base + p->GetOffset_Internal();
                if (!tail.empty())
                {
                    auto* sp = CastField<FStructProperty>(p);
                    if (!sp || !sp->GetStruct()) return false;
                    return bp_setpath(sp->GetStruct(), d, tail, val, out);
                }
                bool ok = false;
                if (auto* bp = CastField<FBoolProperty>(p)) { bp->SetPropertyValueInContainer(base, val == L"1" || val == L"true"); ok = true; }
                else if (CastField<FIntProperty>(p)) { *reinterpret_cast<int32_t*>(d) = _wtoi(val.c_str()); ok = true; }
                else if (CastField<FInt64Property>(p)) { *reinterpret_cast<int64_t*>(d) = _wtoi64(val.c_str()); ok = true; }
                else if (CastField<FFloatProperty>(p)) { *reinterpret_cast<float*>(d) = static_cast<float>(_wtof(val.c_str())); ok = true; }
                else if (CastField<FDoubleProperty>(p)) { *reinterpret_cast<double*>(d) = _wtof(val.c_str()); ok = true; }
                else if (CastField<FByteProperty>(p) && p->GetSize() == 1) { *d = static_cast<uint8_t>(_wtoi(val.c_str())); ok = true; }
                else if (CastField<FObjectProperty>(p))
                {
                    if (val == L"null") { *reinterpret_cast<UObject**>(d) = nullptr; ok = true; }
                    else if (CastField<FObjectProperty>(p)->GetPropertyClass() && CastField<FObjectProperty>(p)->GetPropertyClass()->GetName() == STR("Recipe"))
                    {
                        // WHY: SetRecipeInput.Recipe = the live Recipe asset found by its Name (vault rea-machine-config.md s5)
                        if (UObject* r = pp_recipe_obj(to_utf8(val))) { *reinterpret_cast<UObject**>(d) = r; ok = true; }
                    }
                    else
                    {
                        UObject* cls = nullptr;
                        if (val.rfind(L"DAT_", 0) == 0)
                        {
                            std::vector<UObject*> bs; UObjectGlobals::FindAllOf(STR("Buildable"), bs);
                            for (UObject* o : bs) if (alive(o) && o->GetName() == val) { *reinterpret_cast<UObject**>(d) = o; ok = true; break; }
                        }
                        std::vector<UObject*> all; if (!ok) UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
                        for (UObject* o : all)
                            if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && o->GetClassPrivate()->GetName() == val) { cls = o->GetClassPrivate(); break; }
                        if (cls) { *reinterpret_cast<UObject**>(d) = cls; ok = true; }
                    }
                }
                else if (auto* ap = CastField<FArrayProperty>(p))
                {
                    // WHY: BuildInput.MachineConfigData (TArray<uint8>) carries the SerializedMachineConfig bytes; hex text in, bytes out.
                    if (!CastField<FByteProperty>(ap->GetInner()) || ap->GetInner()->GetSize() != 1) return false;
                    std::vector<uint8_t> bytes;
                    if (!bp_hex_bytes(val, bytes)) { out += L"REFUSE hex for " + path + L" (even length, 0-9 a-f only)\n"; return false; }
                    auto* ra = reinterpret_cast<BpRawArr*>(d);
                    if (ra->data) { out += L"REFUSE " + path + L" already holds data\n"; return false; }
                    if (!bytes.empty())
                    {
                        // ASSUMPTION: the engine frees TArray<uint8> data with FMemory::Free, and FMemory::Malloc is the same GMalloc (RC::Unreal).
                        // The buffer then belongs to the command object and is freed with it. Nothing is freed here.
                        auto* mem = static_cast<uint8_t*>(FMemory::Malloc(bytes.size()));
                        memcpy(mem, bytes.data(), bytes.size());
                        ra->data = mem;
                        ra->num = ra->max = static_cast<int32_t>(bytes.size());
                    }
                    ok = true;
                }
                if (ok) out += path + L"=" + val + L"\n";
                return ok;
            }
        return false;
    }

    // WHAT: bp_send body under seh_run. tok = {bp_send, InputClass, receiverId, k=v...}. Refuses (out = "REFUSE ...") instead of guessing.
    void bp_send_fn(void* p)
    {
        auto* c = static_cast<BpSend*>(p);
        std::wstring& s = c->out;
        if (g_bpp.pending) { s += L"REFUSE previous check still running\n"; return; }
        if (c->tok.size() < 3) { s += L"REFUSE usage: bp_send <InputClass> <receiverId> k=v...\n"; return; }
        for (size_t i = 3; i < c->tok.size(); ++i)
        {
            const size_t eq = c->tok[i].find('=');
            if (eq == std::string::npos || eq == 0) { s += L"REFUSE token without '=': " + widen(c->tok[i]) + L"\n"; return; }
            c->kv[lower(widen(c->tok[i].substr(0, eq)))] = widen(c->tok[i].substr(eq + 1));
        }
        const int receiver = std::atoi(c->tok[2].c_str());
        const std::wstring cname = widen(c->tok[1]);
        // WHY: the native flush drops commands whose cmd+0x28 type id is 0 (same as warp, mb_warpreal.hpp); BuildInput 0x2e, DisassembleInput 0x2f (rea-placement.md). Both go to receiver 0.
        const int32_t tid = cname == L"BuildInput" ? 0x2e : cname == L"DisassembleInput" ? 0x2f : cname == L"MachinePortInput" ? 17 : cname == L"SetRecipeInput" ? 20 : 0;   // ids 17/20: vault rea-machine-config.md s4
        if (receiver < 0 || (receiver == 0 && !tid)) { s += L"REFUSE receiver id must be > 0 (0 only for BuildInput/DisassembleInput)\n"; return; }
        UClass* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (std::wstring(L"/Script/MainSequence.") + cname).c_str());
        if (!alive(cls) || !bp_is_input(cls)) { s += L"REFUSE not a live InputCommand class: " + cname + L"\n"; return; }
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { s += L"REFUSE controller not ready\n"; return; }
        UFunction* af = ctrl->GetFunctionByNameInChain(STR("AddInputCommand"));
        if (!af) { s += L"REFUSE AddInputCommand missing\n"; return; }
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
        if (receiver > 0)
        {
            UObject* recv = nullptr;
            for (UObject* o : all) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(o) == receiver) { recv = o; break; }
            if (!recv) { s += L"REFUSE no live SaveableObject with id " + std::to_wstring(receiver) + L"\n"; return; }
            s += L"RECV " + recv->GetClassPrivate()->GetName() + L"#" + std::to_wstring(receiver) + L"\n";
        }

        // (a) factory on the class (CreateInput*), (b) else StaticConstructObject
        UObject* cmdo = nullptr;
        for (UFunction* f : cls->ForEachFunction())
        {
            if (cmdo || f->GetName().rfind(STR("CreateInput"), 0) != 0) continue;
            FProperty* rp = nullptr;
            for (FProperty* pr : f->ForEachProperty()) if (pr->HasAnyPropertyFlags(CPF_ReturnParm)) rp = pr;
            if (!rp || !CastField<FObjectProperty>(rp)) continue;
            std::vector<uint8_t> buf(f->GetParmsSize() + 16, 0);
            for (FProperty* pr : f->ForEachProperty())
            {
                if (!pr->HasAnyPropertyFlags(CPF_Parm) || pr->HasAnyPropertyFlags(CPF_ReturnParm) || pr->HasAnyPropertyFlags(CPF_OutParm)) continue;
                const std::wstring pn = lower(pr->GetName());
                for (auto& kv : c->kv)
                    if (bp_head(kv.first) == pn)
                    {
                        if (!bp_setpath(f, buf.data(), kv.first, kv.second, s)) { s += L"REFUSE factory field " + kv.first + L"\n"; return; }
                        c->used.insert(kv.first);
                    }
            }
            UObject* cdo = cls->GetClassDefaultObject();
            if (!alive(cdo)) { s += L"REFUSE class default object not ready\n"; return; }
            cdo->ProcessEvent(f, buf.data());
            UObject* made = *reinterpret_cast<UObject* const*>(buf.data() + rp->GetOffset_Internal());
            if (!alive(made)) { s += L"REFUSE factory " + f->GetName() + L" returned no live object\n"; return; }
            cmdo = made;
            s += L"via factory " + f->GetName() + L"\n";
        }
        if (!cmdo)
        {
            cmdo = UObjectGlobals::StaticConstructObject(FStaticConstructObjectParameters(cls, ctrl));
            if (!alive(cmdo)) { s += L"REFUSE StaticConstructObject returned no live object\n"; return; }
            s += L"via StaticConstructObject (no factory)\n";
        }
        for (auto& kv : c->kv)
        {
            if (c->used.count(kv.first)) continue;
            if (!bp_setpath(cmdo->GetClassPrivate(), reinterpret_cast<uint8_t*>(cmdo), kv.first, kv.second, s)) { s += L"REFUSE unknown field " + kv.first + L"\n"; return; }
        }
        if (tid) { *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(cmdo) + 0x28) = tid; s += L"type id cmd+0x28 = " + std::to_wstring(tid) + L"\n"; }
        s += L"SEND command object:\n";
        bp_dump(s, reinterpret_cast<const uint8_t*>(cmdo), cmdo->GetClassPrivate(), nullptr);

        // AddInputCommand(receiver id, command object): parameters found by name
        std::vector<uint8_t> abuf(af->GetParmsSize() + 16, 0);
        FProperty* pr_recv = nullptr; FProperty* pr_cmd = nullptr;
        for (FProperty* pr : af->ForEachProperty())
        {
            if (!pr->HasAnyPropertyFlags(CPF_Parm) || pr->HasAnyPropertyFlags(CPF_ReturnParm) || pr->HasAnyPropertyFlags(CPF_OutParm)) continue;
            const std::wstring pn = lower(pr->GetName());
            if (!pr_recv && pn.find(L"receiver") != std::wstring::npos && CastField<FIntProperty>(pr)) pr_recv = pr;
            else if (!pr_cmd && pn.find(L"command") != std::wstring::npos && CastField<FObjectProperty>(pr)) pr_cmd = pr;
        }
        if (!pr_recv || !pr_cmd) { s += L"REFUSE AddInputCommand receiver/command params not found\n"; return; }
        *reinterpret_cast<int32_t*>(abuf.data() + pr_recv->GetOffset_Internal()) = receiver;
        *reinterpret_cast<UObject**>(abuf.data() + pr_cmd->GetOffset_Internal()) = cmdo;
        ctrl->ProcessEvent(af, abuf.data());
        c->called = true;
        s += L"CALLED AddInputCommand receiver=" + std::to_wstring(receiver) + L" cmd=" + cmdo->GetClassPrivate()->GetName() + L"\n";
    }

    // WHAT: bp_send <InputClass> <receiverId> k=v...: builds one InputCommand and sends it. Writes (one command). Only on user command.
    std::wstring bp_send_cmd(const std::string& cmd)
    {
        BpSend c;
        { std::istringstream is(cmd); std::string t; while (is >> t) c.tok.push_back(t); }
        if (c.tok.size() < 3) return STR("error: usage bp_send <InputClass> <receiverId> k=v...");
        if (g_bpp.pending) return STR("error: previous bp_place/bp_send still being checked, wait 5 s");
        bp_file_init();
        bp_out(L"=== bp_send " + widen(cmd));
        BpSnap before;
        if (!bp_snap(before)) { bp_out(L"REFUSE before snapshot failed"); bp_flush(); return STR("error: snapshot failed"); }
        bp_inv_full(before, L"BEFORE send");
        const bool ok = seh_run(bp_send_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT bp_send " + seh_describe(g_seh));
        if (c.called) { g_bpp.pending = true; g_bpp.t0 = GetTickCount64(); g_bpp.stage = 0; g_bpp.ref = before; }
        bp_flush();
        return !ok ? STR("error: bp_send fault (see BPProbe.txt)") : c.called ? STR("ok called, diffs follow in BPProbe.txt") : STR("refused, see BPProbe.txt");
    }

    // WHAT: bp_belts <attachId> x,y,rot x,y,rot ...: places one DAT_ConveyorBelt per cell in one call (no per-belt wait), one before/after diff.
    // WHY: a 1x1 machine is centred on a tile only when x and y are both odd (GridLocation is in half tiles, site X = 2*cellX + w - 14); even = half-tile offset, so the whole batch is refused.
    std::wstring bp_belts_cmd(const std::string& cmd)
    {
        std::vector<std::string> tok;
        { std::istringstream is(cmd); std::string t; while (is >> t) tok.push_back(t); }
        if (tok.size() < 3) return STR("error: usage bp_belts <attachId> x,y,rot ...");
        std::vector<std::array<int, 3>> cells;
        for (size_t i = 2; i < tok.size(); ++i)
        {
            int x, y, r;
            if (sscanf_s(tok[i].c_str(), "%d,%d,%d", &x, &y, &r) != 3 || !(x & 1) || !(y & 1) || r < 0 || r > 3)
                return STR("error: cell must be x,y,rot with x,y odd and rot 0-3: ") + widen(tok[i]);
            cells.push_back({ x, y, r });
        }
        bp_file_init();
        bp_out(L"=== bp_belts " + widen(cmd));
        BpSnap before;
        if (!bp_snap(before)) { bp_out(L"REFUSE before snapshot failed"); bp_flush(); return STR("error: snapshot failed"); }
        bp_inv_full(before, L"BEFORE belts");
        int sent = 0;
        for (auto& c : cells)
        {
            BpSend s;
            s.tok = { "bp_send", "BuildInput", "0", "buildable=DAT_ConveyorBelt", "attachtargetid=" + tok[1], "gridtransform.gridlocation.x=" + std::to_string(c[0]),
                      "gridtransform.gridlocation.y=" + std::to_string(c[1]), "gridtransform.height=0", "gridtransform.gridrotation=" + std::to_string(c[2]) };
            g_bpp.pending = false;
            const bool ok = seh_run(bp_send_fn, &s);
            if (!ok) bp_out(L"FAULT bp_belts " + seh_describe(g_seh));
            if (s.called) ++sent; else bp_out(s.out);
        }
        g_bpp.pending = true; g_bpp.t0 = GetTickCount64(); g_bpp.stage = 0; g_bpp.ref = before;
        bp_out(L"BELTS sent " + std::to_wstring(sent) + L" of " + std::to_wstring(cells.size()));
        bp_flush();
        return STR("ok bp_belts sent ") + widen(std::to_string(sent));
    }

    // ===== refund space check: bp_kill, bp_run kill_new, bp_paste replace (removals that give ItemCost back to the player) =====
    std::string pp_short(std::string s);                                  // defined with the paste helpers below
    FProperty* pp_field(UStruct* st, const wchar_t* name);
    UObject* pp_obj(UStruct* st, const uint8_t* base, const wchar_t* name);
    // WHAT: one ItemCost entry of a Buildable: item object, its label, amount.
    struct RCostItem { uint64_t item = 0; std::string label; int64_t amount = 0; };
    // WHAT: one item a removal gives back to the player: item object, label, amount, MaxStackSize (1 when unreadable).
    struct RefundItem { uint64_t item = 0; std::string label; int64_t amount = 0; int64_t max = 1; };
    // WHAT: one inventory slot: item object (0 when empty) and amount (<= 0 = empty).
    struct RSlot { uint64_t item = 0; int64_t amount = 0; };
    // WHAT: slot math result: slots the refund needs, empty slots, units that fit in partial stacks, slots missing (0 = fits).
    struct RefundFit { int64_t need = 0, empty = 0, room = 0, missing = 0; };

    // WHAT: pure. Partial stacks of the same item fill first (room = max - amount), the rest needs empty slots (ceil(rest / max)).
    //       need = one entry per item (aggregated). missing = slots needed beyond the empty ones (empty slots are shared by all items).
    // WHY: conservative: max = Item MaxStackSize only (StackMultiplier ignored), so the free space is never over-estimated.
    RefundFit refund_fit(const std::vector<RSlot>& slots, const std::vector<RefundItem>& need)
    {
        RefundFit f;
        for (const RSlot& s : slots) if (s.amount <= 0) ++f.empty;
        for (const RefundItem& n : need)
        {
            if (n.amount <= 0) continue;
            const int64_t mx = std::max<int64_t>(n.max, 1);
            int64_t room = 0;
            for (const RSlot& s : slots) if (s.item == n.item && s.amount > 0 && s.amount < mx) room += mx - s.amount;
            const int64_t rest = std::max<int64_t>(n.amount - room, 0);
            f.room += std::min(room, n.amount);
            f.need += (rest + mx - 1) / mx;
        }
        f.missing = std::max<int64_t>(f.need - f.empty, 0);
        return f;
    }

    // WHAT: ItemCost per machine class (key = pp_short of the Buildable's Class object name). Under seh_run, read only.
    struct RCostCtx { std::map<std::string, std::vector<RCostItem>> by_cls; };
    void refund_cost_fn(void* p)
    {
        auto* c = static_cast<RCostCtx*>(p);
        std::vector<UObject*> bl;
        UObjectGlobals::FindAllOf(STR("Buildable"), bl);
        for (UObject* b : bl)
        {
            if (!alive(b) || b->GetName().rfind(STR("Default__"), 0) == 0) continue;
            UObject* co = pp_obj(b->GetClassPrivate(), reinterpret_cast<const uint8_t*>(b), STR("Class"));
            if (!alive(co)) continue;
            const std::string key = pp_short(to_utf8(co->GetName()));
            if (c->by_cls.count(key)) continue;
            FProperty* f = pp_field(b->GetClassPrivate(), STR("ItemCost"));
            auto* ap = f ? CastField<FArrayProperty>(f) : nullptr;
            auto* sp = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
            if (!sp || !sp->GetStruct()) continue;
            const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(reinterpret_cast<const uint8_t*>(b) + f->GetOffset_Internal());
            const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
            std::vector<RCostItem> cost;
            for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
            {
                const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
                uint64_t item = 0;
                int32_t amt = 0;
                int off = 0;
                if (!stack_read(sp->GetStruct(), e, item, amt, off) || amt <= 0 || item == 0) continue;
                UObject* it = reinterpret_cast<UObject*>(static_cast<uintptr_t>(item));
                cost.push_back(RCostItem{ item, alive(it) ? to_utf8(it->GetName()) : std::string("?"), amt });
            }
            c->by_cls[key] = cost;
        }
    }

    // WHAT: refund of removing the machines (id, short class). Sums the ItemCost of each class per item. err = a class with no Buildable (nothing may be removed).
    // WHY: the refund is the full ItemCost (conservative). An unknown class is refused, never counted as free.
    std::string refund_for(const std::vector<std::pair<int, std::string>>& ms, std::vector<RefundItem>& need)
    {
        need.clear();
        if (ms.empty()) return "";
        RCostCtx c;
        if (!seh_run(refund_cost_fn, &c)) return "refund cost read fault, nothing removed (see BPProbe.txt)";
        for (const auto& m : ms)
        {
            auto it = c.by_cls.find(m.second);
            if (it == c.by_cls.end() && m.second.find("ItemCrate") != std::string::npos)   // WHY: loot box left by an overflowing refund, not a buildable: the player must collect it
                return "an item crate (loot box, id " + std::to_string(m.first) + ") is in the way: pick up its items in the game, then import again. Nothing removed";
            if (it == c.by_cls.end()) return "refund cost unknown for " + m.second + " (id " + std::to_string(m.first) + "), nothing removed";
            for (const RCostItem& ci : it->second)
            {
                auto f = std::find_if(need.begin(), need.end(), [&](const RefundItem& r) { return r.item == ci.item; });
                if (f == need.end()) { need.push_back(RefundItem{ ci.item, ci.label, 0, 1 }); f = need.end() - 1; }
                f->amount += ci.amount;
            }
        }
        return "";
    }

    // WHAT: Inventory.HasSpaceForItems(Items: ItemStack {item, amount}) for one stack. 1 = room, 0 = no room, -1 unusable. Same call as cargo_full (mb_mining.hpp).
    int refund_has_space(UObject* inv, UObject* item, int64_t amount)
    {
        UFunction* fn = inv->GetFunctionByNameInChain(STR("HasSpaceForItems"));
        FProperty* ip = fn ? fn->FindProperty(FName(STR("Items"), FNAME_Add)) : nullptr;
        auto* sp = ip ? CastField<FStructProperty>(ip) : nullptr;
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        auto* rb = rp ? CastField<FBoolProperty>(rp) : nullptr;
        FProperty* fi = (sp && sp->GetStruct()) ? sp->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add)) : nullptr;
        FProperty* fa = (sp && sp->GetStruct()) ? sp->GetStruct()->FindProperty(FName(STR("Amount"), FNAME_Add)) : nullptr;
        if (!rb || !fi || !fa) return -1;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        *reinterpret_cast<UObject**>(buf.data() + ip->GetOffset_Internal() + fi->GetOffset_Internal()) = item;
        *reinterpret_cast<int32_t*>(buf.data() + ip->GetOffset_Internal() + fa->GetOffset_Internal()) = static_cast<int32_t>(std::min<int64_t>(amount, 2000000000));
        inv->ProcessEvent(fn, buf.data());
        return (buf[rp->GetOffset_Internal()] & rb->GetFieldMask()) != 0 ? 1 : 0;
    }

    // WHAT: live read of the refund check under seh_run: the player's ItemSlots, MaxStackSize per need item, and the game's HasSpaceForItems
    //       per need item with its full amount (labels it refuses go to fit_no). slots_ok false = layout not as expected.
    struct RefundCtx { UObject* inv = nullptr; std::vector<RSlot> slots; std::vector<RefundItem>* need = nullptr; bool slots_ok = false; bool fit_used = false; std::string fit_no; };
    void refund_read_fn(void* p)
    {
        auto* c = static_cast<RefundCtx*>(p);
        FProperty* tp = c->inv->GetPropertyByNameInChain(STR("ItemSlots"));
        auto* ap = tp ? CastField<FArrayProperty>(tp) : nullptr;
        auto* arr = ap ? c->inv->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("ItemSlots")) : nullptr;
        auto* sp = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
        if (!arr || !sp || !sp->GetStruct()) return;
        const int es = ap->GetInner()->GetElementSize();
        for (int i = 0; i < arr->Num(); ++i)
        {
            const uint8_t* e = reinterpret_cast<uint8_t*>(arr->GetData()) + static_cast<size_t>(i) * es;
            uint64_t item = 0;
            int32_t amt = 0;
            int off = 0;
            if (!stack_read(sp->GetStruct(), e, item, amt, off)) return;
            c->slots.push_back(RSlot{ item, amt });
        }
        c->slots_ok = true;
        for (RefundItem& n : *c->need)
        {
            UObject* it = reinterpret_cast<UObject*>(static_cast<uintptr_t>(n.item));
            int32_t mx = 0;
            if (alive(it) && get_int_prop(it, STR("MaxStackSize"), mx) && mx > 0) n.max = mx;
            if (n.amount <= 0 || !alive(it)) continue;
            const int r = refund_has_space(c->inv, it, n.amount);
            if (r >= 0) c->fit_used = true;
            if (r == 0) c->fit_no += (c->fit_no.empty() ? "" : ", ") + n.label;
        }
    }

    // WHAT: refund check before a removal that gives `need` to the player inventory. "" = fits (nothing else to do), else the refusal text (nothing may be removed).
    //       Logs 'REFUND need <who>: ... free ...' with the method used (slots + HasSpaceForItems; slots only when that game function is unusable).
    // WHY: a refund that does not fit becomes loot boxes in the world, which the user does not want. Creative players are checked too.
    std::string refund_check(const std::vector<RefundItem>& in, const char* who)
    {
        if (in.empty()) return "";
        std::vector<RefundItem> need = in;
        RefundCtx c;
        c.inv = player_inventory();
        if (!c.inv) return "player inventory cannot be read, nothing removed";
        c.need = &need;
        if (!seh_run(refund_read_fn, &c)) { bp_out(L"FAULT refund check " + seh_describe(g_seh)); return "refund check fault, nothing removed (see BPProbe.txt)"; }
        if (!c.slots_ok) return "player inventory slots cannot be read, nothing removed";
        const RefundFit f = refund_fit(c.slots, need);
        std::string items;
        for (const RefundItem& n : need) items += (items.empty() ? "" : ", ") + n.label + " x" + std::to_string(n.amount);
        const std::string method = c.fit_used ? "slots + HasSpaceForItems" : "slots only (HasSpaceForItems unusable)";
        bp_out(widen(std::string("REFUND need ") + who + ": " + std::to_string(f.need) + " slots (partial stacks take " + std::to_string(f.room) +
                     " units), free empty slots " + std::to_string(f.empty) + ", missing " + std::to_string(f.missing) + ", method " + method +
                     (c.fit_no.empty() ? "" : ", game refused: " + c.fit_no) + "; items " + items));
        if (f.missing == 0 && c.fit_no.empty()) return "";
        return "not enough inventory space for refunds: need " + std::to_string(std::max<int64_t>(f.missing, 1)) + " more slots (items: " + items + ")";
    }

    // WHAT: bp_kill <machineId> ...: disassembles every listed machine in one call (DisassembleInput, receiver 0), one before/after diff. The refund goes to the player inventory.
    //       Refuses (nothing sent) when the refund does not fit the player inventory (refund_check).
    std::wstring bp_kill_cmd(const std::string& cmd)
    {
        std::vector<std::string> tok;
        { std::istringstream is(cmd); std::string t; while (is >> t) tok.push_back(t); }
        if (tok.size() < 2) return STR("error: usage bp_kill <machineId> ...");
        for (size_t i = 1; i < tok.size(); ++i) if (std::atoi(tok[i].c_str()) <= 0) return STR("error: bad id ") + widen(tok[i]);
        bp_file_init();
        bp_out(L"=== bp_kill " + widen(cmd));
        BpSnap before;
        if (!bp_snap(before)) { bp_out(L"REFUSE before snapshot failed"); bp_flush(); return STR("error: snapshot failed"); }
        std::vector<std::pair<int, std::string>> ms;
        for (size_t i = 1; i < tok.size(); ++i)
        {
            const int id = std::atoi(tok[i].c_str());
            auto f = before.ids.find(id);
            if (f == before.ids.end()) { bp_out(L"REFUSE bp_kill id " + std::to_wstring(id) + L" not in the snapshot, nothing sent"); bp_flush(); return STR("error: id not found (see BPProbe.txt)"); }
            ms.push_back({ id, pp_short(to_utf8(f->second)) });
        }
        std::vector<RefundItem> need;
        std::string rerr = refund_for(ms, need);
        if (rerr.empty()) rerr = refund_check(need, "bp_kill");
        if (!rerr.empty()) { bp_out(widen("REFUSE bp_kill " + rerr)); bp_flush(); return widen("error: " + rerr); }
        bp_inv_full(before, L"BEFORE kill");
        int sent = 0;
        for (size_t i = 1; i < tok.size(); ++i)
        {
            BpSend s;
            s.tok = { "bp_send", "DisassembleInput", "0", "targetid=" + tok[i], "bmassdisassemble=0" };
            g_bpp.pending = false;
            if (!seh_run(bp_send_fn, &s)) bp_out(L"FAULT bp_kill " + seh_describe(g_seh));
            if (s.called) ++sent; else bp_out(s.out);
        }
        g_bpp.pending = true; g_bpp.t0 = GetTickCount64(); g_bpp.stage = 0; g_bpp.ref = before;
        bp_out(L"KILL sent " + std::to_wstring(sent) + L" of " + std::to_wstring(tok.size() - 1));
        bp_flush();
        return STR("ok bp_kill sent ") + widen(std::to_string(sent));
    }

    // ===== bp_run: script runner. Parsed at call time, executed one op per bp_tick (state machine, timestamps, never blocks) =====
    // WHAT: one expanded op: source line number (1-based), raw tokens, parsed integers (station id / place x y h rot / wait ms).
    struct BpRunOp { int line = 0; std::vector<std::string> t; std::vector<long long> v; };
    // WHAT: one script line after blank/comment removal: number and tokens.
    struct BpRunLine { int no = 0; std::vector<std::string> t; };
    // WHAT: running script. next = index of the next op, wait_until = GetTickCount64 before which no op runs, snap = last snap ("snap" op).
    struct BpRunState { bool active = false; std::vector<BpRunOp> ops; size_t next = 0; ULONGLONG wait_until = 0; int executed = 0, refused = 0; long long station = 0; bool have_snap = false; BpSnap snap; };
    BpRunState g_bpr;
    // ponytail: flat op cap so a huge repeat cannot exhaust memory; raise it if a real script needs more.
    const size_t kBpRunMaxOps = 100000;

    // WHAT: strict integer parse of a whole token. false when malformed.
    bool bp_run_num(const std::string& s, long long& v)
    {
        if (s.empty()) return false;
        const char* e = s.data() + s.size();
        const auto r = std::from_chars(s.data(), e, v);
        return r.ec == std::errc() && r.ptr == e;
    }

    // WHAT: reads the script file (BOM dropped). Keeps only non-blank, non-comment lines; a '#' token ends the line.
    bool bp_run_read(const std::wstring& path, std::vector<BpRunLine>& lines)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::string line;
        int no = 0;
        while (std::getline(f, line))
        {
            ++no;
            if (no == 1 && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            std::istringstream is(line);
            BpRunLine r; r.no = no;
            std::string t;
            while (is >> t && t[0] != '#') r.t.push_back(t);
            if (!r.t.empty()) lines.push_back(r);
        }
        return true;
    }

    // WHAT: parses lines from i into out (repeat blocks copied n times). depth = repeat nesting (max 2). inRep: stops at endrepeat (i left on it).
    //       Any bad line -> false, err = "line N: reason". The whole script is refused on false.
    bool bp_run_parse(const std::vector<BpRunLine>& lines, size_t& i, int depth, bool inRep, std::vector<BpRunOp>& out, std::wstring& err)
    {
        while (i < lines.size())
        {
            const BpRunLine& ln = lines[i];
            const std::string& op = ln.t[0];
            const size_t na = ln.t.size() - 1;
            auto bad = [&](const std::wstring& why) { err = L"line " + std::to_wstring(ln.no) + L": " + why; return false; };
            auto num = [&](size_t k, long long& v) { return k < ln.t.size() && bp_run_num(ln.t[k], v); };
            if (op == "endrepeat")
            {
                if (!inRep) return bad(L"endrepeat without repeat");
                if (na != 0) return bad(L"endrepeat takes no arguments");
                return true;
            }
            if (op == "repeat")
            {
                long long n = 0;
                if (na != 1 || !num(1, n) || n < 0) return bad(L"usage: repeat <n> (integer >= 0)");
                if (depth >= 2) return bad(L"repeat nested deeper than 2 levels");
                const int start = ln.no;
                ++i;
                std::vector<BpRunOp> body;
                if (!bp_run_parse(lines, i, depth + 1, true, body, err)) return false;
                if (i >= lines.size()) { err = L"line " + std::to_wstring(start) + L": repeat without endrepeat"; return false; }
                ++i;
                for (long long k = 0; k < n && !body.empty(); ++k)
                {
                    if (out.size() + body.size() > kBpRunMaxOps) { err = L"line " + std::to_wstring(start) + L": too many ops (cap " + std::to_wstring(kBpRunMaxOps) + L")"; return false; }
                    out.insert(out.end(), body.begin(), body.end());
                }
                continue;
            }
            BpRunOp o; o.line = ln.no; o.t = ln.t;
            long long v[4] = { 0, 0, 0, 0 };
            if (op == "station") { if (na != 1 || !num(1, v[0]) || v[0] <= 0) return bad(L"usage: station <id> (id > 0)"); o.v = { v[0] }; }
            else if (op == "place")
            {
                const bool withCfg = na == 6 && ln.t[6].rfind("cfg=", 0) == 0;
                if ((na != 5 && !withCfg) || ln.t[1].rfind("DAT_", 0) != 0 || !num(2, v[0]) || !num(3, v[1]) || !num(4, v[2]) || !num(5, v[3]))
                    return bad(L"usage: place <DAT_name> <x> <y> <h> <rot> [cfg=<hex>] (integers)");
                std::vector<uint8_t> cfgBytes;
                if (withCfg && !bp_hex_bytes(widen(ln.t[6].substr(4)), cfgBytes)) return bad(L"cfg= needs hex (even length, 0-9 a-f)");
                o.v = { v[0], v[1], v[2], v[3] };
            }
            else if (op == "snap" || op == "kill_new") { if (na != 0) return bad(L"usage: " + widen(op)); }
            else if (op == "wait") { if (na != 1 || !num(1, v[0]) || v[0] < 0) return bad(L"usage: wait <ms> (integer >= 0)"); o.v = { v[0] }; }
            else if (op == "report") { if (na < 1) return bad(L"usage: report <label>"); }
            else return bad(L"unknown op " + widen(op));
            out.push_back(o);
            ++i;
        }
        return true;
    }

    // WHAT: runs one expanded op. Returns ok; detail = text after OK/REFUSE in the log. Extra lines (report, diff, ids) are written with bp_out.
    //       Sends go through bp_send_fn under seh_run; the place/kill diff bookkeeping of bp_send is skipped.
    bool bp_run_exec(const BpRunOp& op, ULONGLONG now, std::wstring& detail)
    {
        const std::string& k = op.t[0];
        if (k == "station") { g_bpr.station = op.v[0]; return true; }
        if (k == "wait") { g_bpr.wait_until = now + static_cast<ULONGLONG>(op.v[0]); return true; }
        if (k == "snap")
        {
            BpSnap s;
            if (!bp_snap(s)) { detail = L"snapshot failed"; return false; }
            g_bpr.snap = s; g_bpr.have_snap = true;
            detail = L"machines=" + std::to_wstring(s.ids.size());
            return true;
        }
        if (k == "place")
        {
            if (g_bpr.station == 0) { detail = L"no station set (station <id> first)"; return false; }
            BpSend s;
            s.tok = { "bp_send", "BuildInput", "0", "buildable=" + op.t[1], "attachtargetid=" + std::to_string(g_bpr.station),
                      "gridtransform.gridlocation.x=" + std::to_string(op.v[0]), "gridtransform.gridlocation.y=" + std::to_string(op.v[1]),
                      "gridtransform.height=" + std::to_string(op.v[2]), "gridtransform.gridrotation=" + std::to_string(op.v[3]) };
            if (op.t.size() == 7) s.tok.push_back("machineconfigdata=" + op.t[6].substr(4));
            if (!seh_run(bp_send_fn, &s)) { detail = L"FAULT " + seh_describe(g_seh); return false; }
            if (!s.called) { detail = s.out; return false; }
            return true;
        }
        if (k == "kill_new" || k == "report")
        {
            if (!g_bpr.have_snap) { detail = L"no snap yet"; return false; }
            BpSnap cur;
            if (!bp_snap(cur)) { detail = L"snapshot failed"; return false; }
            std::vector<int> ids;
            for (auto& kv : cur.ids) if (!g_bpr.snap.ids.count(kv.first)) ids.push_back(kv.first);
            if (k == "report")
            {
                std::map<std::wstring, int> per;
                for (int id : ids) ++per[cur.ids[id]];
                std::wstring cc;
                for (auto& kv : per) cc += (cc.empty() ? L"" : L", ") + kv.first + L" x" + std::to_wstring(kv.second);
                std::wstring label;
                for (size_t j = 1; j < op.t.size(); ++j) label += (j > 1 ? L" " : L"") + widen(op.t[j]);
                std::wstring line = L"RUN REPORT " + label + L": new machines " + std::to_wstring(ids.size());
                if (!cc.empty()) line += L" (" + cc + L")";
                bp_out(line);
                bp_diff(g_bpr.snap, cur);
                return true;
            }
            // kill_new: only ids that are in cur and not in the last snap
            if (!ids.empty())
            {
                std::vector<std::pair<int, std::string>> ms;
                for (int id : ids) ms.push_back({ id, pp_short(to_utf8(cur.ids[id])) });
                std::vector<RefundItem> need;
                std::string rerr = refund_for(ms, need);
                if (rerr.empty()) rerr = refund_check(need, "kill_new");
                if (!rerr.empty()) { detail = widen(rerr); return false; }
            }
            bool ok = true;
            int sent = 0;
            std::wstring list;
            for (int id : ids)
            {
                list += (list.empty() ? L"" : L",") + std::to_wstring(id);
                BpSend s;
                s.tok = { "bp_send", "DisassembleInput", "0", "targetid=" + std::to_string(id), "bmassdisassemble=0" };
                if (!seh_run(bp_send_fn, &s)) { bp_out(L"FAULT kill_new " + seh_describe(g_seh)); ok = false; }
                else if (s.called) ++sent;
                else { bp_out(s.out); ok = false; }
            }
            detail = L"ids=" + (list.empty() ? std::wstring(L"none") : list) + L" sent=" + std::to_wstring(sent) + L" of " + std::to_wstring(ids.size());
            return ok;
        }
        detail = L"internal: unknown op";
        return false;
    }

    // WHAT: one step of the running script (from bp_tick). At most one op per call; waits by timestamp; holds while a bp_place/bp_send check runs.
    void bp_run_tick()
    {
        if (!g_bpr.active || g_bpp.pending) return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_bpr.wait_until) return;
        if (g_bpr.next >= g_bpr.ops.size())
        {
            bp_out(L"RUN DONE ops=" + std::to_wstring(g_bpr.executed) + L" refused=" + std::to_wstring(g_bpr.refused));
            g_bpr.active = false;
            g_bpr.ops.clear();
            return;
        }
        const BpRunOp& op = g_bpr.ops[g_bpr.next++];
        ++g_bpr.executed;
        std::wstring head = L"RUN " + std::to_wstring(op.line);
        for (const std::string& t : op.t) head += L" " + widen(t);
        bp_out(head);
        std::wstring detail;
        const bool ok = bp_run_exec(op, now, detail);
        if (!ok) ++g_bpr.refused;
        bp_out(L"RUN " + std::to_wstring(op.line) + (ok ? L" OK " : L" REFUSE ") + detail);
    }

    // WHAT: bp_run <absolute path> loads and expands the script (whole file refused on any bad line), bp_run stop aborts. Writes (one command). Only on user command.
    std::wstring bp_run_cmd(const std::string& cmd)
    {
        std::string arg = cmd.size() > 7 ? cmd.substr(7) : "";
        const size_t a = arg.find_first_not_of(" \t\r\n");
        arg = a == std::string::npos ? "" : arg.substr(a, arg.find_last_not_of(" \t\r\n") - a + 1);
        bp_file_init();
        if (arg == "stop")
        {
            if (!g_bpr.active) return STR("error: no bp_run active");
            bp_out(L"RUN ABORT ops=" + std::to_wstring(g_bpr.executed) + L" refused=" + std::to_wstring(g_bpr.refused));
            g_bpr.active = false;
            g_bpr.ops.clear();
            bp_flush();
            return STR("ok bp_run stopped");
        }
        if (arg.empty()) return STR("error: usage bp_run <absolute path to script> | bp_run stop");
        if (g_bpr.active) return STR("error: bp_run already running (bp_run stop aborts it)");
        const std::wstring path = widen(arg);
        bp_out(L"=== bp_run " + path);
        std::vector<BpRunLine> lines;
        if (!bp_run_read(path, lines)) { bp_out(L"RUN REFUSE cannot open " + path); bp_flush(); return STR("error: cannot open script (see BPProbe.txt)"); }
        std::vector<BpRunOp> ops;
        size_t i = 0;
        std::wstring err;
        if (!bp_run_parse(lines, i, 0, false, ops, err)) { bp_out(L"RUN REFUSE load " + err); bp_flush(); return STR("error: script refused (see BPProbe.txt)"); }
        g_bpr = BpRunState{};
        g_bpr.active = true;
        g_bpr.ops = std::move(ops);
        bp_out(L"RUN LOADED ops=" + std::to_wstring(g_bpr.ops.size()));
        bp_flush();
        return STR("ok ") + widen(std::to_string(g_bpr.ops.size())) + STR(" ops queued");
    }

    // ===== Probe 7 commands: bp_aim, bp_confirm, bp_enter (all WRITE, only on user command) =====
    // WHAT: registers the UpdatePreview hook of BuilderBase / MachineBuilder / ConveyorBuilder (bp_add_hook dedups). Under seh_run.
    void bp_aim_hooks_fn(void*)
    {
        for (const wchar_t* cn : { L"BuilderBase", L"MachineBuilder", L"ConveyorBuilder" })
        {
            UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (std::wstring(L"/Script/MainSequence.") + cn).c_str());
            if (!alive(c)) continue;
            for (UStruct* u = c; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
                if (UClass* uc = Cast<UClass>(u))
                    for (UFunction* f : uc->ForEachFunction())
                        if (lower(f->GetName()) == L"updatepreview") bp_add_hook(f, false, 5, false);
        }
    }

    // WHAT: bp_aim <x> <y> <z> | bp_aim off. Sets / clears the override point of the builder UpdatePreview pre-hook (works unarmed).
    std::wstring bp_aim_cmd(const std::string& cmd)
    {
        std::vector<std::string> tok;
        { std::istringstream is(cmd); std::string t; while (is >> t) tok.push_back(t); }
        bp_file_init();
        if (tok.size() == 2 && tok[1] == "off")
        {
            g_aim.on = false;
            bp_out(L"=== bp_aim off");
            bp_flush();
            return STR("ok aim off");
        }
        if (tok.size() != 4) return STR("error: usage bp_aim <x> <y> <z> | bp_aim off");
        double v[3] = { 0, 0, 0 };
        for (int i = 0; i < 3; ++i)
        {
            char* e = nullptr;
            v[i] = std::strtod(tok[i + 1].c_str(), &e);
            if (e == tok[i + 1].c_str() || *e) return STR("error: bp_aim needs three numbers");
        }
        bp_out(L"=== bp_aim " + widen(cmd));
        if (!g_aim.hooked)
        {
            g_aim.hooked = true;
            if (!seh_run(bp_aim_hooks_fn, nullptr)) bp_out(L"FAULT aim hooks " + seh_describe(g_seh));
        }
        for (int i = 0; i < 3; ++i) g_aim.v[i] = v[i];
        g_aim.on = true;
        g_aim.wrote = false;
        bp_out(L"AIM on target=(" + std::to_wstring(v[0]) + L"," + std::to_wstring(v[1]) + L"," + std::to_wstring(v[2]) + L")");
        bp_flush();
        return STR("ok aim set, see BPProbe.txt");
    }

    struct BpConfCtx { std::wstring out; bool called = false; };

    // WHAT: bp_confirm body under seh_run. First live ConveyorBuilder / MachineBuilder / BuilderBase. Refuses when none, when bValid is not 1,
    //       or when ConfirmBuild takes input parameters (not guessed).
    void bp_confirm_fn(void* p)
    {
        auto* c = static_cast<BpConfCtx*>(p);
        std::wstring& s = c->out;
        UObject* self = nullptr;
        for (const wchar_t* cn : { L"ConveyorBuilder", L"MachineBuilder", L"BuilderBase" })
        {
            if (self) break;
            std::vector<UObject*> v;
            UObjectGlobals::FindAllOf(cn, v);
            for (UObject* o : v) if (!self && alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0) self = o;
        }
        if (!self) { s += L"REFUSE no live ConveyorBuilder / MachineBuilder / BuilderBase\n"; return; }
        s += L"BUILDER " + bp_builder_info(self) + L"\n";
        bool valid = false;
        if (!get_bool_prop(self, STR("bValid"), valid) || !valid) { s += L"REFUSE bValid is not 1\n"; return; }
        UFunction* f = self->GetFunctionByNameInChain(STR("ConfirmBuild"));
        if (!f) { s += L"REFUSE ConfirmBuild missing on builder\n"; return; }
        bp_sig_one(s, f);
        for (FProperty* pr : f->ForEachProperty())
            if (pr->HasAnyPropertyFlags(CPF_Parm) && !pr->HasAnyPropertyFlags(CPF_ReturnParm) && !pr->HasAnyPropertyFlags(CPF_OutParm))
            { s += L"REFUSE ConfirmBuild has input parameter " + pr->GetName() + L" (not guessing)\n"; return; }
        std::vector<uint8_t> buf(f->GetParmsSize() + 16, 0);
        self->ProcessEvent(f, buf.data());
        c->called = true;
        s += L"CALLED ConfirmBuild on " + self->GetClassPrivate()->GetName() + L"\n";
        bp_dump(s, buf.data(), f, nullptr, f->GetParmsSize());
    }

    // WHAT: bp_confirm: snapshot before (like bp_place), ConfirmBuild on the builder, then +1.5 s / +5 s diffs via bp_place_tick.
    std::wstring bp_confirm_cmd()
    {
        if (g_bpp.pending) return STR("error: previous bp_place/bp_send/bp_confirm still being checked, wait 5 s");
        bp_file_init();
        bp_out(L"=== bp_confirm");
        BpSnap before;
        if (!bp_snap(before)) { bp_out(L"REFUSE before snapshot failed"); bp_flush(); return STR("error: snapshot failed"); }
        bp_inv_full(before, L"BEFORE confirm");
        BpConfCtx c;
        const bool ok = seh_run(bp_confirm_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT bp_confirm " + seh_describe(g_seh));
        if (c.called) { g_bpp.pending = true; g_bpp.t0 = GetTickCount64(); g_bpp.stage = 0; g_bpp.ref = before; }
        bp_flush();
        return !ok ? STR("error: bp_confirm fault (see BPProbe.txt)") : c.called ? STR("ok called, diffs follow in BPProbe.txt") : STR("refused, see BPProbe.txt");
    }

    struct BpEnterCtx { std::vector<std::string> tok; std::wstring out; bool called = false; };

    // WHAT: bp_enter body under seh_run. EnterBuildingMode(buildable) or EnterMachineBuildMode(buildable, null) on BP_PlayerController_C.
    //       Refuses on missing / ambiguous Buildable name, missing function, or parameters that are not all objects.
    void bp_enter_fn(void* p)
    {
        auto* c = static_cast<BpEnterCtx*>(p);
        std::wstring& s = c->out;
        const std::wstring want = widen(c->tok[1]);
        const bool machine = c->tok.size() > 2 && c->tok[2] == "machine";
        UObject* ctrl = first_of(STR("BP_PlayerController_C"));
        if (!alive(ctrl)) { s += L"REFUSE BP_PlayerController_C not live\n"; return; }
        UObject* bd = nullptr; int nb = 0;
        UObjectGlobals::ForEachUObject([&](UObject* o, int32_t, int32_t)
        {
            if (alive(o) && o->GetName() == want && o->GetName().rfind(STR("Default__"), 0) != 0 && bp_is_buildable(o->GetClassPrivate()))
            { if (!bd) bd = o; ++nb; }
            return LoopAction::Continue;
        });
        if (!bd) { s += L"REFUSE no live Buildable named " + want + L"\n"; return; }
        if (nb > 1) { s += L"REFUSE " + std::to_wstring(nb) + L" Buildables named " + want + L" (ambiguous)\n"; return; }
        const wchar_t* fnn = machine ? L"EnterMachineBuildMode" : L"EnterBuildingMode";
        UFunction* f = ctrl->GetFunctionByNameInChain(fnn);
        if (!f) { s += L"REFUSE " + std::wstring(fnn) + L" missing\n"; return; }
        bp_sig_one(s, f);
        std::vector<uint8_t> buf(f->GetParmsSize() + 16, 0);
        int objs = 0;
        for (FProperty* pr : f->ForEachProperty())
        {
            if (!pr->HasAnyPropertyFlags(CPF_Parm) || pr->HasAnyPropertyFlags(CPF_ReturnParm) || pr->HasAnyPropertyFlags(CPF_OutParm)) continue;
            if (!CastField<FObjectProperty>(pr)) { s += L"REFUSE parameter " + pr->GetName() + L" is not an object (not guessing)\n"; return; }
            UObject* v = objs == 0 ? bd : nullptr;   // machine: 2nd argument (TemplateMachine) null
            *reinterpret_cast<UObject**>(buf.data() + pr->GetOffset_Internal()) = v;
            s += L"ARG " + pr->GetName() + L" = " + (v ? v->GetName() : std::wstring(STR("null"))) + L"\n";
            ++objs;
        }
        if (objs != (machine ? 2 : 1)) { s += L"REFUSE " + std::wstring(fnn) + L" has " + std::to_wstring(objs) + L" object parameters (not guessing)\n"; return; }
        bp_dump(s, buf.data(), f, nullptr, f->GetParmsSize());
        ctrl->ProcessEvent(f, buf.data());
        c->called = true;
        s += L"CALLED " + std::wstring(fnn) + L" on " + ctrl->GetClassPrivate()->GetName() + L"\n";
        bp_dump(s, buf.data(), f, nullptr, f->GetParmsSize());
    }

    // WHAT: bp_enter <BuildableDataAssetName> [machine]. Writes (one command). Only on user command.
    std::wstring bp_enter_cmd(const std::string& cmd)
    {
        BpEnterCtx c;
        { std::istringstream is(cmd); std::string t; while (is >> t) c.tok.push_back(t); }
        if (c.tok.size() < 2) return STR("error: usage bp_enter <BuildableDataAssetName> [machine]");
        bp_file_init();
        bp_out(L"=== bp_enter " + widen(cmd));
        const bool ok = seh_run(bp_enter_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT bp_enter " + seh_describe(g_seh));
        bp_flush();
        return !ok ? STR("error: bp_enter fault (see BPProbe.txt)") : c.called ? STR("ok called, see BPProbe.txt") : STR("refused, see BPProbe.txt");
    }

    // ===== Probe 9: bp_paste <path|clip> [station <id>] [replace] (WRITES machines, all or nothing) =====
    // WHY tables: vault tasks/paste-tables.txt. Machine: DAT -> footprint w,h (the class column is unused, the Buildable Class is read live).
    // Block: class -> floor w,h and nofloor (the block w,h columns are unused).
    struct PTabM { const char* dat; int w, h; };
    const PTabM kPasteMach[] = {
        {"DAT_LargeConveyorBelt",3,3}, {"DAT_TradeConsole",5,5}, {"DAT_Assembler",5,5}, {"DAT_BeltBalancer_01",1,2},
        {"DAT_Container",2,2}, {"DAT_ConveyorBelt",1,1}, {"DAT_MatSciLab",3,3}, {"DAT_ReactorMachine",6,6},
        {"DAT_Refinery",3,3}, {"DAT_RelicAnalyzer",3,3}, {"DAT_SignWall",2,6}, {"DAT_Splitter",1,1},
        {"DAT_Teleporter",1,1}, {"DAT_UndergroundBelt",1,1}, {"DAT_WorkbenchMachine",2,4}, {"DAT_Container_2",4,4},
        {"DAT_CompSciLab",3,3}, {"DAT_Armoury",4,4}, {"DAT_BeltBalancer_02",1,2}, {"DAT_ConveyorBelt2",1,1},
        {"DAT_Splitter_2",1,1}, {"DAT_UndergroundBelt_2",1,1}, {"DAT_EnrichmentChamber",9,9}, {"DAT_ElectroSciLab",5,5},
        {"DAT_BeltBalancer_03",1,2}, {"DAT_ConveyorBelt3",1,1}, {"DAT_Splitter_3",1,1}, {"DAT_UndergroundBelt_3",1,1},
        {"DAT_QuantumSciLab",9,9}, {"DAT_RelicSynthesizer",9,9}, {"DAT_AdvancedAssembler",9,9}, {"DAT_GrowthChamber",5,5},
        {"DAT_Supercomputer",5,5}, {"DAT_AstroSciLab",5,5}, {"DAT_XenoSciLab",5,5} };
    struct PTabB { const char* cls; int fw, fh; bool nofloor; };
    const PTabB kPasteBlk[] = {
        {"BPO_Station_01",14,14,false}, {"BPO_Station_2x1",14,30,false}, {"BPO_Dock",0,0,true}, {"BPO_CommandModule",0,0,false},
        {"BPO_ShipyardCore",0,0,true}, {"BPO_ShipyardSide",0,0,true}, {"BPO_Station_02A",30,30,false}, {"BPO_StationCorner_01",0,0,false},
        {"BPO_StationCorridor_01",0,0,false}, {"BPO_ElevatorBlock_03",12,12,false}, {"BPO_StationTurretBlock_01",0,0,false},
        {"BPO_StationArmour_01",0,0,false}, {"BPO_MiningDroneBayBlock",0,0,false}, {"BPO_SolarPanelBlock",0,0,false},
        {"BPO_Dock_2",14,30,false}, {"BPO_SolarPanelBlock_02",0,0,false}, {"BPO_ShipyardCore_02",0,0,true}, {"BPO_Station_02",30,30,false} };

    // WHAT: one block (JSON or live): short class name, grid x/y, height, rotation.
    struct PBlock { std::string cls; int x = 0, y = 0, h = 0, rot = 0; };
    // WHAT: one machine. JSON: cls, dat (DAT_ DefaultName or empty), x y h rot. Resolved: bdat, bcls, w/hh (rotated footprint).
    //       Existing machines: id, bcls, footprint.
    // WHAT: one port from the JSON Ports array (Face N/E/S/W, Index, Enabled).
    struct PPort { std::string face; int index = 0; bool enabled = true; };
    // WHAT: ... plus recipe (JSON Recipe, "" = absent), ports (JSON Ports, hasPorts = array present) and cfg (bytes sent as machineconfigdata, empty = none).
    struct PMach { std::string cls, dat, bdat, bcls, recipe; bool hasPorts = false; std::vector<PPort> ports; std::vector<uint8_t> cfg; int x = 0, y = 0, h = 0, rot = 0, w = 1, hh = 1, id = 0;
                   bool hasHex = false; std::vector<uint8_t> hexcfg;      // hasHex/hexcfg: JSON ConfigHex (verbatim config)
                   bool hasLive = false; std::vector<uint8_t> live; };    // existing machines: live SerializedMachineConfig from Blueprint.Machines (hasLive = read)
    struct PRect { int x0, y0, x1, y1; };
    struct PasteDoc { std::string name; std::vector<PBlock> blocks; std::vector<PMach> machines; };
    using PBuildables = std::vector<std::pair<std::string, std::string>>;   // Buildable asset name, class short name
    // WHAT: live read result (pp_scan_fn): buildables, chosen station, its blocks and machines.
    struct PasteCtx { long long station = 0; std::string err, cands; PBuildables bld; std::vector<PBlock> blocks; std::vector<PMach> existing; };
    // WHAT: a paste in progress. stage 1 = waiting for replace removals (t_next = next check), stage 2 = placed, waiting for verify.
    // cells = underground exit cells (not placed, checked for overlap/floor/conflicts like ms)
    // skipped = machines the cost check left out (not placed, not verified); short_txt = "item xK, ..." of their shortfall.
    std::string g_pp_ugskip;   // lone undergrounds without Length info, skipped (listed in the final status)
    // WHAT: one config change of a kept machine (identical position/class/rotation, different recipe or ports). recipe = SetRecipeInput; ports = MachinePortInput toggles.
    struct PCfgChg { PMach m; bool recipe = false; std::string oldRec, newRec; std::vector<int> ports; };
    // kept = identical machines already on the station (not placed, not paid, not refunded). chg = their config changes (verified at +3 s). recChanged = recipe changes.
    // sync = replace mode (blueprint sync). staleN = stale machines removed. stale_ids = ids of the stale machines (stage 1 waits until they are gone).
    struct BpPasteRun { bool active = false; int stage = 0, tries = 0, placed = 0; ULONGLONG t_next = 0; long long station = 0; std::vector<PMach> ms, cells, skipped, kept; std::string short_txt; std::vector<PCfgChg> chg; int recChanged = 0;
                        bool sync = false; int staleN = 0; std::vector<int> stale_ids; };
    BpPasteRun g_bpaste;

    // WHAT: bp_import plan (M4 dry-run): the last planned blueprint against the live station. Nothing is placed or sent from it.
    //       place = new machines (no cost filter), cells = underground exit cells, kept = identical (free), chg = their config changes,
    //       removed = conflicts + stale (existing), staleIds = ids of the stale ones, existing = live station machines at plan time,
    //       fp = pp_fingerprint(existing), need/refund = per item cost (bp_import), skippedUg = underground positions dropped (no Length).
    struct PPlan { long long station = 0; std::string name; std::vector<PMach> place, cells, kept; std::vector<PCfgChg> chg; int recChanged = 0, portChanged = 0;
                   std::vector<PMach> removed; std::vector<int> staleIds; PBuildables bld; std::vector<PMach> existing;
                   std::map<std::string, int64_t> need, refund; uint64_t fp = 0; bool valid = false; std::string skippedUg; };
    PPlan g_bpplan;

    // WHAT: "Class" short name: path and '.' suffix removed, "_C" removed. "/Game/x/BPO_A.BPO_A_C" -> "BPO_A".
    std::string pp_short(std::string s)
    {
        const size_t sl = s.find_last_of("/\\");
        if (sl != std::string::npos) s = s.substr(sl + 1);
        const size_t dot = s.find('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        if (s.size() > 2 && s.compare(s.size() - 2, 2, "_C") == 0) s.resize(s.size() - 2);
        return s;
    }
    const PTabM* pp_mach_tab(const std::string& dat) { for (const PTabM& t : kPasteMach) if (dat == t.dat) return &t; return nullptr; }
    const PTabB* pp_blk_tab(const std::string& cls) { for (const PTabB& t : kPasteBlk) if (cls == t.cls) return &t; return nullptr; }
    // WHAT: floor division by 8 (layer of a height). -4 -> -1, 8 -> 1.
    int pp_layer(int h) { return h >= 0 ? h / 8 : -((-h + 7) / 8); }
    PRect pp_rect(int cx, int cy, int hw, int hh) { return { cx - hw, cy - hh, cx + hw, cy + hh }; }
    bool pp_in(const PRect& a, const PRect& f) { return a.x0 >= f.x0 && a.x1 <= f.x1 && a.y0 >= f.y0 && a.y1 <= f.y1; }
    // WHAT: positive-area intersection (touching edges do not count).
    bool pp_hit(const PRect& a, const PRect& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
    // WHAT: w x h turned by rot (odd rotation swaps).
    void pp_fp(int w, int h, int rot, int& fw, int& fh) { fw = (rot & 1) ? h : w; fh = (rot & 1) ? w : h; }
    // WHAT: floor rect of one block: centre = GridLocation * 8, half extents = table floor (swapped when rot odd). false = no floor.
    bool pp_floor(const PBlock& b, PRect& r)
    {
        const PTabB* t = pp_blk_tab(b.cls);
        if (!t || t->nofloor || t->fw <= 0 || t->fh <= 0) return false;
        int hw = 0, hh = 0;
        pp_fp(t->fw, t->fh, b.rot, hw, hh);
        r = pp_rect(b.x * 8, b.y * 8, hw, hh);
        return true;
    }
    std::string pp_where(const PMach& m) { return m.bdat + "@(" + std::to_string(m.x) + "," + std::to_string(m.y) + ",h" + std::to_string(m.h) + ")"; }
    // WHAT: new machines against the live block floors (same layer, rect fully inside one floor), parity (x = w, y = h mod 2) and each other.
    //       "" when OK, else the first reason.
    std::string pp_check_new(const std::vector<PBlock>& blocks, const std::vector<PMach>& ms)
    {
        for (const PMach& m : ms)
        {
            if (((m.x - m.w) & 1) || ((m.y - m.hh) & 1)) return "parity (X must match width, Y height): " + pp_where(m);
            const PRect r = pp_rect(m.x, m.y, m.w, m.hh);
            bool on = m.bdat == "DAT_SignWall";   // WHY: wall-mounted (sits on the wall between floors), no floor to check
            for (const PBlock& b : blocks)
            {
                PRect f;
                if (!on && pp_layer(b.h) == m.h / 8 && pp_floor(b, f) && pp_in(r, f)) { on = true; break; }
            }
            if (!on) return "outside every block floor of its layer: " + pp_where(m);
        }
        for (size_t i = 0; i < ms.size(); ++i)
            for (size_t j = i + 1; j < ms.size(); ++j)
                if (pp_hit(pp_rect(ms[i].x, ms[i].y, ms[i].w, ms[i].hh), pp_rect(ms[j].x, ms[j].y, ms[j].w, ms[j].hh)))
                    return "overlap: " + pp_where(ms[i]) + " and " + pp_where(ms[j]);
        return "";
    }
    // WHAT: indices of existing machines whose footprint overlaps a new one.
    std::vector<size_t> pp_conflicts(const std::vector<PMach>& ms, const std::vector<PMach>& ex)
    {
        std::vector<size_t> hit;
        for (size_t k = 0; k < ex.size(); ++k)
        {
            const PRect er = pp_rect(ex[k].x, ex[k].y, ex[k].w, ex[k].hh);
            for (const PMach& m : ms) if (m.h == ex[k].h && pp_hit(pp_rect(m.x, m.y, m.w, m.hh), er)) { hit.push_back(k); break; }   // same layer only
        }
        return hit;
    }

    int pp_cfg_len(const std::vector<uint8_t>& b);   // defined with the config bytes below
    bool pp_is_ug(const PMach& m);                    // defined with the underground pairing below
    // WHAT: identity keep. A pasted machine with an existing machine of the same class, GridLocation, Height and rotation is IDENTICAL: it moves
    //       from ms to kept (existing id and live config copied). Each existing machine matches at most once. Pure (no log).
    //       sync (replace mode): an underground also needs the same Length (live config); a different Length is not identical (stale + new).
    void pp_split_kept(std::vector<PMach>& ms, const std::vector<PMach>& ex, std::vector<PMach>& kept, bool sync = false)
    {
        std::vector<PMach> rest;
        std::vector<bool> used(ex.size(), false);
        for (PMach m : ms)
        {
            int hit = -1;
            for (size_t k = 0; k < ex.size() && hit < 0; ++k)
            {
                if (used[k] || ex[k].bcls != m.bcls || ex[k].x != m.x || ex[k].y != m.y || ex[k].h != m.h || ex[k].rot != m.rot) continue;
                if (sync && pp_is_ug(m) && ex[k].hasLive && pp_cfg_len(ex[k].live) != pp_cfg_len(m.cfg)) continue;   // Length differs
                hit = static_cast<int>(k);
            }
            if (hit < 0) { rest.push_back(m); continue; }
            used[hit] = true;
            m.id = ex[hit].id;
            m.hasLive = ex[hit].hasLive;
            m.live = ex[hit].live;
            kept.push_back(m);
        }
        ms.swap(rest);
    }

    // ----- machine config bytes (vault rea-machine-config.md s2/s3). Little endian. FString = i32 len incl NUL, bytes, NUL (empty = i32 0). -----
    void pp_put_i32(std::vector<uint8_t>& b, int32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> (8 * i)) & 0xff)); }
    void pp_put_fstr(std::vector<uint8_t>& b, const std::string& s)
    {
        if (s.empty()) { pp_put_i32(b, 0); return; }
        pp_put_i32(b, static_cast<int32_t>(s.size() + 1));
        b.insert(b.end(), s.begin(), s.end());
        b.push_back(0);
    }
    // WHAT: recipe machine config: u16 1, FString recipe, i32 n, n x u8 enabled (one per port, array order).
    std::vector<uint8_t> pp_cfg_recipe(const std::string& recipe, const std::vector<uint8_t>& enabled)
    {
        std::vector<uint8_t> b = { 1, 0 };
        pp_put_fstr(b, recipe);
        pp_put_i32(b, static_cast<int32_t>(enabled.size()));
        b.insert(b.end(), enabled.begin(), enabled.end());
        return b;
    }
    // WHAT: underground config: u16 1, i32 Length.
    std::vector<uint8_t> pp_cfg_underground(int len) { std::vector<uint8_t> b = { 1, 0 }; pp_put_i32(b, len); return b; }
    // WHAT: lowercase hex text of bytes ("" for none).
    std::string pp_hex(const std::vector<uint8_t>& b)
    {
        static const char* dig = "0123456789abcdef";
        std::string s;
        for (uint8_t v : b) { s += dig[v >> 4]; s += dig[v & 15]; }
        return s;
    }
    // WHAT: Length (i32 at byte 2) of an underground config, -1 when the bytes are not that layout.
    int pp_cfg_len(const std::vector<uint8_t>& b)
    {
        if (b.size() != 6) return -1;
        return static_cast<int32_t>(static_cast<uint32_t>(b[2]) | (static_cast<uint32_t>(b[3]) << 8) | (static_cast<uint32_t>(b[4]) << 16) | (static_cast<uint32_t>(b[5]) << 24));
    }
    // WHAT: recipe layout (u16 1, FString recipe, i32 n, n x u8 enabled) -> recipe text and port bytes. false when not that layout or not exactly the size. Pure.
    bool pp_cfg_parse(const std::vector<uint8_t>& b, std::string& rec, std::vector<uint8_t>& ports)
    {
        if (b.size() < 2 + 4 || b[0] != 1 || b[1] != 0) return false;
        auto i32 = [&](size_t at) { return static_cast<int32_t>(static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) | (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24)); };
        size_t p = 2;
        const int32_t fl = i32(p);
        p += 4;
        if (fl < 0 || p + static_cast<size_t>(fl) > b.size()) return false;   // negative = UTF-16 FString, not used by these classes
        rec.clear();
        if (fl > 0)
        {
            if (b[p + fl - 1] != 0) return false;   // FString must end with NUL
            rec.assign(b.begin() + static_cast<std::ptrdiff_t>(p), b.begin() + static_cast<std::ptrdiff_t>(p) + fl - 1);
            p += fl;
        }
        if (b.size() < p + 4) return false;
        const int32_t n = i32(p);
        p += 4;
        if (n < 0 || p + static_cast<size_t>(n) != b.size()) return false;
        ports.assign(b.begin() + static_cast<std::ptrdiff_t>(p), b.end());
        return true;
    }
    // WHAT: decision for one kept recipe machine: target = encoder bytes (what the paste wants), live = its SerializedMachineConfig.
    //       ok = false -> unknown (no live bytes, not the recipe layout, port counts differ): nothing is sent. Else recipe = recipe text differs,
    //       ports = indices whose enabled byte differs (one MachinePortInput toggle each). Pure.
    struct PCfgDiff { bool ok = false, recipe = false; std::string targetRec, liveRec, why; std::vector<int> ports; };
    PCfgDiff pp_cfg_diff(const std::vector<uint8_t>& target, const std::vector<uint8_t>& live)
    {
        PCfgDiff d;
        std::string trec, lrec;
        std::vector<uint8_t> tp, lp;
        if (!pp_cfg_parse(target, trec, tp)) { d.why = "target not recipe layout"; return d; }
        if (live.empty()) { d.why = "live config empty"; return d; }
        if (!pp_cfg_parse(live, lrec, lp)) { d.why = "live config not recipe layout"; return d; }
        if (tp.size() != lp.size()) { d.why = "port count differs (live " + std::to_string(lp.size()) + ", target " + std::to_string(tp.size()) + ")"; return d; }
        d.ok = true;
        d.targetRec = trec;
        d.liveRec = lrec;
        d.recipe = trec != lrec;
        for (size_t i = 0; i < tp.size(); ++i) if ((tp[i] != 0) != (lp[i] != 0)) d.ports.push_back(static_cast<int>(i));
        return d;
    }
    // WHAT: JSON ConfigHex text -> bytes. false + why when not even-length hex or over 4096 bytes. Pure.
    bool pp_hex_cfg(const std::string& hex, std::vector<uint8_t>& out, std::string& why)
    {
        if (!bp_hex_bytes(widen(hex), out)) { why = "not even-length hex (0-9 a-f)"; return false; }
        if (out.size() > 4096) { why = "more than 4096 bytes"; return false; }
        return true;
    }
    bool pp_is_ug(const PMach& m) { return m.dat.rfind("DAT_UndergroundBelt", 0) == 0; }

    // WHAT: greedy affordability, input order. have = stock (each kept machine takes its cost out of it). keep[i] = machine i is paid.
    //       short_out = per item, sum over the skipped machines of (need - stock at their turn) where the stock was short. Pure (selftest).
    using PCostMap = std::map<std::string, int64_t>;
    std::vector<bool> pp_afford(const std::vector<PCostMap>& costs, PCostMap have, PCostMap& short_out)
    {
        std::vector<bool> keep(costs.size(), false);
        for (size_t i = 0; i < costs.size(); ++i)
        {
            bool ok = true;
            for (const auto& kv : costs[i])
            {
                const int64_t h = have[kv.first];
                if (h < kv.second) { ok = false; short_out[kv.first] += kv.second - h; }
            }
            if (!ok) continue;
            for (const auto& kv : costs[i]) have[kv.first] -= kv.second;
            keep[i] = true;
        }
        return keep;
    }

    // WHAT: recipe machines. Port count from machines.json input_port_count. verified = layout checked in a save (false = INF, same layout assumed).
    // crafter = recipes.json crafter without "CRAFT_" (the offline recipe table below uses the same names).
    struct PRecipeMach { const char* dat; int ports; bool verified; const char* crafter; };
    const PRecipeMach kPasteRecipe[] = {
        {"DAT_Assembler",8,true,"Assembler"}, {"DAT_Refinery",4,true,"Refinery"}, {"DAT_EnrichmentChamber",12,true,"Enrichment"},
        {"DAT_AdvancedAssembler",12,false,"AdvancedAssembler"}, {"DAT_GrowthChamber",8,false,"GrowthChamber"},
        {"DAT_Supercomputer",8,false,"Supercomputer"}, {"DAT_RelicSynthesizer",12,false,"RelicSynthesizer"} };

    // BEGIN GEN recipe table (tasks/gen_recipe_table.py from extract/rea/recipes.json, 63 recipes; do not edit by hand)
    struct PRecRow { const char* crafter; const char* out; const char* ins; const char* rec; };
    const PRecRow kPasteRecRows[] = {
        {"AdvancedAssembler", "advancedcircuits", "basiccircuit+coolant+detectorarray+superalloy", "REC_AdvancedCircuits"},
        {"AdvancedAssembler", "advancedplates", "chitincomposite+chromaticalloy+multisensor+reinforcedplates", "REC_AdvancedPlates"},
        {"GrowthChamber", "alphaprotein", "biomass+enzyme+nutrients+xenoeukarya", "REC_AlphaProtein"},
        {"Armoury", "backpackmk1", "hullplates+lightweightframe", "REC_BackpackMk1"},
        {"Armoury", "backpackmk2", "graphene+reinforcedplates", "REC_BackpackMk2"},
        {"Armoury", "backpackmk3", "biolattice+superalloyframe", "REC_BackpackMk3"},
        {"Assembler", "basiccircuit", "magneticcoils+siliconwafers", "REC_BasicCircuit"},
        {"Assembler", "basicmotor", "titaniumplates", "REC_BasicMotor"},
        {"GrowthChamber", "betaprotein", "biomass+enzyme+nutrients+xenoarchaea", "REC_BetaProtein"},
        {"GrowthChamber", "biolattice", "alphaprotein+betaprotein+nutrients", "REC_Biolattice"},
        {"Assembler", "capacitor", "nickelplates+titaniumplates", "REC_Capacitor"},
        {"AdvancedAssembler", "chitincomposite", "biolattice+enzyme+graphene+reinforcedglass", "REC_ChitinComposite"},
        {"Enrichment", "chromaticalloy", "iridiumplates+superalloy", "REC_ChromaticAlloy"},
        {"Refinery", "cobaltplates", "cobaltore", "REC_CobaltPlates"},
        {"Assembler", "containmentcoil", "basiccircuit+capacitor+magneticcoils", "REC_ContainmentCoil"},
        {"Enrichment", "coolant", "nitrogengas", "REC_Coolant"},
        {"Assembler", "detectorarray", "basiccircuit+cobaltplates+graphene", "REC_DetectorArray"},
        {"AdvancedAssembler", "emptysimulationmatrix", "advancedcircuits+lonsdaleite", "REC_EmptySimulationMatrix"},
        {"Supercomputer", "encodedsimulationmatrix", "emptysimulationmatrix+stellarsurveydata", "REC_EncodedSimulationMatrix"},
        {"Enrichment", "enrichedcobaltore", "cobaltore", "REC_EnrichedCobaltOre"},
        {"Refinery", "cobaltplates", "enrichedcobaltore", "REC_EnrichedCobaltPlates"},
        {"Enrichment", "enrichedfuel", "hydrogenfuel", "REC_EnrichedFuel"},
        {"Enrichment", "enrichednickelore", "nickelore", "REC_EnrichedNickelOre"},
        {"Refinery", "nickelplates", "enrichednickelore", "REC_EnrichedNickelPlates"},
        {"Enrichment", "enrichedtitaniumore", "titaniumore", "REC_EnrichedTitaniumOre"},
        {"Refinery", "titaniumplates", "enrichedtitaniumore", "REC_EnrichedTitaniumPlates"},
        {"GrowthChamber", "enzyme", "biomass+nutrients+xenobacteria", "REC_Enzyme"},
        {"Armoury", "exoskeletonmk1", "capacitor+servomotor", "REC_ExoskeletonMk1"},
        {"Armoury", "exoskeletonmk2", "coolant+maglevcoupling", "REC_ExoskeletonMk2"},
        {"Armoury", "exoskeletonmk3", "chitincomposite+chromaticalloy", "REC_ExoskeletonMk3"},
        {"Armoury", "exosuit", "advancedplates+backpackmk3+exoskeletonmk3", "REC_Exosuit"},
        {"Refinery", "graphene", "graphite", "REC_Graphene"},
        {"Assembler", "hullplates", "titaniumplates", "REC_HullPlates"},
        {"Refinery", "hydrogenfuel", "waterice", "REC_HydrogenFuel"},
        {"Refinery", "iridiumplates", "iridiumore+titaniumore", "REC_IridiumPlates"},
        {"Assembler", "lightweightframe", "titaniumplates", "REC_LightweightFrame"},
        {"Assembler", "magneticcoils", "nickelplates", "REC_MageticCoils"},
        {"Assembler", "maglevcoupling", "servomotor+superalloy", "REC_MaglevCoupling"},
        {"Assembler", "missile", "containmentcoil+coolant+hullplates", "REC_Missile"},
        {"AdvancedAssembler", "multisensor", "chromaticalloy+encodedsimulationmatrix+lonsdaleite+superconductorcoil", "REC_Multisensor"},
        {"Refinery", "nickelplates", "nickelore", "REC_NickelPlates"},
        {"Enrichment", "nickelore", "protomatter", "REC_NickelSynthesis"},
        {"GrowthChamber", "nutrients", "graphite+silica+waterice", "REC_Nutrients"},
        {"RelicSynthesizer", "protomatter", "", "REC_Protomatter"},
        {"Refinery", "reinforcedglass", "silica", "REC_ReinforcedGlass"},
        {"Assembler", "reinforcedplates", "hullplates+superalloy", "REC_ReinforcedPlates"},
        {"Assembler", "reliccapacitor", "capacitor+relicshard", "REC_RelicCapacitor"},
        {"RelicSynthesizer", "relicshard", "superalloy", "REC_RelicShardSynthesis"},
        {"Assembler", "repairkit", "lightweightframe+nickelplates", "REC_RepairKit"},
        {"Assembler", "repairkitelectric", "capacitor+magneticcoils", "REC_RepairKitElectric"},
        {"Assembler", "repairkithull", "hullplates+lightweightframe", "REC_RepairKitHull"},
        {"AdvancedAssembler", "servoplates", "maglevcoupling+reinforcedplates", "REC_ServoPlates"},
        {"Assembler", "servomotor", "basicmotor+capacitor+lightweightframe", "REC_Servomotor"},
        {"Refinery", "siliconwafers", "silica", "REC_SiliconWafers"},
        {"Enrichment", "superalloy", "enrichedcobaltore+enrichednickelore+enrichedtitaniumore", "REC_Superalloy"},
        {"AdvancedAssembler", "superalloyframe", "superalloy", "REC_SuperalloyFrame"},
        {"Assembler", "superconductorcoil", "containmentcoil+coolant+maglevcoupling", "REC_SuperconductorCoil"},
        {"Refinery", "titaniumplates", "titaniumore", "REC_TitaniumPlates"},
        {"Enrichment", "titaniumore", "protomatter", "REC_TitaniumSynthesis"},
        {"Enrichment", "warpfuel", "containmentcoil+enrichedfuel", "REC_WarpFuel"},
        {"GrowthChamber", "xenoeukarya", "biomass+xenoeukarya", "REC_XenoEukaryaRecycling"},
        {"GrowthChamber", "xenoarchaea", "biomass+xenoarchaea", "REC_XenoarchaeaRecycling"},
        {"GrowthChamber", "xenobacteria", "biomass+xenobacteria", "REC_XenobacteriaRecycling"},
    };
    // END GEN recipe table

    // WHAT: name key for site <-> game matching: lower case, no leading "item_", no space / '_' / '-' ("item_Silicon Wafers" -> "siliconwafers").
    std::string pp_norm(std::string s)
    {
        for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
        if (s.rfind("item_", 0) == 0) s.erase(0, 5);
        std::string r;
        for (char ch : s) if (ch != ' ' && ch != '_' && ch != '-' && ch != '\t') r += ch;
        return r;
    }
    // WHAT: "item_a + item_b" -> canonical input set "a+b" (each name pp_norm'd, sorted, empty parts dropped). "" = no items.
    std::string pp_item_set(const std::string& s)
    {
        std::vector<std::string> v;
        std::string cur;
        for (size_t i = 0; i <= s.size(); ++i)
        {
            if (i < s.size() && s[i] != '+') { cur += s[i]; continue; }
            const size_t a = cur.find_first_not_of(" \t"), b = cur.find_last_not_of(" \t");
            if (a != std::string::npos) v.push_back(pp_norm(cur.substr(a, b - a + 1)));
            cur.clear();
        }
        std::sort(v.begin(), v.end());
        std::string r;
        for (const std::string& t : v) r += (r.empty() ? "" : "+") + t;
        return r;
    }
    const PRecipeMach* pp_recipe_mach(const std::string& dat) { for (const PRecipeMach& t : kPasteRecipe) if (dat == t.dat) return &t; return nullptr; }
    // WHAT: site recipe id "inputs>output" (website) -> game recipe asset id (REC_*) for the machine's crafter. No guessing:
    //       candidates = same crafter and output item; one candidate -> it; several -> the one with the same input set. Else false + why.
    bool pp_site_rec(const std::string& site, const std::string& crafter, std::string& rec, std::string& why)
    {
        const size_t gt = site.find('>');
        if (gt == std::string::npos) { why = "no '>' in site id"; return false; }
        const std::string out = pp_item_set(site.substr(gt + 1));
        const std::string ins = pp_item_set(site.substr(0, gt));
        std::vector<const PRecRow*> c;
        for (const PRecRow& r : kPasteRecRows) if (crafter == r.crafter && out == r.out) c.push_back(&r);
        if (c.size() > 1)
        {
            std::vector<const PRecRow*> same;
            for (const PRecRow* r : c) if (ins == r->ins) same.push_back(r);
            c = same;
        }
        if (c.size() != 1) { why = std::to_string(c.size()) + " offline candidates for " + crafter + " output " + out; return false; }
        rec = c[0]->rec;
        return true;
    }

    // WHAT: underground pairing. ONE PLACE TO CHANGE THE RULE. "" = OK, else the refusal reason (paste is refused whole).
    //   PROVEN IN GAME (user saw it): a pair is ONE machine at the ENTRANCE cell, DAT_UndergroundBelt_2 at (-7,y) rot 0,
    //   config 01 00 + i32 Length, Length = cells from entrance to exit INCLUSIVE (-7 -> -3 = 3). Nothing at the exit cell or in
    //   the gap (the one machine draws both hoods). Two machines would draw 4 hoods. The website export lists a pair as TWO
    //   UndergroundBelt machines (entrance first along the flow, exit second), so the exit is folded into the entrance here.
    //   Pair = same DAT tier, same row (rot 0/2) or column (rot 1/3), same rotation. GridLocation is in half tiles: cell = 2 units,
    //   so Length = |delta| / 2 + 1 (delta in units). Odd delta = not on the grid = no pair.
    //   Flow: rot 0 = +x, 1 = +y, 2 = -x, 3 = -y (the belt rotation seen in bp_belts). Entrance = upstream one (first along the flow).
    //   Greedy: each unused machine takes the first unused machine after it on the same line and tier. Length > tier MaxLength = refused.
    //   Exit cells go to `cells` (not placed; only checked for overlap, floor and conflicts).
    std::string pp_pair_underground(std::vector<PMach>& ms, std::vector<PMach>& cells)
    {
        struct PUgTier { const char* dat; int maxLen; };   // Mk1 8, Mk2 10, Mk3 12 (MaxLength)
        const PUgTier tiers[] = { { "DAT_UndergroundBelt", 8 }, { "DAT_UndergroundBelt_2", 10 }, { "DAT_UndergroundBelt_3", 12 } };
        auto pos = [&](const PMach& m) { return "(" + std::to_string(m.x) + "," + std::to_string(m.y) + ")"; };
        std::vector<bool> drop(ms.size(), false);
        for (const PUgTier& t : tiers)
        for (int rot = 0; rot < 4; ++rot)
        {
            const bool xaxis = (rot & 1) == 0;
            std::vector<size_t> v;
            for (size_t i = 0; i < ms.size(); ++i)
                if (ms[i].rot == rot && ms[i].dat == t.dat) v.push_back(i);
            // line = the coordinate across the flow, flow = the coordinate along it (negated for -x / -y so upstream sorts first)
            auto line = [&](size_t i) { return xaxis ? ms[i].y : ms[i].x; };
            auto flow = [&](size_t i) { int f = xaxis ? ms[i].x : ms[i].y; return rot >= 2 ? -f : f; };
            std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) { return line(a) != line(b) ? line(a) < line(b) : flow(a) < flow(b); });
            std::vector<bool> used(v.size(), false);
            for (size_t k = 0; k < v.size(); ++k)
            {
                if (used[k]) continue;
                for (size_t q = k + 1; q < v.size(); ++q)
                {
                    if (line(v[q]) != line(v[k])) break;
                    if (used[q]) continue;
                    const int d = flow(v[q]) - flow(v[k]);
                    if (d < 2 || d % 2) break;   // first unused machine on the line decides: off-grid or same cell = no pair
                    const int len = d / 2 + 1;
                    if (len > t.maxLen) return "underground too long: " + pos(ms[v[k]]) + " to " + pos(ms[v[q]]) + " Length " + std::to_string(len) + " > " + std::to_string(t.maxLen) + " (" + t.dat + ")";
                    ms[v[k]].cfg = pp_cfg_underground(len);
                    if (ms[v[k]].hasHex || ms[v[q]].hasHex) bp_out(widen("PASTE note: ConfigHex ignored for paired underground " + ms[v[k]].cls + " at " + pos(ms[v[k]]) + " (Length from the pair)"));
                    cells.push_back(ms[v[q]]);
                    drop[v[q]] = true;
                    used[k] = used[q] = true;
                    break;
                }
                if (!used[k])
                {
                    // WHY: a lone underground may still be a stand-alone tunnel when the JSON gives ConfigHex 01 00 i32 Length (1..tier max).
                    const PMach& u = ms[v[k]];
                    const int hl = pp_cfg_len(u.hexcfg);
                    if (!u.hasHex)   // WHY: Length unknown (e.g. hand-built one imported from a save): skip it and say so instead of refusing the whole paste
                    {
                        drop[v[k]] = true;
                        g_pp_ugskip += (g_pp_ugskip.empty() ? "" : ", ") + pos(u);
                        bp_out(widen("PASTE skipped underground without partner/Length at " + pos(u)));
                        continue;
                    }
                    if (u.hexcfg.size() != 6 || u.hexcfg[0] != 1 || u.hexcfg[1] != 0 || hl < 1 || hl > t.maxLen)
                        return "underground without partner at " + pos(u) + " (ConfigHex must be 01 00 + i32 Length, Length 1-" + std::to_string(t.maxLen) + ")";
                    ms[v[k]].cfg = u.hexcfg;
                    bp_out(widen("PASTE note: stand-alone underground " + u.cls + " at " + pos(u) + " Length " + std::to_string(hl) + " from ConfigHex"));
                }
            }
        }
        std::vector<PMach> keep;
        for (size_t i = 0; i < ms.size(); ++i) if (!drop[i]) keep.push_back(ms[i]);
        ms.swap(keep);
        return "";
    }

    // WHAT: fills cfg of every new machine (empty = no config). Recipe machines with a Recipe; undergrounds by pairing (exit cells -> cells).
    //       Recipe = the game recipe Name already resolved by pp_resolve_recipes (verbatim, original case; vault rea-machine-config.md s2).
    //       Ports: (Face,Index) -> array index is not in machines.json, so every port is written enabled and the note says so.
    //       "" = OK, else the underground refusal reason.
    std::string pp_encode_cfg(std::vector<PMach>& ms, std::vector<PMach>& cells)
    {
        for (PMach& m : ms)
        {
            if (m.hasHex && !pp_is_ug(m))   // ConfigHex: verbatim, no recipe/ports encoder
            {
                m.cfg = m.hexcfg;
                bp_out(L"PASTE CFG " + widen(m.bcls) + L" source=ConfigHex bytes=" + std::to_wstring(m.cfg.size()));
                continue;
            }
            const PRecipeMach* rt = nullptr;
            for (const PRecipeMach& t : kPasteRecipe) if (m.dat == t.dat) rt = &t;
            if (!rt) continue;
            if (m.recipe.empty())
            {
                if (m.hasPorts) bp_out(L"PASTE note: Ports without Recipe ignored for " + widen(m.bcls));
                continue;
            }
            m.cfg = pp_cfg_recipe(m.recipe, std::vector<uint8_t>(static_cast<size_t>(rt->ports), 1));
            if (m.hasPorts) bp_out(L"PASTE note: port mapping unverified for " + widen(m.bcls) + L", all " + widen(std::to_string(rt->ports)) + L" ports written enabled");
            if (!rt->verified) bp_out(L"PASTE note: config layout assumed (INF, not seen in a save) for " + widen(m.bcls));
        }
        return pp_pair_underground(ms, cells);
    }

    // ----- JSON: small parser (objects, arrays, strings, numbers, true/false/null). Unknown keys are kept and ignored. -----
    struct PJson
    {
        enum Kind { Null, Bool, Num, Str, Arr, Obj } k = Null;
        bool b = false; double n = 0; std::string s;
        std::vector<std::string> keys; std::vector<PJson> vals;   // Obj: keys[i] -> vals[i]; Arr: vals only
        const PJson* get(const char* key) const { for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == key) return &vals[i]; return nullptr; }
    };
    class PJParser
    {
    public:
        std::string err;
        bool parse(const std::string& t, PJson& out)
        {
            t_ = &t; i_ = 0; err.clear();
            ws();
            if (!value(out, 0)) return false;
            ws();
            return i_ == t_->size() || fail("trailing data");
        }
    private:
        const std::string* t_ = nullptr;
        size_t i_ = 0;
        bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at byte " + std::to_string(i_); return false; }
        void ws() { while (i_ < t_->size()) { const char c = (*t_)[i_]; if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break; ++i_; } }
        bool lit(const char* w, PJson& v, PJson::Kind k, bool bv)
        {
            const size_t n = std::strlen(w);
            if (t_->compare(i_, n, w) != 0) return fail("bad literal");
            i_ += n; v.k = k; v.b = bv;
            return true;
        }
        bool value(PJson& v, int depth)
        {
            if (depth > 64) return fail("nesting too deep");
            if (i_ >= t_->size()) return fail("unexpected end");
            const char c = (*t_)[i_];
            if (c == '{') return object(v, depth);
            if (c == '[') return array(v, depth);
            if (c == '"') { v.k = PJson::Str; return str(v.s); }
            if (c == 't') return lit("true", v, PJson::Bool, true);
            if (c == 'f') return lit("false", v, PJson::Bool, false);
            if (c == 'n') return lit("null", v, PJson::Null, false);
            return num(v);
        }
        static bool numch(char c) { return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E'; }
        bool num(PJson& v)
        {
            const size_t st = i_;
            while (i_ < t_->size() && numch((*t_)[i_])) ++i_;
            if (i_ == st) return fail("unexpected character");
            const std::string tok = t_->substr(st, i_ - st);
            char* end = nullptr;
            v.n = std::strtod(tok.c_str(), &end);
            if (end != tok.c_str() + tok.size()) return fail("bad number");
            v.k = PJson::Num;
            return true;
        }
        bool hex4(unsigned& cp)
        {
            if (i_ + 4 > t_->size()) return false;
            cp = 0;
            for (int k = 0; k < 4; ++k)
            {
                const char h = (*t_)[i_++];
                unsigned d = 0;
                if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
                else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
                else return false;
                cp = cp * 16 + d;
            }
            return true;
        }
        static void utf8(std::string& o, unsigned cp)
        {
            if (cp < 0x80) o += static_cast<char>(cp);
            else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
            else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        }
        bool str(std::string& out)
        {
            ++i_;   // opening quote (checked by the caller)
            while (true)
            {
                if (i_ >= t_->size()) return fail("unterminated string");
                const char c = (*t_)[i_++];
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (i_ >= t_->size()) return fail("bad escape");
                const char e = (*t_)[i_++];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned cp = 0;
                    if (!hex4(cp)) return fail("bad \\u escape");
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < t_->size() && (*t_)[i_] == '\\' && (*t_)[i_ + 1] == 'u')
                    {
                        i_ += 2;
                        unsigned lo = 0;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
                }
            }
        }
        bool array(PJson& v, int depth)
        {
            v.k = PJson::Arr;
            ++i_; ws();
            if (i_ < t_->size() && (*t_)[i_] == ']') { ++i_; return true; }
            while (true)
            {
                v.vals.emplace_back();
                ws();
                if (!value(v.vals.back(), depth + 1)) return false;
                ws();
                if (i_ >= t_->size()) return fail("unterminated array");
                const char c = (*t_)[i_++];
                if (c == ']') return true;
                if (c != ',') return fail("expected , or ]");
            }
        }
        bool object(PJson& v, int depth)
        {
            v.k = PJson::Obj;
            ++i_; ws();
            if (i_ < t_->size() && (*t_)[i_] == '}') { ++i_; return true; }
            while (true)
            {
                ws();
                if (i_ >= t_->size() || (*t_)[i_] != '"') return fail("expected key");
                v.keys.emplace_back();
                if (!str(v.keys.back())) return false;
                ws();
                if (i_ >= t_->size() || (*t_)[i_] != ':') return fail("expected :");
                ++i_; ws();
                v.vals.emplace_back();
                if (!value(v.vals.back(), depth + 1)) return false;
                ws();
                if (i_ >= t_->size()) return fail("unterminated object");
                const char c = (*t_)[i_++];
                if (c == '}') return true;
                if (c != ',') return fail("expected , or }");
            }
        }
    };

    // WHAT: integral JSON number into out. false + err when missing / not an integer / out of int range.
    bool pp_int_json(const PJson* v, int& out, std::string& err, const char* what)
    {
        if (!v || v->k != PJson::Num || v->n != std::floor(v->n) || v->n < -2147483648.0 || v->n > 2147483647.0)
        { err = std::string("bad or missing number ") + what; return false; }
        out = static_cast<int>(v->n);
        return true;
    }
    // WHAT: GridLocation{X,Y}, Height, GridRotation of one object (block: itself; machine: its Transform). false + err.
    bool pp_grid_json(const PJson& o, int& x, int& y, int& h, int& rot, std::string& err, const char* what)
    {
        const PJson* gl = o.get("GridLocation");
        if (!gl || gl->k != PJson::Obj) { err = std::string("missing GridLocation in ") + what; return false; }
        return pp_int_json(gl->get("X"), x, err, "GridLocation.X") && pp_int_json(gl->get("Y"), y, err, "GridLocation.Y") &&
               pp_int_json(o.get("Height"), h, err, "Height") && pp_int_json(o.get("GridRotation"), rot, err, "GridRotation");
    }
    // WHAT: parses the website Factory Blueprint JSON into doc (blocks, machines). DAT resolution comes later. false + err.
    bool pp_parse_doc(const std::string& text, PasteDoc& doc, std::string& err)
    {
        PJson root;
        PJParser p;
        if (!p.parse(text, root)) { err = "bad JSON: " + p.err; return false; }
        if (root.k != PJson::Obj) { err = "bad JSON: top level is not an object"; return false; }
        if (const PJson* nm = root.get("DisplayName")) if (nm->k == PJson::Str) doc.name = nm->s;
        const PJson* bl = root.get("Blocks");
        if (!bl || bl->k != PJson::Arr) { err = "bad JSON: Blocks missing or not an array"; return false; }
        for (const PJson& e : bl->vals)
        {
            PBlock b;
            const PJson* c = e.k == PJson::Obj ? e.get("Class") : nullptr;
            if (!c || c->k != PJson::Str) { err = "bad JSON: block without Class"; return false; }
            b.cls = pp_short(c->s);
            if (!pp_grid_json(e, b.x, b.y, b.h, b.rot, err, "block")) return false;
            doc.blocks.push_back(b);
        }
        const PJson* dm = root.get("DefaultMachines");
        if (dm && dm->k != PJson::Arr) { err = "bad JSON: DefaultMachines not an array"; return false; }
        if (dm) for (const PJson& e : dm->vals)
        {
            PMach m;
            const PJson* c = e.k == PJson::Obj ? e.get("Class") : nullptr;
            if (!c || c->k != PJson::Str) { err = "bad JSON: machine without Class"; return false; }
            m.cls = pp_short(c->s);
            if (const PJson* dn = e.get("DefaultName")) if (dn->k == PJson::Str && dn->s.rfind("DAT_", 0) == 0) m.dat = dn->s;
            const PJson* tr = e.get("Transform");
            if (!tr || tr->k != PJson::Obj) { err = "bad JSON: machine without Transform: " + m.cls; return false; }
            if (!pp_grid_json(*tr, m.x, m.y, m.h, m.rot, err, "machine Transform")) return false;
            if (m.h % 8 != 0) { err = "machine Height not a multiple of 8: " + m.cls; return false; }
            if (m.rot < 0 || m.rot > 3) { err = "machine GridRotation not 0-3: " + m.cls; return false; }
            if (const PJson* rc = e.get("Recipe")) if (rc->k == PJson::Str) m.recipe = rc->s;
            if (const PJson* pa = e.get("Ports"))
            {
                if (pa->k != PJson::Arr) { err = "bad JSON: Ports not an array: " + m.cls; return false; }
                m.hasPorts = true;
                for (const PJson& pe : pa->vals)
                {
                    PPort pt;
                    const PJson* fc = pe.get("Face");
                    const PJson* en = pe.get("Enabled");
                    if (!fc || fc->k != PJson::Str || !en || en->k != PJson::Bool) { err = "bad JSON: Ports entry needs Face and Enabled: " + m.cls; return false; }
                    if (!pp_int_json(pe.get("Index"), pt.index, err, "Ports.Index")) return false;
                    pt.face = fc->s;
                    pt.enabled = en->b;
                    m.ports.push_back(pt);
                }
            }
            if (const PJson* ch = e.get("ConfigHex"))
                if (ch->k != PJson::Null)
                {
                    std::string why;
                    if (ch->k != PJson::Str) { err = "bad JSON: ConfigHex not a string: " + m.cls; return false; }
                    if (!pp_hex_cfg(ch->s, m.hexcfg, why)) { err = "ConfigHex refused for " + m.cls + ": " + why; return false; }
                    m.hasHex = true;
                }
            doc.machines.push_back(m);
        }
        return true;
    }

    // WHAT: Buildable asset for a class: first with a footprint row, else the first of that class ("" when none).
    std::string pp_dat_for_cls(const std::string& cls, const PBuildables& bld)
    {
        std::string any;
        for (const auto& b : bld)
            if (b.second == cls) { if (pp_mach_tab(b.first)) return b.first; if (any.empty()) any = b.first; }
        return any;
    }
    // WHAT: sync stale selection (replace mode). Indices of existing station machines the blueprint no longer has: not kept (kept = identical or
    //       recipe-only difference, changed in place), class resolves to a live Buildable (same lookup as the footprints), and not a never-removed
    //       class (Dock, SolarPanel, Console, ItemCrate, Elevator, Teleporter). Pure.
    std::vector<size_t> pp_stale(const std::vector<PMach>& ex, const std::vector<PMach>& kept, const PBuildables& bld)
    {
        static const char* const kNever[] = { "Dock", "SolarPanel", "Console", "ItemCrate", "Elevator", "Teleporter" };
        std::vector<size_t> st;
        for (size_t k = 0; k < ex.size(); ++k)
        {
            const PMach& e = ex[k];
            bool isKept = false;
            for (const PMach& m : kept) if (m.id == e.id) isKept = true;
            if (isKept || pp_dat_for_cls(e.bcls, bld).empty()) continue;
            bool never = false;
            for (const char* w : kNever) if (e.bcls.find(w) != std::string::npos) never = true;
            if (!never) st.push_back(k);
        }
        return st;
    }
    // WHAT: resolves each new machine to its Buildable (DAT_ name, else by class), class and rotated footprint. "" or the error.
    std::string pp_resolve(std::vector<PMach>& ms, const PBuildables& bld)
    {
        // WHY: machines with no Buildable (dock, solar panel...) belong to their station block, not the player's build menu: skipped, logged.
        for (size_t i = 0; i < ms.size();)
        {
            if (ms[i].dat.empty()) ms[i].dat = pp_dat_for_cls(ms[i].cls, bld);
            if (!ms[i].dat.empty()) { ++i; continue; }
            bp_out(L"PASTE skipped (block-built, no Buildable): " + widen(ms[i].cls));
            ms.erase(ms.begin() + i);
        }
        for (PMach& m : ms)
        {
            const PTabM* t = pp_mach_tab(m.dat);
            if (!t) return "unknown buildable (no footprint row): " + m.dat;
            std::string bc;
            for (const auto& b : bld) if (b.first == m.dat) { bc = b.second; break; }
            if (bc.empty()) return "buildable not in this game: " + m.dat;
            m.bdat = m.dat; m.bcls = bc;
            pp_fp(t->w, t->h, m.rot, m.w, m.hh);
        }
        return "";
    }

    // ----- live reads (under seh_run) -----
    FProperty* pp_field(UStruct* st, const wchar_t* name)
    {
        for (UStruct* u = st; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty()) if (p->GetName() == name) return p;
        return nullptr;
    }
    bool pp_int(UStruct* st, const uint8_t* base, const wchar_t* name, int& v)
    {
        FProperty* p = pp_field(st, name);
        if (!p || !CastField<FIntProperty>(p)) return false;
        v = *reinterpret_cast<const int32_t*>(base + p->GetOffset_Internal());
        return true;
    }
    const uint8_t* pp_struct(UStruct* st, const uint8_t* base, const wchar_t* name, UStruct*& out)
    {
        FProperty* p = pp_field(st, name);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return nullptr;
        out = sp->GetStruct();
        return base + p->GetOffset_Internal();
    }
    UObject* pp_obj(UStruct* st, const uint8_t* base, const wchar_t* name)
    {
        FProperty* p = pp_field(st, name);
        if (!p || !CastField<FObjectProperty>(p)) return nullptr;
        return *reinterpret_cast<UObject* const*>(base + p->GetOffset_Internal());
    }
    // WHAT: GridTransform-like struct: GridLocation{X,Y}, Height, GridRotation (missing fields stay as given).
    void pp_transform(UStruct* st, const uint8_t* base, int& x, int& y, int& h, int& rot)
    {
        UStruct* gl = nullptr;
        if (const uint8_t* g = pp_struct(st, base, L"GridLocation", gl)) { pp_int(gl, g, L"X", x); pp_int(gl, g, L"Y", y); }
        pp_int(st, base, L"Height", h);
        pp_int(st, base, L"GridRotation", rot);
    }
    // WHAT: Blueprint.Blocks of one owner (BuildBlueprint struct named Blueprint, inherited from BuildingFrame). Under seh_run.
    void pp_read_blocks(UObject* owner, std::vector<PBlock>& out)
    {
        const uint8_t* ob = reinterpret_cast<const uint8_t*>(owner);
        for (UStruct* u = owner->GetClassPrivate(); u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                auto* sp = CastField<FStructProperty>(p);
                if (!sp || !sp->GetStruct() || p->GetName() != STR("Blueprint")) continue;
                if (sp->GetStruct()->GetName().find(L"BuildBlueprint") == std::wstring::npos) continue;
                UStruct* bst = sp->GetStruct();
                const uint8_t* bb = ob + p->GetOffset_Internal();
                FProperty* bf = pp_field(bst, L"Blocks");
                auto* ap = bf ? CastField<FArrayProperty>(bf) : nullptr;
                auto* ip = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
                if (!ip || !ip->GetStruct()) return;
                const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(bb + bf->GetOffset_Internal());
                const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
                for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
                {
                    const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
                    PBlock b;
                    if (UObject* k = pp_obj(ip->GetStruct(), e, L"Class")) b.cls = pp_short(to_utf8(k->GetName()));
                    UStruct* tst = nullptr;
                    if (const uint8_t* t = pp_struct(ip->GetStruct(), e, L"Transform", tst)) pp_transform(tst, t, b.x, b.y, b.h, b.rot);
                    out.push_back(b);
                }
                return;
            }
    }
    // WHAT: Buildable list (name, class). Existing machine footprint: its Buildable's table row (rotated), else 1x1.
    void pp_fp_existing(PMach& e, const PBuildables& bld)
    {
        const PTabM* t = pp_mach_tab(pp_dat_for_cls(e.bcls, bld));
        if (t) pp_fp(t->w, t->h, e.rot, e.w, e.hh);
        else { e.w = 1; e.hh = 1; }
    }
    // WHAT: one Blueprint.Machines entry of the live station: class (if readable), grid transform, SerializedMachineConfig bytes.
    struct PBpMach { PMach m; bool hasCls = false, hasCfg = false; std::vector<uint8_t> cfg; };
    // WHAT: Blueprint.Machines of one owner (struct Blueprint inherited from BuildingFrame, like Blocks). Field names Machines / Class / Transform /
    //       GridTransform / SerializedMachineConfig are UNVERIFIED for the Blueprint; a missing field just leaves the value empty (PASTE CFG shows live=none).
    void pp_read_bp_machines(UObject* owner, std::vector<PBpMach>& out)
    {
        const uint8_t* ob = reinterpret_cast<const uint8_t*>(owner);
        for (UStruct* u = owner->GetClassPrivate(); u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                auto* sp = CastField<FStructProperty>(p);
                if (!sp || !sp->GetStruct() || p->GetName() != STR("Blueprint")) continue;
                if (sp->GetStruct()->GetName().find(L"BuildBlueprint") == std::wstring::npos) continue;
                UStruct* bst = sp->GetStruct();
                const uint8_t* bb = ob + p->GetOffset_Internal();
                FProperty* mf = pp_field(bst, L"Machines");
                auto* ap = mf ? CastField<FArrayProperty>(mf) : nullptr;
                auto* ip = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
                if (!ip || !ip->GetStruct()) return;
                const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(bb + mf->GetOffset_Internal());
                const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
                UStruct* et = ip->GetStruct();
                for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
                {
                    const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
                    PBpMach b;
                    if (UObject* k = pp_obj(et, e, L"Class")) { b.m.bcls = pp_short(to_utf8(k->GetName())); b.hasCls = true; }
                    UStruct* tst = nullptr;
                    const uint8_t* t = pp_struct(et, e, L"Transform", tst);
                    if (!t) t = pp_struct(et, e, L"GridTransform", tst);
                    if (t && tst) pp_transform(tst, t, b.m.x, b.m.y, b.m.h, b.m.rot);
                    if (FProperty* cf = pp_field(et, L"SerializedMachineConfig"))
                    {
                        auto* ca = CastField<FArrayProperty>(cf);
                        if (ca && CastField<FByteProperty>(ca->GetInner()))
                        {
                            const auto* cta = reinterpret_cast<const TArray<uint8_t>*>(e + cf->GetOffset_Internal());
                            if (cta->GetData()) b.cfg.assign(cta->GetData(), cta->GetData() + cta->Num());
                            b.hasCfg = true;
                        }
                    }
                    out.push_back(b);
                }
                return;
            }
    }

    // WHAT: full read for bp_paste, under seh_run. station 0 = the one station with a Blueprint that has blocks (several -> err, lists ids).
    void pp_scan_fn(void* p)
    {
        auto* c = static_cast<PasteCtx*>(p);
        std::vector<UObject*> bl;
        UObjectGlobals::FindAllOf(STR("Buildable"), bl);
        for (UObject* b : bl)
            if (alive(b) && b->GetName().rfind(STR("Default__"), 0) != 0)
                if (UObject* k = pp_obj(b->GetClassPrivate(), reinterpret_cast<const uint8_t*>(b), L"Class"))
                    c->bld.emplace_back(to_utf8(b->GetName()), pp_short(to_utf8(k->GetName())));
        std::vector<UObject*> sts, live;
        UObjectGlobals::FindAllOf(STR("StationObject"), sts);
        for (UObject* s : sts) if (alive(s) && s->GetName().rfind(STR("Default__"), 0) != 0) live.push_back(s);
        UObject* st = nullptr;
        if (c->station > 0)
        {
            for (UObject* s : live) if (obj_id(s) == c->station) st = s;
            if (!st) { c->err = "no live StationObject with id " + std::to_string(c->station); return; }
        }
        else
        {
            std::vector<UObject*> with;
            for (UObject* s : live) { std::vector<PBlock> tmp; pp_read_blocks(s, tmp); if (!tmp.empty()) with.push_back(s); }
            if (with.empty()) { c->err = "no station with a Blueprint that has blocks"; return; }
            if (with.size() > 1)   // WHY: NPC stations are BP_Abandoned*/BPO_* classes; the player's station is BP_StationObject_C
            {
                std::vector<UObject*> mine;
                for (UObject* s : with) if (s->GetClassPrivate()->GetName() == STR("BP_StationObject_C")) mine.push_back(s);
                if (!mine.empty()) with = mine;
            }
            if (with.size() > 1)
            {
                for (UObject* s : with) c->cands += (c->cands.empty() ? "" : ", ") + std::to_string(obj_id(s));
                c->err = "several stations with a Blueprint, use station <id>: " + c->cands;
                return;
            }
            st = with[0];
            c->station = obj_id(st);
        }
        pp_read_blocks(st, c->blocks);
        std::map<int, UObject*> byid;
        std::vector<UObject*> sv;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), sv);
        for (UObject* o : sv) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(o) > 0) byid[obj_id(o)] = o;
        for (int id : station_ids(st, STR("MachineIDs")))
        {
            auto it = byid.find(id);
            if (it == byid.end() || !alive(it->second)) continue;
            UObject* m = it->second;
            PMach e;
            e.id = id;
            e.bcls = pp_short(to_utf8(m->GetClassPrivate()->GetName()));
            e.cls = e.bcls;
            UStruct* gst = nullptr;
            if (const uint8_t* gt = pp_struct(m->GetClassPrivate(), reinterpret_cast<const uint8_t*>(m), L"GridTransform", gst)) pp_transform(gst, gt, e.x, e.y, e.h, e.rot);
            pp_fp_existing(e, c->bld);
            c->existing.push_back(e);
        }
        // WHY: live config of each existing machine = its Blueprint.Machines entry (same class and GridTransform). Used by the keep / config diff.
        std::vector<PBpMach> bpm;
        pp_read_bp_machines(st, bpm);
        for (PMach& e : c->existing)
            for (const PBpMach& b : bpm)
                if (!e.hasLive && b.hasCfg && b.m.x == e.x && b.m.y == e.y && b.m.h == e.h && b.m.rot == e.rot && (!b.hasCls || b.m.bcls == e.bcls)) { e.hasLive = true; e.live = b.cfg; }
    }

    // WHAT: Windows clipboard text (CF_UNICODETEXT) as UTF-8. false when there is no text or the clipboard is busy.
    bool pp_clip(std::string& out)
    {
        if (!OpenClipboard(nullptr)) return false;
        bool ok = false;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) { out = to_utf8(std::wstring(w)); GlobalUnlock(h); ok = true; }
        CloseClipboard();
        return ok;
    }
    // WHAT: the one result line: logged and written (overwrite) to dlls\PasteStatus.txt for the Lua UI.
    void pp_status(const std::string& line)
    {
        bp_out(widen(line));
        try { write_atomic(get_dll_dir() / "PasteStatus.txt", line + "\n"); } catch (...) {}
    }
    std::wstring pp_fail(const std::string& why) { pp_status("error: " + why); bp_flush(); return widen("error: " + why); }
    // WHAT: status head: "ok placed P, kept K, recipes changed R[, skipped S (not enough items: ...)], refused F[; skipped underground ...]".
    //       sync: "ok sync: placed P, kept K, removed R stale, recipes changed C[, skipped ...], refused F[; ...]".
    std::string pp_head(int placed, int refused)
    {
        std::string s = g_bpaste.sync
            ? "ok sync: placed " + std::to_string(placed) + ", kept " + std::to_string(g_bpaste.kept.size()) + ", removed " + std::to_string(g_bpaste.staleN) + " stale, recipes changed " + std::to_string(g_bpaste.recChanged)
            : "ok placed " + std::to_string(placed) + ", kept " + std::to_string(g_bpaste.kept.size()) + ", recipes changed " + std::to_string(g_bpaste.recChanged);
        if (!g_bpaste.skipped.empty()) s += ", skipped " + std::to_string(g_bpaste.skipped.size()) + " (not enough items: " + g_bpaste.short_txt + ")";
        s += ", refused " + std::to_string(refused);
        if (!g_pp_ugskip.empty()) s += "; skipped underground without Length at " + g_pp_ugskip;
        return s;
    }

    // WHAT: display label of an item object, same rule as bp_inv_read ("kind/Name", or the kind when it equals the name). UTF-8.
    std::string pp_item_label(UObject* it)
    {
        std::wstring dn = item_kind(it);
        const std::wstring on = it->GetName();
        if (dn != on) dn += L"/" + on;
        return to_utf8(dn);
    }
    // WHAT: creative flag read. state 1 = creative, 0 = not, -1 = no readable bCreative. src = which object and value (logged).
    struct PCrCtx { int state = -1; std::string src; };
    void pp_creative_fn(void* p)
    {
        auto* c = static_cast<PCrCtx*>(p);
        auto flag = [&](UObject* o, const char* tag) -> bool
        {
            if (!alive(o)) return false;
            FProperty* f = pp_field(o->GetClassPrivate(), L"bCreative");
            auto* bp = f ? CastField<FBoolProperty>(f) : nullptr;
            if (!bp) return false;
            const uint8_t v = *(reinterpret_cast<const uint8_t*>(o) + f->GetOffset_Internal()) & bp->GetFieldMask();
            c->state = v ? 1 : 0;
            c->src = std::string(tag) + ".bCreative=" + (v ? "1" : "0");
            return true;
        };
        if (!flag(first_of(STR("DMSPlayerObject")), "DMSPlayerObject")) flag(first_of(STR("BP_PlayerController_C")), "BP_PlayerController_C");
        if (c->state < 0) c->src = "no readable bCreative (DMSPlayerObject, BP_PlayerController_C)";
    }
    // WHAT: Buildable ItemCost per DAT in `want` (item label -> amount, summed). found = DATs whose asset has an ItemCost property. Under seh_run.
    // raw = item key is the raw object name (bp_import); else the display label (pp_item_label, bp_paste).
    struct PCostCtx { std::set<std::string> want, found; std::map<std::string, PCostMap> cost; bool raw = false; };
    void pp_cost_fn(void* p)
    {
        auto* c = static_cast<PCostCtx*>(p);
        std::vector<UObject*> bl;
        UObjectGlobals::FindAllOf(STR("Buildable"), bl);
        for (UObject* b : bl)
        {
            if (!alive(b) || b->GetName().rfind(STR("Default__"), 0) == 0) continue;
            const std::string dat = to_utf8(b->GetName());
            if (!c->want.count(dat) || c->found.count(dat)) continue;
            FProperty* f = pp_field(b->GetClassPrivate(), L"ItemCost");
            auto* ap = f ? CastField<FArrayProperty>(f) : nullptr;
            auto* sp = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
            if (!sp || !sp->GetStruct()) continue;
            c->found.insert(dat);
            const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(reinterpret_cast<const uint8_t*>(b) + f->GetOffset_Internal());
            const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
            for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
            {
                const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
                uint64_t item = 0;
                int32_t amt = 0;
                int off = 0;
                if (!stack_read(sp->GetStruct(), e, item, amt, off) || amt <= 0 || item == 0) continue;
                UObject* it = reinterpret_cast<UObject*>(static_cast<uintptr_t>(item));
                if (alive(it)) c->cost[dat][c->raw ? to_utf8(it->GetName()) : pp_item_label(it)] += amt;
            }
        }
    }
    // WHAT: cost check before placing. Creative = skipped. Else: need per item (Buildable ItemCost of the new machines) vs the player inventory
    //       (bp_snap "player|" keys); greedy in input order via pp_afford. ms = affordable machines (input order), skipped = the rest,
    //       short_txt = their shortfall. Logs PASTE COST lines. "" or the refusal (snapshot / cost read failed, nothing placed).
    std::string pp_cost_filter(std::vector<PMach>& ms, std::vector<PMach>& skipped, std::string& short_txt)
    {
        PCrCtx cr;
        if (!seh_run(pp_creative_fn, &cr)) bp_out(L"FAULT bp_paste creative flag " + seh_describe(g_seh));
        bp_out(widen("PASTE COST creative: " + cr.src + (cr.state == 1 ? " -> check skipped" : " -> check on")));
        if (cr.state == 1 || ms.empty()) return "";
        BpSnap before;
        if (!bp_snap(before)) return "snapshot failed, nothing placed";
        PCostCtx cc;
        for (const PMach& m : ms) cc.want.insert(m.bdat);
        if (!seh_run(pp_cost_fn, &cc)) { bp_out(L"FAULT bp_paste cost read " + seh_describe(g_seh)); return "cost read fault, nothing placed (see BPProbe.txt)"; }
        PCostMap have, need;
        for (const auto& kv : before.inv)
            if (kv.first.rfind(L"player|", 0) == 0) have[to_utf8(kv.first.substr(7))] += kv.second;
        std::vector<PCostMap> per;
        for (const PMach& m : ms)
        {
            if (!cc.found.count(m.bdat)) bp_out(widen("PASTE COST no ItemCost read for " + m.bdat + " (counted as free)"));
            per.push_back(cc.cost[m.bdat]);
            for (const auto& kv : cc.cost[m.bdat]) need[kv.first] += kv.second;
        }
        for (const auto& kv : need)
            bp_out(widen("PASTE COST need " + kv.first + " x" + std::to_string(kv.second) + " have " + std::to_string(have[kv.first])));
        PCostMap shortm;
        const std::vector<bool> keep = pp_afford(per, have, shortm);
        std::vector<PMach> ok;
        for (size_t i = 0; i < ms.size(); ++i)
        {
            if (keep[i]) { ok.push_back(ms[i]); continue; }
            skipped.push_back(ms[i]);
            bp_out(widen("PASTE COST skipped " + ms[i].bcls + " " + pp_where(ms[i]) + " (not enough items)"));
        }
        ms = ok;
        for (const auto& kv : shortm) short_txt += (short_txt.empty() ? "" : ", ") + kv.first + " x" + std::to_string(kv.second);
        return "";
    }

    // WHAT: one BuildInput per new machine (station = attach target), like bp_belts. Returns the number called.
    int pp_place_all(const std::vector<PMach>& ms, long long station)
    {
        int placed = 0;
        for (const PMach& m : ms)
        {
            BpSend s;
            s.tok = { "bp_send", "BuildInput", "0", "buildable=" + m.bdat, "attachtargetid=" + std::to_string(station),
                      "gridtransform.gridlocation.x=" + std::to_string(m.x), "gridtransform.gridlocation.y=" + std::to_string(m.y),
                      "gridtransform.height=" + std::to_string(m.h), "gridtransform.gridrotation=" + std::to_string(m.rot) };
            if (!m.cfg.empty()) s.tok.push_back("machineconfigdata=" + pp_hex(m.cfg));
            g_bpp.pending = false;
            if (!seh_run(bp_send_fn, &s)) bp_out(L"FAULT bp_paste " + seh_describe(g_seh));
            if (s.called) ++placed; else bp_out(s.out);
        }
        return placed;
    }
    // WHAT: sends the placement of g_bpaste (one before snapshot; bp_place_tick logs the +1.5 s / +5 s diffs), arms the +3 s verify. false when the snapshot failed.
    bool pp_do_place()
    {
        BpSnap before;
        if (!bp_snap(before)) { pp_status("error: snapshot failed, nothing placed"); g_bpaste.active = false; return false; }
        g_bpaste.placed = pp_place_all(g_bpaste.ms, g_bpaste.station);
        if (g_bpaste.sync)
            bp_out(widen("PASTE SYNC removed " + std::to_string(g_bpaste.staleN) + " stale, kept " + std::to_string(g_bpaste.kept.size()) + ", placed " +
                         std::to_string(g_bpaste.placed) + ", recipes changed " + std::to_string(g_bpaste.recChanged)));
        g_bpp.pending = true; g_bpp.t0 = GetTickCount64(); g_bpp.stage = 0; g_bpp.ref = before;
        const int n = static_cast<int>(g_bpaste.ms.size());
        pp_status(pp_head(g_bpaste.placed, n - g_bpaste.placed) + " (verify at +3 s)");
        g_bpaste.stage = 2;
        g_bpaste.t_next = GetTickCount64() + 3000;
        return true;
    }
    // WHAT: one live station machine for the config check: id, class, grid transform, Length (underground only).
    struct PLiveCfg { PMach m; bool hasLen = false; int len = 0; };
    // WHAT: result of pp_cfg_fn (under seh_run). err = station not found.
    struct PCfgCtx { long long station = 0; std::string err; std::vector<PLiveCfg> live; std::vector<PBpMach> bp; };

    // WHAT: under seh_run: live station (by id) machines with class, grid transform and Length (underground), and its Blueprint.Machines.
    void pp_cfg_fn(void* p)
    {
        auto* c = static_cast<PCfgCtx*>(p);
        std::vector<UObject*> sts;
        UObjectGlobals::FindAllOf(STR("StationObject"), sts);
        UObject* st = nullptr;
        for (UObject* s : sts) if (alive(s) && s->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(s) == c->station) st = s;
        if (!st) { c->err = "no live StationObject with id " + std::to_string(c->station); return; }
        pp_read_bp_machines(st, c->bp);
        std::map<int, UObject*> byid;
        std::vector<UObject*> sv;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), sv);
        for (UObject* o : sv) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(o) > 0) byid[obj_id(o)] = o;
        for (int id : station_ids(st, STR("MachineIDs")))
        {
            auto it = byid.find(id);
            if (it == byid.end() || !alive(it->second)) continue;
            UObject* m = it->second;
            PLiveCfg r;
            r.m.id = id;
            r.m.bcls = pp_short(to_utf8(m->GetClassPrivate()->GetName()));
            UStruct* gst = nullptr;
            if (const uint8_t* gt = pp_struct(m->GetClassPrivate(), reinterpret_cast<const uint8_t*>(m), L"GridTransform", gst)) pp_transform(gst, gt, r.m.x, r.m.y, r.m.h, r.m.rot);
            if (r.m.bcls.find("Underground") != std::string::npos)
                r.hasLen = pp_int(m->GetClassPrivate(), reinterpret_cast<const uint8_t*>(m), L"Length", r.len);
            c->live.push_back(r);
        }
    }

    // WHAT: +3 s check: every placed machine must be on the station with the same class and GridLocation. Logs VERIFY and updates the status line.
    void pp_verify()
    {
        PasteCtx c;
        c.station = g_bpaste.station;
        if (!seh_run(pp_scan_fn, &c)) { bp_out(L"PASTE VERIFY FAULT " + seh_describe(g_seh)); return; }
        std::vector<bool> used(c.existing.size(), false);
        int present = 0, miss = 0;
        std::string missing;
        std::vector<PMach> chk = g_bpaste.ms;   // placed + kept (kept = identical machine already there, counts as present)
        chk.insert(chk.end(), g_bpaste.kept.begin(), g_bpaste.kept.end());
        for (const PMach& m : chk)
        {
            bool found = false;
            for (size_t k = 0; k < c.existing.size() && !found; ++k)
                if (!used[k] && c.existing[k].bcls == m.bcls && c.existing[k].x == m.x && c.existing[k].y == m.y) { used[k] = true; found = true; }
            if (found) { ++present; continue; }
            bp_out(widen("PASTE VERIFY MISSING " + m.bcls + " " + pp_where(m)));
            if (miss++ < 10) missing += (missing.empty() ? "" : ", ") + m.bcls + "@(" + std::to_string(m.x) + "," + std::to_string(m.y) + ")";
        }
        const int n = static_cast<int>(chk.size());
        bp_out(widen("PASTE VERIFY: " + std::to_string(present) + " of " + std::to_string(n) + " present, missing: " + (missing.empty() ? std::string("none") : missing)));
        // config round trip: sent bytes (doc) vs live SerializedMachineConfig of the same machine (class + GridLocation + Height)
        PCfgCtx cc;
        cc.station = g_bpaste.station;
        if (!seh_run(pp_cfg_fn, &cc)) bp_out(L"PASTE CFG FAULT " + seh_describe(g_seh));
        else if (!cc.err.empty()) bp_out(widen("PASTE CFG " + cc.err));
        else
        {
            std::vector<bool> lu(cc.live.size(), false), bu(cc.bp.size(), false);
            std::vector<PMach> cfgchk = g_bpaste.ms;   // placed, then the kept machines whose config was changed
            for (const PCfgChg& ch : g_bpaste.chg) cfgchk.push_back(ch.m);
            for (const PMach& m : cfgchk)
            {
                int li = -1;
                for (size_t k = 0; k < cc.live.size() && li < 0; ++k)
                    if (!lu[k] && cc.live[k].m.bcls == m.bcls && cc.live[k].m.x == m.x && cc.live[k].m.y == m.y) li = static_cast<int>(k);
                if (li < 0) continue;   // not placed: already in the missing list
                lu[li] = true;
                const PLiveCfg& lv = cc.live[li];
                int bi = -1;
                for (size_t k = 0; k < cc.bp.size() && bi < 0; ++k)
                    if (!bu[k] && cc.bp[k].m.x == m.x && cc.bp[k].m.y == m.y && cc.bp[k].m.h == m.h && (!cc.bp[k].hasCls || cc.bp[k].m.bcls == m.bcls)) bi = static_cast<int>(k);
                if (bi >= 0) bu[bi] = true;
                const std::string sent = m.cfg.empty() ? std::string("-") : pp_hex(m.cfg);
                const std::string live = (bi < 0 || !cc.bp[bi].hasCfg) ? std::string("none") : cc.bp[bi].cfg.empty() ? std::string("-") : pp_hex(cc.bp[bi].cfg);
                const bool same = bi >= 0 && cc.bp[bi].hasCfg && (m.cfg == cc.bp[bi].cfg);
                std::string line = "PASTE CFG " + std::to_string(lv.m.id) + " " + m.bcls + " sent=" + sent + " live=" + live + " " + (same ? "MATCH" : "DIFF");
                if (lv.hasLen) line += " Length=" + std::to_string(lv.len) + " sent_Length=" + std::to_string(pp_cfg_len(m.cfg));
                bp_out(widen(line));
            }
        }
        pp_status(pp_head(g_bpaste.placed, static_cast<int>(g_bpaste.ms.size()) - g_bpaste.placed) + "; verify " + std::to_string(present) + " of " + std::to_string(n) + " (missing: " + (missing.empty() ? std::string("none") : missing) + ")");
    }

    // ----- recipe names (live, under seh_run): the config string is the Recipe asset's Name property, verbatim -----
    // WHAT: one Recipe to resolve. site = JSON Recipe text; rec = REC_* id (site form) or the plain name (plain form).
    //       Filled by pp_recipe_fn: objname = the live asset name, name = its Name property.
    struct PRecReq { std::string site, rec; bool plain = false; std::string objname, name; UObject* obj = nullptr; };
    struct PRecCtx { std::vector<PRecReq> reqs; std::string err; std::wstring log; };
    std::string pp_lo(std::string s) { for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32); return s; }
    // WHAT: Name property of one live Recipe (FStr or FName, verbatim). false when missing, another type, or empty.
    bool pp_recipe_name(UObject* o, std::string& out)
    {
        FProperty* f = pp_field(o->GetClassPrivate(), L"Name");
        if (!f) return false;
        const uint8_t* d = reinterpret_cast<const uint8_t*>(o) + f->GetOffset_Internal();
        std::wstring w;
        if (CastField<FStrProperty>(f)) { const auto& a = reinterpret_cast<const FString*>(d)->GetCharArray(); if (a.Num() > 0 && a.GetData()) w = std::wstring(a.GetData()); }
        else if (CastField<FNameProperty>(f)) w = std::wstring(reinterpret_cast<const FName*>(d)->ToString());
        else return false;
        out = to_utf8(w);
        return !out.empty();
    }
    // WHAT: under seh_run: every live Recipe asset (not Default__). Plain form: Name equal (case-insensitive) to req.rec. Site form: asset
    //       name equal (case-insensitive) to the REC id, or to the id without "REC_". Exactly one match each, else err (no guessing).
    void pp_recipe_fn(void* p)
    {
        auto* c = static_cast<PRecCtx*>(p);
        std::vector<UObject*> all, live;
        UObjectGlobals::FindAllOf(STR("Recipe"), all);
        for (UObject* o : all) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0) live.push_back(o);
        static bool layout_logged = false;   // WHY: the Recipe property list is logged once per game session (the Name property is confirmed there)
        if (!layout_logged && !live.empty())
        {
            layout_logged = true;
            std::set<UStruct*> seen;
            c->log += L"RECIPE LAYOUT " + live[0]->GetClassPrivate()->GetName() + L"\n";
            bp_layout(c->log, live[0]->GetClassPrivate(), L"  ", 0, seen);
        }
        for (PRecReq& r : c->reqs)
        {
            const std::string want = pp_lo(r.rec);
            const std::string bare = (want.rfind("rec_", 0) == 0) ? want.substr(4) : want;
            UObject* hit = nullptr;
            int n = 0;
            for (UObject* o : live)
            {
                bool m = false;
                if (r.plain) { std::string rn; m = pp_recipe_name(o, rn) && pp_lo(rn) == want; }
                else { const std::string on = pp_lo(to_utf8(o->GetName())); m = on == want || on == bare; }
                if (m) { if (hit != o) ++n; hit = o; }
            }
            if (n == 0) { c->err = "recipe not found: " + r.site; return; }
            if (n > 1) { c->err = "recipe ambiguous: " + r.site; return; }
            if (!pp_recipe_name(hit, r.name)) { c->err = "recipe Name unreadable: " + r.site; return; }
            r.objname = to_utf8(hit->GetName());
            r.obj = hit;
        }
    }
    // WHAT: live Recipe asset whose Name equals `name` (exactly one, same rule as pp_recipe_fn plain form). nullptr when none / ambiguous. Call under seh_run.
    UObject* pp_recipe_obj(const std::string& name)
    {
        PRecCtx c;
        PRecReq r;
        r.site = r.rec = name;
        r.plain = true;
        c.reqs.push_back(r);
        pp_recipe_fn(&c);
        return c.err.empty() ? c.reqs[0].obj : nullptr;
    }
    // WHAT: resolves the Recipe of every recipe machine in ms to the game Name: site ids via pp_site_rec (offline table), plain names as given.
    //       Sets m.recipe = game Name (verbatim). "" or the refusal ("recipe not found: <site id>"). Logs PASTE RECIPE per machine.
    std::string pp_resolve_recipes(std::vector<PMach>& ms)
    {
        PRecCtx c;
        std::vector<PMach*> who;
        for (PMach& m : ms)
        {
            const PRecipeMach* rt = pp_recipe_mach(m.dat);
            if (!rt || m.recipe.empty() || m.hasHex) continue;   // ConfigHex: recipe lookup skipped
            PRecReq r;
            r.site = m.recipe;
            if (m.recipe.find('>') == std::string::npos) { r.plain = true; r.rec = m.recipe; }
            else
            {
                std::string why;
                if (!pp_site_rec(m.recipe, rt->crafter, r.rec, why))
                {
                    bp_out(L"PASTE RECIPE site=" + widen(m.recipe) + L" refused: " + widen(why));
                    return "recipe not found: " + m.recipe;
                }
            }
            c.reqs.push_back(r);
            who.push_back(&m);
        }
        if (c.reqs.empty()) return "";
        if (!seh_run(pp_recipe_fn, &c)) { bp_out(L"FAULT bp_paste " + seh_describe(g_seh)); return "live recipe read fault (see BPProbe.txt)"; }
        bp_out(c.log);
        if (!c.err.empty()) return c.err;
        for (size_t i = 0; i < c.reqs.size(); ++i)
        {
            const PRecReq& r = c.reqs[i];
            who[i]->recipe = r.name;
            bp_out(widen("PASTE RECIPE site=" + r.site + " -> " + r.objname + " -> name=" + r.name));
        }
        return "";
    }

    // WHAT: config decision for each kept machine. Recipe machines with config bytes: pp_cfg_diff against the live bytes. Unknown live config = logged,
    //       kept as is. Other classes (splitter, dock, underground...) are kept without comparison (logged). Fills chg and recChanged.
    void pp_kept_changes(const std::vector<PMach>& kept, std::vector<PCfgChg>& chg, int& recChanged)
    {
        for (const PMach& m : kept)
        {
            const std::string id = std::to_string(m.id);
            if (m.cfg.empty()) continue;
            if (!pp_recipe_mach(m.dat)) { bp_out(widen("PASTE KEEP note: config not compared for " + m.bcls + " id=" + id + " (kept as is)")); continue; }
            const PCfgDiff d = pp_cfg_diff(m.cfg, m.live);
            if (!d.ok) { bp_out(widen("PASTE CFG " + id + " " + m.bcls + " live unknown (" + d.why + "), kept as is")); continue; }
            // WHY: the site port order -> game port index mapping is unverified and the encoder writes all ports enabled, so a port the user
            //      disabled in the game would be switched back on: port diffs are NOT applied (only logged) until the mapping is verified.
            if (!d.ports.empty()) bp_out(widen("PASTE note: " + std::to_string(d.ports.size()) + " port difference(s) on id " + id + " not applied (port mapping unverified)"));
            if (!d.recipe) continue;
            PCfgChg c;
            c.m = m;
            c.recipe = d.recipe;
            c.oldRec = d.liveRec;
            c.newRec = d.targetRec;
            chg.push_back(c);
            if (d.recipe) ++recChanged;
        }
    }
    // WHAT: sends the config changes of kept machines, one bp_send at a time (receiver = the machine id). SetRecipeInput for a recipe change,
    //       MachinePortInput (PortIndex) per differing port. Logs PASTE RECIPE CHANGE / PASTE PORT TOGGLE; refused sends print their reason.
    void pp_send_changes(const std::vector<PCfgChg>& chg)
    {
        for (const PCfgChg& c : chg)
        {
            const std::string id = std::to_string(c.m.id);
            if (c.recipe)
            {
                BpSend s;
                s.tok = { "bp_send", "SetRecipeInput", id, "recipe=" + c.newRec };
                g_bpp.pending = false;
                if (!seh_run(bp_send_fn, &s)) bp_out(L"FAULT bp_paste " + seh_describe(g_seh));
                bp_out(widen("PASTE RECIPE CHANGE id=" + id + " old=" + c.oldRec + " new=" + c.newRec + (s.called ? " sent" : " REFUSED")));
                if (!s.called) bp_out(s.out);
            }
            for (int i : c.ports)
            {
                BpSend s;
                s.tok = { "bp_send", "MachinePortInput", id, "portindex=" + std::to_string(i) };
                g_bpp.pending = false;
                if (!seh_run(bp_send_fn, &s)) bp_out(L"FAULT bp_paste " + seh_describe(g_seh));
                bp_out(widen("PASTE PORT TOGGLE id=" + id + " port=" + std::to_string(i) + (s.called ? " sent" : " REFUSED")));
                if (!s.called) bp_out(s.out);
            }
        }
    }

    // WHAT: bp_paste <path|clip> [station <id>] [replace]. Validates everything first; places nothing on any refusal. Writes (only on user command).
    // WHAT: fingerprint of the live station (FNV-1a over id, bcls, x, y, h, rot of each machine, in order). Plan staleness check.
    uint64_t pp_fingerprint(const std::vector<PMach>& ex)
    {
        uint64_t hv = 1469598103934665603ULL;
        auto mix8 = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { hv ^= (v >> (8 * i)) & 0xffu; hv *= 1099511628211ULL; } };
        for (const PMach& m : ex)
        {
            mix8(static_cast<uint64_t>(static_cast<int64_t>(m.id)));
            mix8(m.bcls.size());
            for (char ch : m.bcls) { hv ^= static_cast<uint8_t>(ch); hv *= 1099511628211ULL; }
            mix8(static_cast<uint64_t>(static_cast<int64_t>(m.x)));
            mix8(static_cast<uint64_t>(static_cast<int64_t>(m.y)));
            mix8(static_cast<uint64_t>(static_cast<int64_t>(m.h)));
            mix8(static_cast<uint64_t>(static_cast<int64_t>(m.rot)));
        }
        return hv;
    }

    // WHAT: read + parse + plan of a blueprint (shared by bp_paste and bp_import; pure move of the bp_paste load checks). Reads the file (or clip),
    //       parses, live scan, block check, resolve, recipes, config encode, new-machine check, identity keep (sync = replace), config changes.
    //       Fills p (place = new machines before the cost filter). "" or the error text (caller: pp_fail / pp_imp_fail).
    std::string pp_plan_load(const std::string& src, long long station, bool replace, PPlan& p)
    {
        std::string text;
        if (src == "clip")
        {
            if (!pp_clip(text)) return "clipboard has no text";
        }
        else
        {
            std::ifstream f(std::filesystem::path(widen(src)), std::ios::binary);
            if (!f) return "cannot open " + src;
            std::ostringstream ss;
            ss << f.rdbuf();
            text = ss.str();
            if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
        }
        g_pp_ugskip.clear();
        PasteDoc doc;
        std::string err;
        if (!pp_parse_doc(text, doc, err)) return err;
        // an empty blueprint is allowed on purpose: sync then cleans every player-built machine off the station (user wants that)
        PasteCtx c;
        c.station = station;
        if (!seh_run(pp_scan_fn, &c)) { bp_out(L"FAULT bp_paste " + seh_describe(g_seh)); return "live read fault (see BPProbe.txt)"; }
        if (!c.err.empty()) return c.err;
        std::vector<std::string> missing;
        for (const PBlock& jb : doc.blocks)
        {
            bool f = false;
            for (const PBlock& lb : c.blocks) if (lb.cls == jb.cls && lb.x == jb.x && lb.y == jb.y && lb.h == jb.h) { f = true; break; }
            if (!f) missing.push_back(jb.cls + "@(" + std::to_string(jb.x) + "," + std::to_string(jb.y) + ",h" + std::to_string(jb.h) + ")");
        }
        if (!missing.empty())
        {
            std::string m = "Blocks are not on the grid:";
            for (size_t i = 0; i < missing.size() && i < 5; ++i) m += " " + missing[i] + ";";
            return m;
        }
        const std::string rerr = pp_resolve(doc.machines, c.bld);
        if (!rerr.empty()) return rerr;
        const std::string recerr = pp_resolve_recipes(doc.machines);
        if (!recerr.empty()) return recerr;
        std::vector<PMach> cells;
        const std::string uerr = pp_encode_cfg(doc.machines, cells);
        if (!uerr.empty()) return uerr;
        std::vector<PMach> all = doc.machines;   // placed + exit cells (checked, not placed)
        all.insert(all.end(), cells.begin(), cells.end());
        const std::string perr = pp_check_new(c.blocks, all);
        if (!perr.empty()) return perr;
        // identity: an existing identical machine is kept (not placed, not paid, not removed); its config may still change
        std::vector<PMach> kept;
        pp_split_kept(doc.machines, c.existing, kept, replace);
        for (const PMach& k : kept) bp_out(widen("PASTE KEEP " + k.bcls + " @(" + std::to_string(k.x) + "," + std::to_string(k.y) + "," + std::to_string(k.h) + ") id=" + std::to_string(k.id)));
        std::vector<PCfgChg> chg;
        int recChanged = 0;
        pp_kept_changes(kept, chg, recChanged);
        p.station = c.station; p.name = doc.name; p.place = doc.machines; p.cells = cells; p.kept = kept; p.chg = chg; p.recChanged = recChanged;
        p.portChanged = 0;
        for (const PCfgChg& ch : chg) p.portChanged += static_cast<int>(ch.ports.size());
        p.bld = c.bld; p.existing = c.existing; p.skippedUg = g_pp_ugskip;
        return "";
    }

    // WHAT: removals of a plan (pure move of the bp_paste conflict/stale checks, nothing sent). rmv = indices into p.existing (conflicts, then stale),
    //       stale = the stale ones (replace only). "" or the refusal text (verbatim, the caller fails with it).
    std::string pp_plan_removals(PPlan& p, bool replace, std::vector<size_t>& rmv, std::vector<size_t>& stale)
    {
        std::vector<PMach> all = p.place;   // placed + exit cells
        all.insert(all.end(), p.cells.begin(), p.cells.end());
        const std::vector<size_t> hit = pp_conflicts(all, p.existing);
        // sync (replace): existing machines the blueprint no longer has are removed too (stale); they join the removal list
        stale = replace ? pp_stale(p.existing, p.kept, p.bld) : std::vector<size_t>();
        rmv = hit;
        for (size_t s : stale) if (std::find(rmv.begin(), rmv.end(), s) == rmv.end()) rmv.push_back(s);
        if (!rmv.empty())
        {
            std::string list;
            for (size_t i = 0; i < hit.size() && i < 10; ++i)
            {
                const PMach& e = p.existing[hit[i]];
                list += (i ? ", " : "") + e.bcls + "#" + std::to_string(e.id) + "@(" + std::to_string(e.x) + "," + std::to_string(e.y) + ")";
            }
            if (!replace) return std::to_string(hit.size()) + " machine conflicts with existing machines (use replace): " + list;
            for (size_t k : hit)
                if (p.existing[k].bcls.find("Dock") != std::string::npos || p.existing[k].bcls.find("SolarPanel") != std::string::npos || p.existing[k].bcls.find("Console") != std::string::npos)
                    return "replace refused, would remove " + p.existing[k].bcls + "#" + std::to_string(p.existing[k].id) + " (Dock/SolarPanel/Console are never removed): " + list;
            for (size_t k : hit)   // sync: only stale machines (player-buildable, not kept) may be removed; any other overlap refuses
                if (std::find(stale.begin(), stale.end(), k) == stale.end())
                    return "sync refused, would remove " + p.existing[k].bcls + "#" + std::to_string(p.existing[k].id) + " (no player Buildable, never removed): " + list;
        }
        return "";
    }

    std::wstring bp_paste_cmd(const std::string& cmd)
    {
        std::vector<std::string> tk;
        { std::istringstream is(cmd.size() > 9 ? cmd.substr(9) : std::string()); std::string t; while (is >> t) tk.push_back(t); }
        bool replace = false;
        long long station = 0;
        while (!tk.empty())
        {
            if (tk.back() == "replace") { replace = true; tk.pop_back(); continue; }
            const std::string& id = tk.back();
            if (tk.size() >= 2 && tk[tk.size() - 2] == "station" && !id.empty() && std::all_of(id.begin(), id.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
            { station = std::strtoll(id.c_str(), nullptr, 10); tk.resize(tk.size() - 2); continue; }
            break;
        }
        std::string src;
        for (size_t i = 0; i < tk.size(); ++i) src += (i ? " " : "") + tk[i];
        if (src.size() >= 2 && src.front() == '"' && src.back() == '"') src = src.substr(1, src.size() - 2);
        if (src.empty()) return STR("error: usage bp_paste <path|clip> [station <id>] [replace]");
        if (g_bpaste.active) return STR("error: previous bp_paste still running (see PasteStatus.txt)");
        if (g_bpp.pending) return STR("error: previous bp_place/bp_send still being checked, wait 5 s");
        bp_file_init();
        bp_out(L"=== bp_paste " + widen(src) + (station ? L" station " + std::to_wstring(station) : L"") + (replace ? L" replace" : L""));
        PPlan P;
        const std::string lerr = pp_plan_load(src, station, replace, P);
        if (!lerr.empty()) return pp_fail(lerr);
        // cost check: unaffordable machines are dropped here, so replace never removes machines for them
        std::vector<PMach> skipped;
        std::string short_txt;
        const std::string cerr = pp_cost_filter(P.place, skipped, short_txt);
        if (!cerr.empty()) return pp_fail(cerr);
        std::vector<size_t> rmv, stale;
        const std::string rmerr = pp_plan_removals(P, replace, rmv, stale);
        if (!rmerr.empty()) return pp_fail(rmerr);
        const std::string nm = std::to_string(P.place.size());
        if (!rmv.empty())
        {
            // refund of the removals goes to the player inventory: refuse before any DisassembleInput when it does not fit
            std::vector<std::pair<int, std::string>> rm;
            for (size_t k : rmv) rm.push_back({ P.existing[k].id, P.existing[k].bcls });
            std::vector<RefundItem> need;
            std::string rfc = refund_for(rm, need);
            if (rfc.empty()) rfc = refund_check(need, "bp_paste replace");
            if (!rfc.empty()) return pp_fail(rfc);
        }
        pp_send_changes(P.chg);   // every refusal is above: only now do config changes go out
        g_bpaste = BpPasteRun{};
        g_bpaste.active = true;
        g_bpaste.station = P.station;
        g_bpaste.ms = P.place;
        g_bpaste.cells = P.cells;
        g_bpaste.skipped = skipped;
        g_bpaste.short_txt = short_txt;
        g_bpaste.kept = P.kept;
        g_bpaste.chg = P.chg;
        g_bpaste.recChanged = P.recChanged;
        g_bpaste.sync = replace;
        for (size_t k : stale) g_bpaste.stale_ids.push_back(P.existing[k].id);
        if (rmv.empty())
        {
            if (!pp_do_place()) { bp_flush(); return STR("error: snapshot failed, nothing placed"); }
            bp_flush();
            return widen(std::string(g_bpaste.sync ? "ok sync: placed " : "ok placed ") + std::to_string(g_bpaste.placed));
        }
        // replace: disassemble the conflicts and stale machines now, place after they are gone (checked every 1 s, up to 60 tries)
        int removed = 0;
        for (size_t k : rmv)
        {
            const bool isStale = std::find(stale.begin(), stale.end(), k) != stale.end();
            const std::string at = "(" + std::to_string(P.existing[k].x) + "," + std::to_string(P.existing[k].y) + "," + std::to_string(P.existing[k].h) + ")";
            if (isStale) bp_out(widen("PASTE STALE " + P.existing[k].bcls + " @" + at + " id=" + std::to_string(P.existing[k].id)));
            BpSend s;
            s.tok = { "bp_send", "DisassembleInput", "0", "targetid=" + std::to_string(P.existing[k].id), "bmassdisassemble=0" };
            g_bpp.pending = false;
            if (!seh_run(bp_send_fn, &s)) bp_out(L"FAULT bp_paste " + seh_describe(g_seh));
            if (s.called) { ++removed; if (isStale) ++g_bpaste.staleN; } else bp_out(s.out);
        }
        g_bpaste.stage = 1;
        g_bpaste.t_next = GetTickCount64() + 1500;
        pp_status("PASTE removing " + std::to_string(removed) + " of " + std::to_string(rmv.size()) + ", placing " + nm + " after removal");
        bp_flush();
        return widen("ok queued " + nm + " (removing " + std::to_string(removed) + ", see PasteStatus.txt)");
    }

    // WHAT: one step of a running paste (from bp_tick): stage 1 waits for the removals then places; stage 2 verifies at +3 s.
    void bp_paste_tick()
    {
        if (!g_bpaste.active) return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_bpaste.t_next) return;
        if (g_bpaste.stage == 1)
        {
            PasteCtx c;
            c.station = g_bpaste.station;
            if (!seh_run(pp_scan_fn, &c)) { pp_status("error: live read fault after removal, nothing placed"); g_bpaste.active = false; return; }
            std::vector<PMach> all = g_bpaste.ms;
            all.insert(all.end(), g_bpaste.cells.begin(), g_bpaste.cells.end());
            const std::vector<size_t> hit = pp_conflicts(all, c.existing);
            std::set<int> left;   // ids still there: overlapping the paste, or stale (sync) and not yet gone
            for (size_t k : hit) left.insert(c.existing[k].id);
            for (const PMach& e : c.existing)
                if (std::find(g_bpaste.stale_ids.begin(), g_bpaste.stale_ids.end(), e.id) != g_bpaste.stale_ids.end()) left.insert(e.id);
            if (!left.empty())
            {
                // WHY: removing hundreds of machines takes the sim many ticks (218 removals > 5 s); wait up to 60 s, status shows progress
                if (++g_bpaste.tries < 60) { pp_status("removing, " + std::to_string(left.size()) + " left..."); g_bpaste.t_next = now + 1000; return; }
                pp_status("error: " + std::to_string(left.size()) + " conflicts still present after removal, nothing placed");
                g_bpaste.active = false;
                return;
            }
            pp_do_place();
        }
        else if (g_bpaste.stage == 2)
        {
            pp_verify();
            g_bpaste.active = false;
        }
    }

    // ===== bp_import (M4 dry-run): plan a blueprint against the live station and write its cost + status files. Places, removes and sends nothing. =====
    // WHAT: writes one BlueprintImport.* file via write_atomic (UTF-8, \n). A failed write is logged, never thrown.
    void pp_imp_write(const char* name, const std::string& text)
    {
        try { write_atomic(get_dll_dir() / name, text); }
        catch (...) { bp_out(widen(std::string("IMPORT cannot write ") + name)); }
    }
    struct PImpCost { PCostMap need, refund, net; };
    // WHAT: pure. need = ItemCost of the machines to place, refund = ItemCost of the machines to remove, net = need - refund per item (may be negative).
    //       Kept machines are not passed in (they cost nothing).
    PImpCost pp_import_cost(const std::vector<PCostMap>& place, const std::vector<PCostMap>& removed)
    {
        PImpCost r;
        for (const PCostMap& m : place) for (const auto& kv : m) r.need[kv.first] += kv.second;
        for (const PCostMap& m : removed) for (const auto& kv : m) r.refund[kv.first] += kv.second;
        for (const auto& kv : r.need) r.net[kv.first] += kv.second;
        for (const auto& kv : r.refund) r.net[kv.first] -= kv.second;
        return r;
    }
    // WHAT: BlueprintImport.cost text: "ITEM_Name|need|refund|net" per item with need > 0 or refund > 0, sorted by name. "" when nothing to pay.
    std::string pp_import_cost_text(const PImpCost& c)
    {
        std::set<std::string> names;
        for (const auto& kv : c.need) names.insert(kv.first);
        for (const auto& kv : c.refund) names.insert(kv.first);
        std::string s;
        for (const std::string& n : names)
        {
            const auto ni = c.need.find(n);
            const auto ri = c.refund.find(n);
            const int64_t nd = ni != c.need.end() ? ni->second : 0;
            const int64_t rf = ri != c.refund.end() ? ri->second : 0;
            if (nd <= 0 && rf <= 0) continue;
            s += n + "|" + std::to_string(nd) + "|" + std::to_string(rf) + "|" + std::to_string(nd - rf) + "\n";
        }
        return s;
    }
    // WHAT: refund part of the status: "Name x<refund>" for up to 5 items with refund > 0 (ITEM_ prefix stripped), then ", +M more". "" when none.
    std::string pp_import_refund_txt(const PCostMap& refund)
    {
        std::string s;
        int shown = 0, more = 0;
        for (const auto& kv : refund)
        {
            if (kv.second <= 0) continue;
            if (shown == 5) { ++more; continue; }
            const std::string nm = kv.first.rfind("ITEM_", 0) == 0 ? kv.first.substr(5) : kv.first;
            s += (shown ? ", " : "") + nm + " x" + std::to_string(kv.second);
            ++shown;
        }
        if (more) s += ", +" + std::to_string(more) + " more";
        return s;
    }
    // WHAT: status line 1 text: "place P, keep K, remove R[ (refund <items>)], recipe changes C". The refund part only when R > 0.
    std::string pp_import_text(int place, int keep, int remove, const PCostMap& refund, int recipes)
    {
        std::string s = "place " + std::to_string(place) + ", keep " + std::to_string(keep) + ", remove " + std::to_string(remove);
        if (remove > 0)
        {
            const std::string r = pp_import_refund_txt(refund);
            s += " (refund " + (r.empty() ? std::string("none") : r) + ")";
        }
        return s + ", recipe changes " + std::to_string(recipes);
    }
    // WHAT: removes= value: "Class:count" per class (short name), most numerous first (ties by name). "" when none.
    std::string pp_import_removes(const std::vector<PMach>& removed)
    {
        std::map<std::string, int> n;
        for (const PMach& r : removed) ++n[r.bcls];
        std::vector<std::pair<std::string, int>> v(n.begin(), n.end());
        std::stable_sort(v.begin(), v.end(), [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) { return a.second > b.second; });
        std::string s;
        for (const auto& kv : v) s += (s.empty() ? "" : ",") + kv.first + ":" + std::to_string(kv.second);
        return s;
    }
    // WHAT: bp_import failure: cost file emptied, status "error|msg" + seq, plan invalid. Returns the command result.
    std::wstring pp_imp_fail(int seq, const std::string& msg)
    {
        g_bpplan.valid = false;
        pp_imp_write("BlueprintImport.cost", "");
        pp_imp_write("BlueprintImport.status", "error|" + msg + "\nseq=" + std::to_string(seq) + "\n");
        bp_out(widen("IMPORT error: " + msg));
        bp_flush();
        return widen("error: " + msg);
    }
    // WHAT: "bp_import <path> [station <id>]". Plans the blueprint like bp_paste replace (same load, checks, removals), prices it (cost = new
    //       machines, refund = removals, raw item names), keeps the plan in g_bpplan. Writes BlueprintImport.cost, then BlueprintImport.status.
    //       Places, removes and sends nothing; no cost filter, no refund check.
    std::wstring bp_import_cmd(const std::string& cmd)
    {
        const int seq = g_cmd.seq;
        std::vector<std::string> tk;
        { std::istringstream is(cmd.substr(10)); std::string t; while (is >> t) tk.push_back(t); }
        long long station = 0;
        while (tk.size() >= 2 && tk[tk.size() - 2] == "station" && !tk.back().empty() && std::all_of(tk.back().begin(), tk.back().end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
        { station = std::strtoll(tk.back().c_str(), nullptr, 10); tk.resize(tk.size() - 2); }
        std::string src;
        for (size_t i = 0; i < tk.size(); ++i) src += (i ? " " : "") + tk[i];
        if (src.size() >= 2 && src.front() == '"' && src.back() == '"') src = src.substr(1, src.size() - 2);
        if (src.empty()) return pp_imp_fail(seq, "usage bp_import <path> [station <id>]");
        if (g_bpaste.active || g_bpp.pending) return pp_imp_fail(seq, "busy: a build is running");
        bp_file_init();
        bp_out(L"=== bp_import " + widen(src) + (station ? L" station " + std::to_wstring(station) : L""));
        pp_imp_write("BlueprintImport.status", "working|reading blueprint\nseq=" + std::to_string(seq) + "\n");
        // WHY: the read and parse are pp_plan_load's (shared with bp_paste); here the file checks only. clip is not a file.
        std::error_code ec;
        const std::filesystem::path fp(widen(src));
        if (src == "clip" || !std::filesystem::is_regular_file(fp, ec)) return pp_imp_fail(seq, "cannot open " + src);
        const std::uintmax_t size = std::filesystem::file_size(fp, ec);
        if (ec) return pp_imp_fail(seq, "cannot open " + src);
        if (size > 64u * 1024u * 1024u) return pp_imp_fail(seq, "file too large");
        PPlan P;
        const std::string lerr = pp_plan_load(src, station, true, P);
        if (!lerr.empty()) return pp_imp_fail(seq, lerr);
        std::vector<size_t> rmv, stale;
        const std::string rerr = pp_plan_removals(P, true, rmv, stale);
        if (!rerr.empty()) return pp_imp_fail(seq, rerr);
        for (size_t k : rmv) P.removed.push_back(P.existing[k]);
        for (size_t k : stale) P.staleIds.push_back(P.existing[k].id);
        // cost: ItemCost of the machines to place (by their Buildable) and of the removals (by their class's Buildable)
        PCostCtx cc;
        cc.raw = true;
        for (const PMach& m : P.place) cc.want.insert(m.bdat);
        std::vector<std::string> rdat;
        for (const PMach& r : P.removed)
        {
            const std::string d = pp_dat_for_cls(r.bcls, P.bld);
            if (d.empty()) return pp_imp_fail(seq, "refund cost unknown for " + r.bcls);
            rdat.push_back(d);
            cc.want.insert(d);
        }
        if (!seh_run(pp_cost_fn, &cc)) { bp_out(L"FAULT bp_import cost read " + seh_describe(g_seh)); return pp_imp_fail(seq, "cost read fault"); }
        std::vector<PCostMap> pc, rc;
        for (const PMach& m : P.place)
        {
            if (!cc.found.count(m.bdat)) bp_out(widen("PASTE COST no ItemCost read for " + m.bdat + " (counted as free)"));
            pc.push_back(cc.cost[m.bdat]);
        }
        for (const std::string& d : rdat)
        {
            if (!cc.found.count(d)) bp_out(widen("PASTE COST no ItemCost read for " + d + " (refund counted as 0)"));
            rc.push_back(cc.cost[d]);
        }
        const PImpCost ic = pp_import_cost(pc, rc);
        PCrCtx cr;
        if (!seh_run(pp_creative_fn, &cr)) bp_out(L"FAULT bp_import creative flag " + seh_describe(g_seh));
        bp_out(widen("PASTE COST creative: " + cr.src));
        const std::string creative = cr.state == 1 ? "1" : cr.state == 0 ? "0" : "unknown";
        P.need = ic.need;
        P.refund = ic.refund;
        P.fp = pp_fingerprint(P.existing);
        P.valid = true;
        g_bpplan = P;
        const std::string txt = pp_import_text(static_cast<int>(P.place.size()), static_cast<int>(P.kept.size()), static_cast<int>(P.removed.size()), ic.refund, P.recChanged);
        std::string st = "done|" + txt + "\n";
        st += "seq=" + std::to_string(seq) + "\nblueprint=" + P.name + "\nstation=" + std::to_string(P.station) + "\nplace=" + std::to_string(P.place.size()) +
              "\nkeep=" + std::to_string(P.kept.size()) + "\nremove=" + std::to_string(P.removed.size()) + "\nrecipe=" + std::to_string(P.recChanged) +
              "\nport=" + std::to_string(P.portChanged) + "\nskipped=" + P.skippedUg + "\ncreative=" + creative + "\n";
        if (!P.removed.empty()) st += "removes=" + pp_import_removes(P.removed) + "\n";
        pp_imp_write("BlueprintImport.cost", pp_import_cost_text(ic));
        pp_imp_write("BlueprintImport.status", st);
        bp_out(widen("IMPORT ok " + txt + " (blueprint " + P.name + ", station " + std::to_string(P.station) + ")"));
        bp_flush();
        return widen("ok import " + txt);
    }
    // WHAT: bp_paste_selftest: parser, footprint, floor and overlap checks on embedded data. Nothing is placed. PASS/FAIL to BPProbe.txt.
    std::wstring bp_paste_selftest_cmd()
    {
        bp_file_init();
        int pass = 0, total = 0;
        auto pchk = [&](bool ok, const char* what) { ++total; if (ok) ++pass; bp_out(widen(std::string(ok ? "PASS " : "FAIL ") + what)); };
        const char* json = R"({"Version":2,"DisplayName":"AB\"","Blocks":[{"Class":"/Game/Blueprints/BuildingBlocks/BPO_Station_01.BPO_Station_01_C","GridLocation":{"X":0,"Y":-2},"Height":0,"GridRotation":1,"BlockID":5,"Extra":[1,{"a":null,"b":true}]}],"DefaultMachines":[{"Class":"BP_ConveyorBeltObject","DefaultName":"DAT_ConveyorBelt","Transform":{"GridLocation":{"X":-5,"Y":3},"Height":8,"GridRotation":3},"MachineID":1}]})";
        PasteDoc d;
        std::string err;
        const bool okp = pp_parse_doc(json, d, err);
        pchk(okp && d.name == "AB\"", "parse: DisplayName with escapes");
        pchk(okp && d.blocks.size() == 1 && d.blocks[0].cls == "BPO_Station_01" && d.blocks[0].x == 0 && d.blocks[0].y == -2 && d.blocks[0].rot == 1, "parse: block class/location/rotation");
        pchk(okp && d.machines.size() == 1 && d.machines[0].dat == "DAT_ConveyorBelt" && d.machines[0].x == -5 && d.machines[0].h == 8 && d.machines[0].rot == 3, "parse: machine DAT/Transform");
        PasteDoc bad;
        pchk(!pp_parse_doc("{\"Blocks\":[1,]}", bad, err), "parse: trailing comma refused");
        pchk(!pp_parse_doc("{\"Blocks\":[],\"DefaultMachines\":[{\"Class\":\"X\",\"Transform\":{\"GridLocation\":{\"X\":0,\"Y\":0},\"Height\":4,\"GridRotation\":0}}]}", bad, err), "parse: machine Height 4 refused");
        int fw = 0, fh = 0;
        pp_fp(2, 1, 1, fw, fh);
        pchk(fw == 1 && fh == 2, "footprint: odd rotation swaps w/h");
        PRect f;
        const PBlock s1{ "BPO_Station_01", 0, 0, 0, 0 };
        pchk(pp_floor(s1, f) && f.x0 == -14 && f.x1 == 14 && f.y0 == -14 && f.y1 == 14, "floor: 1x1 block at 0 = [-14,14]");
        const PBlock s2{ "BPO_Station_2x1", 1, 0, 0, 1 };
        pchk(pp_floor(s2, f) && f.x0 == -22 && f.x1 == 38 && f.y0 == -14 && f.y1 == 14, "floor: 2x1 block rot 1 swaps (centre 8)");
        const PBlock dk{ "BPO_Dock", 0, 0, 0, 0 };
        pchk(!pp_floor(dk, f), "floor: nofloor block has no floor");
        auto mk = [](int x, int y, int h) { PMach m; m.bdat = "DAT_ConveyorBelt"; m.x = x; m.y = y; m.h = h; m.w = 1; m.hh = 1; return m; };
        const std::vector<PBlock> live{ s1 };
        pchk(pp_check_new(live, { mk(1, 1, 0) }).empty(), "check: belt at (1,1) on floor is OK");
        pchk(!pp_check_new(live, { mk(2, 1, 0) }).empty(), "check: even X refused (parity)");
        pchk(!pp_check_new(live, { mk(15, 1, 0) }).empty(), "check: footprint past floor edge refused");
        pchk(!pp_check_new(live, { mk(1, 1, 8) }).empty(), "check: other layer refused");
        pchk(!pp_check_new(live, { mk(1, 1, 0), mk(1, 1, 0) }).empty(), "check: overlapping new machines refused");
        pchk(pp_check_new(live, { mk(1, 1, 0), mk(3, 1, 0) }).empty(), "check: touching new machines OK");
        PMach ex = mk(1, 1, 0); ex.id = 9;
        pchk(pp_conflicts({ mk(1, 1, 0) }, { ex }).size() == 1, "conflict: overlapping existing found");
        pchk(pp_conflicts({ mk(3, 1, 0) }, { ex }).empty(), "conflict: touching existing not a conflict");
        // config encoders (vault rea-machine-config.md s2/s3): refinery example 0100 0f000000 "titaniumplates"+NUL 04000000 01010101
        {
            std::vector<uint8_t> want = { 1, 0, 15, 0, 0, 0 };
            for (char ch : std::string("titaniumplates")) want.push_back(static_cast<uint8_t>(ch));
            want.push_back(0);
            const uint8_t tail[] = { 4, 0, 0, 0, 1, 1, 1, 1 };
            want.insert(want.end(), tail, tail + 8);
            pchk(pp_cfg_recipe("titaniumplates", std::vector<uint8_t>(4, 1)) == want, "cfg: refinery titaniumplates 4 ports on = sample bytes");
            std::vector<uint8_t> off = want;
            off[off.size() - 2] = 0;   // third port off
            pchk(pp_cfg_recipe("titaniumplates", { 1, 1, 0, 1 }) == off, "cfg: disabled port = 00");
            pchk(pp_cfg_underground(4) == std::vector<uint8_t>({ 1, 0, 4, 0, 0, 0 }), "cfg: underground Length 4 = 01 00 04000000");
            std::vector<uint8_t> hx;
            pchk(bp_hex_bytes(L"0100ff", hx) && hx == std::vector<uint8_t>({ 1, 0, 255 }) && !bp_hex_bytes(L"010", hx) && !bp_hex_bytes(L"0g", hx), "hex: parse, odd length and bad digit refused");
        }
        {
            PMach a; a.dat = "DAT_Assembler"; a.bcls = "BP_AssemblerObject"; a.x = 1; a.y = 1;   // no Recipe: no config
            std::vector<PMach> av{ a }, nc;
            pp_encode_cfg(av, nc);
            pchk(av[0].cfg.empty(), "encode: recipe machine without Recipe has no config");
        }
        // undergrounds (proven in game): pair = ONE machine at the entrance, Length = cells inclusive, exit cell kept out of ms
        {
            auto ug = [](int x, int y, int rot, const char* dat) { PMach m; m.cls = m.bcls = "BP_UndergroundBeltObject"; m.dat = dat; m.bdat = dat; m.x = x; m.y = y; m.rot = rot; m.w = m.hh = 1; return m; };
            std::vector<PMach> ms{ ug(-7, 1, 0, "DAT_UndergroundBelt_2"), ug(-3, 1, 0, "DAT_UndergroundBelt_2") }, cells;
            pchk(pp_encode_cfg(ms, cells).empty() && ms.size() == 1 && cells.size() == 1, "underground: pair = one machine + one exit cell");
            pchk(ms.size() == 1 && ms[0].x == -7 && pp_hex(ms[0].cfg) == "010003000000", "underground: entrance Length 3 cfg 010003000000");
            pchk(cells.size() == 1 && cells[0].x == -3, "underground: exit cell at (-3,y)");
            std::vector<PMach> ms2{ ug(-3, 1, 2, "DAT_UndergroundBelt_2"), ug(-7, 1, 2, "DAT_UndergroundBelt_2") }, cells2;
            pchk(pp_encode_cfg(ms2, cells2).empty() && ms2.size() == 1 && ms2[0].x == -3 && cells2.size() == 1 && cells2[0].x == -7, "underground: rot2 entrance is the east one");
            std::vector<PMach> lg{ ug(-7, 1, 0, "DAT_UndergroundBelt_2"), ug(13, 1, 0, "DAT_UndergroundBelt_2") }, c3;   // 10 cells apart = Length 11 > 10
            pchk(pp_encode_cfg(lg, c3).find("too long") != std::string::npos, "underground: Length 11 on Mk2 refused (too long)");
            std::vector<PMach> up{ ug(-7, 1, 0, "DAT_UndergroundBelt_2") }, c4;
            pchk(pp_encode_cfg(up, c4).find("without partner") != std::string::npos, "underground: unpaired refused");
        }
        // recipe site ids (offline part of the resolution): "inputs>output" -> REC id, no guessing
        {
            std::string rr, why;
            pchk(pp_norm("item_Silicon Wafers") == "siliconwafers" && pp_item_set(" item_Silica + item_Silicon Wafers ") == "silica+siliconwafers", "site: names normalized, input set sorted");
            pchk(pp_site_rec("item_titaniumore>item_titaniumplates", "Refinery", rr, why) && rr == "REC_TitaniumPlates", "site: titaniumore>titaniumplates (Refinery) = REC_TitaniumPlates");
            pchk(pp_site_rec("item_enrichedtitaniumore>item_titaniumplates", "Refinery", rr, why) && rr == "REC_EnrichedTitaniumPlates", "site: enriched input picks REC_EnrichedTitaniumPlates");
            pchk(pp_site_rec("item_anything>item_hydrogenfuel", "Refinery", rr, why) && rr == "REC_HydrogenFuel", "site: single candidate for crafter+output is used");
            pchk(!pp_site_rec("item_enrichedcobalt>item_cobaltplates", "Refinery", rr, why), "site: input set matches no candidate = refused");
            pchk(!pp_site_rec("item_titaniumore>item_titaniumplates", "Assembler", rr, why), "site: wrong crafter = refused");
            pchk(!pp_site_rec("titaniumplates", "Refinery", rr, why), "site: no '>' = not a site id");
            PMach sw; sw.dat = "DAT_Refinery"; sw.bcls = "BP_RefineryObject"; sw.recipe = "SiliconWafers";
            std::vector<PMach> swv{ sw }, nc;
            pp_encode_cfg(swv, nc);
            pchk(swv[0].cfg == pp_cfg_recipe("SiliconWafers", std::vector<uint8_t>(4, 1)), "encode: game Name kept verbatim (case)");
        }
        // ConfigHex: hex text rules, JSON field, verbatim encode, stand-alone and paired undergrounds
        {
            std::vector<uint8_t> hx;
            std::string why;
            pchk(pp_hex_cfg("0100ff", hx, why) && hx == std::vector<uint8_t>({ 1, 0, 255 }), "confighex: valid hex = bytes");
            pchk(!pp_hex_cfg("010", hx, why), "confighex: odd length refused");
            pchk(!pp_hex_cfg("01zz", hx, why), "confighex: non-hex refused");
            pchk(pp_hex_cfg(std::string(8192, '0'), hx, why) && hx.size() == 4096, "confighex: 4096 bytes accepted");
            pchk(!pp_hex_cfg(std::string(8194, '0'), hx, why), "confighex: 4097 bytes refused");
            PasteDoc cd;
            pchk(pp_parse_doc("{\"Blocks\":[],\"DefaultMachines\":[{\"Class\":\"BP_RefineryObject\",\"DefaultName\":\"DAT_Refinery\",\"ConfigHex\":\"0100ff\",\"Transform\":{\"GridLocation\":{\"X\":1,\"Y\":1},\"Height\":0,\"GridRotation\":0}}]}", cd, err)
                  && cd.machines.size() == 1 && cd.machines[0].hasHex && cd.machines[0].hexcfg == std::vector<uint8_t>({ 1, 0, 255 }), "confighex: JSON field parsed");
            PasteDoc cd2;
            pchk(!pp_parse_doc("{\"Blocks\":[],\"DefaultMachines\":[{\"Class\":\"X\",\"ConfigHex\":\"010\",\"Transform\":{\"GridLocation\":{\"X\":1,\"Y\":1},\"Height\":0,\"GridRotation\":0}}]}", cd2, err)
                  && err.find("ConfigHex refused for X") != std::string::npos, "confighex: JSON odd length refused, machine named");
            PMach rf; rf.dat = "DAT_Refinery"; rf.bcls = "BP_RefineryObject"; rf.hasHex = true; rf.hexcfg = { 1, 0, 255 }; rf.recipe = "titaniumplates";
            std::vector<PMach> rv{ rf }, nc;
            pchk(pp_encode_cfg(rv, nc).empty() && rv[0].cfg == std::vector<uint8_t>({ 1, 0, 255 }), "confighex: verbatim, recipe encoder skipped");
            PMach sa; sa.cls = sa.bcls = "BP_UndergroundBeltObject_2"; sa.dat = sa.bdat = "DAT_UndergroundBelt_2"; sa.w = sa.hh = 1; sa.x = -7; sa.y = 1;
            sa.hasHex = true; sa.hexcfg = { 1, 0, 3, 0, 0, 0 };
            std::vector<PMach> sv{ sa }, sc;
            pchk(pp_encode_cfg(sv, sc).empty() && sv.size() == 1 && sv[0].cfg == sa.hexcfg && sc.empty(), "confighex: lone underground Length 3 = stand-alone tunnel");
            PMach sb = sa; sb.hexcfg = { 1, 0, 11, 0, 0, 0 };
            std::vector<PMach> sw{ sb }, sc2;
            pchk(pp_encode_cfg(sw, sc2).find("without partner") != std::string::npos, "confighex: lone underground Length 11 on Mk2 refused");
            PMach p1 = sa; p1.hexcfg = { 1, 0, 9, 0, 0, 0 };
            PMach p2 = sa; p2.x = -3; p2.hasHex = false;
            std::vector<PMach> pv{ p1, p2 }, pc;
            pchk(pp_encode_cfg(pv, pc).empty() && pv.size() == 1 && pv[0].cfg == std::vector<uint8_t>({ 1, 0, 3, 0, 0, 0 }), "confighex: paired underground ignores ConfigHex (Length from pair)");
        }
        // sync stale selection (pure): identical kept; rot / Length differs or absent = stale + new placed; dock / solar / no-Buildable never stale
        {
            auto mkc = [](const char* cls, int x, int y, int h, int rot) { PMach m; m.bcls = cls; m.x = x; m.y = y; m.h = h; m.rot = rot; m.w = m.hh = 1; return m; };
            const PBuildables bl{ { "DAT_Refinery", "BP_RefineryObject" }, { "DAT_Dock", "BP_DockObject" }, { "DAT_SolarPanel", "BP_SolarPanelObject" },
                                  { "DAT_UndergroundBelt_2", "BP_UndergroundBeltObject_2" } };
            PMach ex = mkc("BP_RefineryObject", 3, 3, 0, 0);
            ex.id = 77;
            std::vector<PMach> ms{ mkc("BP_RefineryObject", 3, 3, 0, 0) }, kp;
            pp_split_kept(ms, { ex }, kp, true);
            pchk(pp_stale({ ex }, kp, bl).empty() && ms.empty(), "sync: identical machine kept, not stale, nothing placed");
            std::vector<PMach> rt{ mkc("BP_RefineryObject", 3, 3, 0, 1) }, kr;
            pp_split_kept(rt, { ex }, kr, true);
            pchk(kr.empty() && rt.size() == 1 && pp_stale({ ex }, kr, bl) == std::vector<size_t>({ 0 }), "sync: different rot = stale, new one placed");
            pchk(pp_stale({ ex }, {}, bl) == std::vector<size_t>({ 0 }), "sync: machine absent from blueprint = stale");
            PMach dk = mkc("BP_DockObject", 9, 9, 0, 0);
            dk.id = 5;
            PMach sp = mkc("BP_SolarPanelObject", 11, 9, 0, 0);
            sp.id = 6;
            PMach nb = mkc("BP_BlockOnlyObject", 13, 9, 0, 0);   // no Buildable: never touched
            nb.id = 7;
            pchk(pp_stale({ dk, sp, nb }, {}, bl).empty(), "sync: dock, solar panel and no-Buildable machine never stale");
            PMach ug = mkc("BP_UndergroundBeltObject_2", -7, 1, 0, 0);
            ug.id = 8;
            ug.hasLive = true;
            ug.live = pp_cfg_underground(3);
            PMach pu = mkc("BP_UndergroundBeltObject_2", -7, 1, 0, 0);
            pu.dat = "DAT_UndergroundBelt_2";
            pu.cfg = pp_cfg_underground(5);
            std::vector<PMach> up{ pu }, uk;
            pp_split_kept(up, { ug }, uk, true);
            pchk(uk.empty() && up.size() == 1 && pp_stale({ ug }, uk, bl) == std::vector<size_t>({ 0 }), "sync: underground Length 3 vs 5 = stale, new one placed");
            PMach pu3 = pu;
            pu3.cfg = pp_cfg_underground(3);
            std::vector<PMach> same{ pu3 }, sk;
            pp_split_kept(same, { ug }, sk, true);
            pchk(sk.size() == 1 && same.empty() && pp_stale({ ug }, sk, bl).empty(), "sync: underground same Length = identical, kept");
        }
        // cost greedy (pure): input order, a machine that cannot be paid is skipped, the rest still paid
        {
            PCostMap sh;
            const std::vector<PCostMap> cs{ { { "a", 2 } }, { { "a", 2 } }, { { "b", 1 } } };
            const std::vector<bool> kp = pp_afford(cs, { { "a", 3 }, { "b", 1 } }, sh);
            pchk(kp == std::vector<bool>({ true, false, true }) && sh.size() == 1 && sh["a"] == 1, "afford: 2nd machine short of a by 1 skipped, 3rd still paid");
            PCostMap sh2;
            const std::vector<bool> kp2 = pp_afford({ { { "a", 1 } }, { { "a", 1 } }, { { "a", 1 } } }, { { "a", 2 } }, sh2);
            pchk(kp2 == std::vector<bool>({ true, true, false }) && sh2["a"] == 1, "afford: stock runs out in input order");
        }
        // refund slot math (pure, fake inventory): partial stacks first, empty slots shared by all items
        {
            const uint64_t A = 1, B = 2;
            RefundItem na; na.item = A; na.label = "a"; na.amount = 10; na.max = 5;     // partial 3/5 gives room 2, rest 8 = 2 slots
            RefundItem nb; nb.item = B; nb.label = "b"; nb.amount = 3; nb.max = 1;     // 3 slots of B
            const std::vector<RSlot> sl{ { A, 3 }, { 0, 0 }, { 0, 0 } };                  // 2 empty slots
            pchk(refund_fit(sl, { na }).missing == 0, "refund: 10 of A (max 5) fit: partial 3/5 + 2 empty slots");
            na.amount = 20;   // room 2, rest 18 = 4 slots, 2 empty
            pchk(refund_fit(sl, { na }).missing == 2, "refund: 20 of A (max 5) need 2 more slots");
            na.amount = 10;
            pchk(refund_fit(sl, { na, nb }).missing == 3, "refund: A (2) + B (3) share the 2 empty slots, missing 3");
            pchk(refund_fit({ { A, 5 }, { B, 1 } }, { na }).missing == 2, "refund: full stack and other item give no room, 10 of A need 2 empty slots (none)");
            pchk(refund_fit({ { B, 1 } }, { RefundItem{ A, "a", 5, 5 } }).missing == 1, "refund: other item's stack is not room for A");
            pchk(refund_fit({ { A, 5 } }, { RefundItem{ A, "a", 5, 5 } }).missing == 1, "refund: full stack gives no room, 5 of A need 1 empty slot");
        }
        // identity keep (same class+pos+rot = identical, kept) and the config diff decision (recipe / ports / unknown)
        {
            auto mkc = [](const char* cls, int x, int y, int h, int rot) { PMach m; m.bcls = cls; m.x = x; m.y = y; m.h = h; m.rot = rot; m.w = m.hh = 1; return m; };
            PMach ex = mkc("BP_RefineryObject", 3, 3, 0, 0);
            ex.id = 77; ex.hasLive = true; ex.live = { 1, 0 };
            std::vector<PMach> ms{ mkc("BP_RefineryObject", 3, 3, 0, 0) }, kp;
            pp_split_kept(ms, { ex }, kp);
            pchk(kp.size() == 1 && ms.empty() && kp[0].id == 77 && kp[0].live == std::vector<uint8_t>({ 1, 0 }), "keep: same class+pos+rot is identical (kept, id and live copied)");
            std::vector<PMach> rm{ mkc("BP_RefineryObject", 3, 3, 0, 1) }, kp2;
            pp_split_kept(rm, { ex }, kp2);
            pchk(kp2.empty() && rm.size() == 1 && pp_conflicts(rm, { ex }).size() == 1, "keep: different rot = not kept, conflict");
            std::vector<PMach> cl{ mkc("BP_AssemblerObject", 3, 3, 0, 0) }, kp3;
            pp_split_kept(cl, { ex }, kp3);
            pchk(kp3.empty() && cl.size() == 1 && pp_conflicts(cl, { ex }).size() == 1, "keep: different class = not kept, conflict");
            std::vector<PMach> hg{ mkc("BP_RefineryObject", 3, 3, 8, 0) }, kp4;
            pp_split_kept(hg, { ex }, kp4);
            pchk(kp4.empty() && hg.size() == 1, "keep: different height = not kept");
            std::vector<PMach> two{ mkc("BP_RefineryObject", 3, 3, 0, 0), mkc("BP_RefineryObject", 3, 3, 0, 0) }, kp5;
            pp_split_kept(two, { ex }, kp5);
            pchk(kp5.size() == 1 && two.size() == 1, "keep: one existing matches one pasted machine only");
            const std::vector<uint8_t> tgt = pp_cfg_recipe("titaniumplates", { 1, 1, 1, 1 });
            PCfgDiff d1 = pp_cfg_diff(tgt, pp_cfg_recipe("titaniumplates", { 1, 1, 1, 1 }));
            pchk(d1.ok && !d1.recipe && d1.ports.empty(), "diff: same recipe and ports = no change");
            PCfgDiff d2 = pp_cfg_diff(tgt, pp_cfg_recipe("hullplates", { 1, 1, 1, 1 }));
            pchk(d2.ok && d2.recipe && d2.liveRec == "hullplates" && d2.targetRec == "titaniumplates" && d2.ports.empty(), "diff: other live recipe = recipe change");
            PCfgDiff d3 = pp_cfg_diff(tgt, pp_cfg_recipe("titaniumplates", { 1, 0, 1, 1 }));
            pchk(d3.ok && !d3.recipe && d3.ports == std::vector<int>({ 1 }), "diff: port 1 disabled live = one toggle at index 1");
            PCfgDiff d4 = pp_cfg_diff(tgt, {});
            pchk(!d4.ok, "diff: empty live = unknown, nothing sent");
            PCfgDiff d5 = pp_cfg_diff(tgt, pp_cfg_recipe("titaniumplates", { 1, 1, 1 }));
            pchk(!d5.ok, "diff: port count differs = unknown");
            PCfgDiff d6 = pp_cfg_diff(tgt, { 1, 0, 9, 9 });
            pchk(!d6.ok, "diff: live not recipe layout = unknown");
        }
        // bp_import (M4): cost net, status text, removes order, fingerprint, big blueprint, atomic write
        {
            PCostMap a2{ { "ITEM_A", 2 } }, a1b3{ { "ITEM_A", 1 }, { "ITEM_B", 3 } };
            PImpCost ic = pp_import_cost({ a2, a2, PCostMap{} }, { a1b3 });   // the empty map = a kept machine: adds 0
            pchk(ic.need.size() == 1 && ic.need["ITEM_A"] == 4, "import cost: 2 placed (A:2 each) need A 4, kept adds 0");
            pchk(ic.refund.size() == 2 && ic.refund["ITEM_A"] == 1 && ic.refund["ITEM_B"] == 3, "import cost: 1 removed (A:1,B:3) refund A 1 B 3");
            pchk(ic.net["ITEM_A"] == 3 && ic.net["ITEM_B"] == -3, "import cost: net A 3 B -3 (negative allowed)");
            pchk(pp_import_cost_text(ic) == "ITEM_A|4|1|3\nITEM_B|0|3|-3\n", "import cost file: name|need|refund|net, sorted");
            pchk(pp_import_cost_text(pp_import_cost({ PCostMap{} }, {})).empty(), "import cost file: nothing to pay = empty");
            pchk(pp_import_text(2, 1, 0, PCostMap{}, 0) == "place 2, keep 1, remove 0, recipe changes 0", "import text: no refund part when R=0");
            PCostMap seven;
            for (int i = 0; i < 7; ++i) seven["ITEM_" + std::string(1, static_cast<char>('a' + i))] = i + 1;
            pchk(pp_import_text(0, 0, 3, seven, 0) == "place 0, keep 0, remove 3 (refund a x1, b x2, c x3, d x4, e x5, +2 more), recipe changes 0",
                 "import text: 7 refund items show 5 + '+2 more'");
            std::vector<PMach> rem;
            for (int i = 0; i < 3; ++i) { PMach r; r.bcls = "BP_ConveyorBeltObject"; rem.push_back(r); }
            PMach sp; sp.bcls = "BP_Splitter"; rem.push_back(sp);
            pchk(pp_import_removes(rem) == "BP_ConveyorBeltObject:3,BP_Splitter:1", "import removes: 3 belts, 1 splitter, most numerous first");
            PMach f1; f1.id = 7; f1.bcls = "BP_Splitter"; f1.x = 1; f1.y = 1;
            PMach f2 = f1; f2.id = 8; f2.x = 3;
            PMach f2m = f2; f2m.x = 5;
            const uint64_t fa = pp_fingerprint({ f1, f2 });
            pchk(fa == pp_fingerprint({ f1, f2 }) && fa != pp_fingerprint({ f1, f2m }) && fa != pp_fingerprint({ f1 }), "fingerprint: stable, changes when one machine moves or goes");
            std::ostringstream big;
            const int nBig = 3000;
            big << "{\"Version\":2,\"Blocks\":[],\"DefaultMachines\":[";
            for (int i = 0; i < nBig; ++i)
                big << (i ? "," : "") << "{\"Class\":\"BP_ConveyorBeltObject\",\"DefaultName\":\"DAT_ConveyorBelt\",\"Transform\":{\"GridLocation\":{\"X\":" << ((i % 200) * 2 + 1)
                    << ",\"Y\":" << ((i / 200) * 2 + 1) << "},\"Height\":0,\"GridRotation\":0},\"MachineID\":" << (i + 1) << "}";
            big << "]}";
            const std::string bigs = big.str();
            PasteDoc bd;
            std::string berr;
            const bool bok = pp_parse_doc(bigs, bd, berr);
            pchk(bok && bd.machines.size() == static_cast<size_t>(nBig), ("big blueprint ~" + std::to_string(bigs.size() / 1024) + " KB: 3000 machines parse, count matches").c_str());
            const std::filesystem::path wp = get_dll_dir() / "BlueprintImport.selftest.txt";
            std::filesystem::path tp = wp;
            tp += L".tmp";
            try { write_atomic(wp, "selftest\n"); } catch (...) {}
            std::error_code wec;
            const bool wok = std::filesystem::exists(wp, wec) && !std::filesystem::exists(tp, wec);
            std::filesystem::remove(wp, wec);
            pchk(wok, "write_atomic: file written, no .tmp left");
        }
        bp_out(L"SELFTEST paste " + std::to_wstring(pass) + L"/" + std::to_wstring(total));
        bp_flush();
        return pass == total ? widen("ok selftest PASS " + std::to_string(pass) + "/" + std::to_string(total)) : STR("error: selftest FAIL (see BPProbe.txt)");
    }

    // ===== bp_obj <machineId> (read only): full property dump of one live SaveableObject (object props one level, Recipe Name included) =====
    struct BpObjCtx { int id = 0; bool found = false; std::wstring out; };
    void bp_obj_fn(void* p)
    {
        auto* c = static_cast<BpObjCtx*>(p);
        std::vector<UObject*> sv;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), sv);
        for (UObject* o : sv)
            if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(o) == c->id)
            {
                c->found = true;
                c->out += L"CLASS " + o->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(o)) + L" name=" + o->GetName() + L"\n";
                bp_dump(c->out, reinterpret_cast<const uint8_t*>(o), o->GetClassPrivate(), nullptr);
                return;
            }
    }
    std::wstring bp_obj_cmd(const std::string& cmd)
    {
        const std::string arg = cmd.size() > 7 ? cmd.substr(7) : std::string();
        if (arg.empty() || !std::all_of(arg.begin(), arg.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return STR("error: usage bp_obj <machineId>");
        bp_file_init();
        g_bp.arr_lim = 1000;
        const int id = std::atoi(arg.c_str());
        bp_out(L"=== bp_obj " + std::to_wstring(id));
        BpObjCtx c;
        c.id = id;
        const bool ok = seh_run(bp_obj_fn, &c);
        if (!ok) bp_out(L"FAULT bp_obj " + seh_describe(g_seh));
        else if (!c.found) bp_out(L"bp_obj: no live object with id " + std::to_wstring(id));
        bp_out(c.out);
        g_bp.arr_lim = 64;
        bp_flush();
        if (!ok) return STR("error: bp_obj fault (see BPProbe.txt)");
        return c.found ? widen("ok bp_obj " + arg + " written to BPProbe.txt") : widen("error: no live object with id " + arg);
    }

    // ===== bp_recipes (read only, writes dlls\RecipeNames.json): every live Recipe asset =====
    // WHY: the Recipe asset Name is the config string (see pp_recipe_fn); crafter / output are optional, written only when readable.
    struct PRecOut { std::string name, crafter, output; bool hasCrafter = false, hasOutput = false; };
    struct PRecListCtx { std::map<std::string, PRecOut> rows; std::wstring log; };
    // WHAT: JSON string body: escapes " and \ and control characters (<0x20). UTF-8 bytes pass through.
    std::string pp_jesc(const std::string& s)
    {
        std::string o;
        for (unsigned char ch : s)
        {
            if (ch == '"' || ch == '\\') { o += '\\'; o += static_cast<char>(ch); }
            else if (ch < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", ch); o += b; }
            else o += static_cast<char>(ch);
        }
        return o;
    }
    // WHAT: under seh_run: all live Recipe assets (deduped by object). Name, crafter (first property whose name contains "crafter", object name
    //       or text), Output ItemStack item label. Property layout of the first one goes to c->log.
    void bp_recipes_fn(void* p)
    {
        auto* c = static_cast<PRecListCtx*>(p);
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("Recipe"), all);
        std::set<UObject*> uniq;
        for (UObject* o : all) if (alive(o) && o->GetName().rfind(STR("Default__"), 0) != 0) uniq.insert(o);
        if (!uniq.empty())
        {
            std::set<UStruct*> seen;
            c->log += L"LAYOUT " + (*uniq.begin())->GetClassPrivate()->GetName() + L"\n";
            bp_layout(c->log, (*uniq.begin())->GetClassPrivate(), L"  ", 0, seen);
        }
        for (UObject* o : uniq)
        {
            PRecOut r;
            pp_recipe_name(o, r.name);
            const uint8_t* ob = reinterpret_cast<const uint8_t*>(o);
            for (UStruct* u = o->GetClassPrivate(); u && u->GetName() != STR("Object") && !r.hasCrafter; u = u->GetSuperStruct())
                for (FProperty* f : u->ForEachProperty())
                {
                    if (r.hasCrafter || lower(f->GetName()).find(L"crafter") == std::wstring::npos) continue;
                    if (CastField<FObjectProperty>(f))
                    {
                        UObject* co = *reinterpret_cast<UObject* const*>(ob + f->GetOffset_Internal());
                        if (alive(co)) { r.crafter = pp_short(to_utf8(co->GetName())); r.hasCrafter = true; }
                    }
                    else if (CastField<FStrProperty>(f) || CastField<FNameProperty>(f))
                    {
                        const uint8_t* d = ob + f->GetOffset_Internal();
                        std::wstring w;
                        if (CastField<FStrProperty>(f)) { const auto& a = reinterpret_cast<const FString*>(d)->GetCharArray(); if (a.Num() > 0 && a.GetData()) w = a.GetData(); }
                        else w = std::wstring(reinterpret_cast<const FName*>(d)->ToString());
                        if (!w.empty()) { r.crafter = to_utf8(w); r.hasCrafter = true; }
                    }
                }
            UStruct* ost = nullptr;
            if (const uint8_t* sb = pp_struct(o->GetClassPrivate(), ob, L"Output", ost))
            {
                uint64_t item = 0;
                int32_t amt = 0;
                int off = 0;
                UObject* it = nullptr;
                if (stack_read(ost, sb, item, amt, off) && item) it = reinterpret_cast<UObject*>(static_cast<uintptr_t>(item));
                if (alive(it)) { r.output = pp_item_label(it); r.hasOutput = true; }
            }
            c->rows[to_utf8(o->GetName())] = r;
        }
    }
    std::wstring bp_recipes_cmd()
    {
        bp_file_init();
        bp_out(L"=== bp_recipes");
        PRecListCtx c;
        if (!seh_run(bp_recipes_fn, &c)) { bp_out(L"FAULT bp_recipes " + seh_describe(g_seh)); bp_flush(); return STR("error: bp_recipes fault (see BPProbe.txt)"); }
        bp_out(L"=== bp_recipes layout");
        bp_out(c.log);
        std::string js = "{";
        bool first = true;
        for (const auto& kv : c.rows)
        {
            const PRecOut& r = kv.second;
            js += std::string(first ? "" : ",") + "\n\"" + pp_jesc(kv.first) + "\":{\"name\":\"" + pp_jesc(r.name) + "\"";
            if (r.hasCrafter) js += ",\"crafter\":\"" + pp_jesc(r.crafter) + "\"";
            if (r.hasOutput) js += ",\"output\":\"" + pp_jesc(r.output) + "\"";
            js += "}";
            first = false;
        }
        js += "\n}\n";
        try { write_atomic(get_dll_dir() / "RecipeNames.json", js); }
        catch (...) { bp_out(L"FAULT bp_recipes cannot write RecipeNames.json"); bp_flush(); return STR("error: cannot write RecipeNames.json"); }
        bp_out(L"RECIPES " + std::to_wstring(c.rows.size()) + L" written");
        bp_flush();
        return widen("ok recipes " + std::to_string(c.rows.size()) + " written to RecipeNames.json");
    }

    // WHAT: one 0.25 s step (from cmd_tick). Idle unless armed. Drains up to 20 captures, runs the +1.5 s / +5 s checks,
    //       takes baselines every period_s, reports faults and skip counts, flushes the file.
    void bp_tick(double)
    {
        bp_place_tick();
        bp_run_tick();
        bp_paste_tick();
        {
            std::lock_guard<std::mutex> lk(g_bp.mu);   // Probe 7: HITLOG lines from the hook
            for (const std::wstring& l : g_bp.hitq) bp_out(l);
            g_bp.hitq.clear();
        }
        bp_flush();
        if (!g_bp.armed) return;
        const ULONGLONG now = GetTickCount64();
        std::vector<BpCap> batch;
        {
            std::lock_guard<std::mutex> lk(g_bp.mu);
            while (!g_bp.q.empty() && batch.size() < 20) { batch.push_back(std::move(g_bp.q.front())); g_bp.q.pop_front(); }
        }
        for (auto& cap : batch)
        {
            bp_format_one(cap);
            bp_trigger(cap);
        }
        if (g_bp.pending)
        {
            if (g_bp.stage == 0 && now >= g_bp.trig_ms + 1500)
            {
                BpSnap s;
                const bool ok = bp_snap(s);
                bp_out(L"SNAPSHOT +1.5s trigger=" + g_bp.trig_what);
                if (ok)
                {
                    bp_diff(g_bp.ref, s);
                    std::vector<int> added;
                    for (auto& kv : s.ids) if (!g_bp.ref.ids.count(kv.first)) added.push_back(kv.first);
                    bp_new_machine_dump(added);
                    bp_scan_hooks();
                    if (g_bp.trig_what.find(L"PendingBlueprint") != std::wstring::npos) { bp_inv_full(s, L"AFTER +1.5s"); bp_bpdump_all(); }
                }
                g_bp.stage = 1;
            }
            if (g_bp.stage == 1 && now >= g_bp.trig_ms + 5000)
            {
                BpSnap s;
                const bool ok = bp_snap(s);
                bp_out(L"SNAPSHOT +5s trigger=" + g_bp.trig_what);
                if (ok)
                {
                    bp_diff(g_bp.ref, s);
                    if (g_bp.trig_what.find(L"PendingBlueprint") != std::wstring::npos) bp_inv_full(s, L"AFTER +5s");
                }
                g_bp.pending = false;
                g_bp.stage = 0;
            }
        }
        else if (now >= g_bp.next_base)
        {
            BpSnap s;
            if (bp_snap(s))
            {
                g_bp.base.push_back(s);
                while (g_bp.base.size() > 6) g_bp.base.pop_front();
            }
            g_bp.next_base = now + static_cast<ULONGLONG>(g_bp.period_s * 1000.0);
            if (!g_bp.base_logged)
            {
                g_bp.base_logged = true;
                bp_out(L"BASELINE ms=" + std::to_wstring(s.ms) + L" period=" + std::to_wstring(g_bp.period_s));
            }
        }
        if (g_bp.faults > 0 && g_bp.faults != g_bp.faults_logged)
        {
            g_bp.faults_logged = g_bp.faults;
            bp_out(L"HOOK FAULTS " + std::to_wstring(g_bp.faults));
        }
        if (now >= g_bp.next_skip_log)
        {
            g_bp.next_skip_log = now + 60000;
            if (!g_bp.skipped.empty())
            {
                std::wstring r = L"SKIPPED classes:";
                for (auto& kv : g_bp.skipped) r += L" " + kv.first + L" x" + std::to_wstring(kv.second);
                bp_out(r);
            }
        }
        if (now >= g_bp.next_eh) { g_bp.next_eh = now + 1000; bp_edit_run(); }
        if (now >= g_bp.next_des) { g_bp.next_des = now + 5000; bp_scan_hooks(true); }
        if (now >= g_bp.next_stat)
        {
            g_bp.next_stat = now + 60000;
            for (auto& h : g_bp.hooks)
                if (h.hits != h.stat_hits)
                {
                    h.stat_hits = h.hits;
                    bp_out(L"HOOKSTAT " + h.name + L" hits=" + std::to_wstring(h.hits) + L" logged=" + std::to_wstring(h.logged) + L" dup=" + std::to_wstring(h.dup) + (h.logged >= h.cap && !h.input ? L" CAPPED" : L""));
                }
            if (g_bp.dropped) bp_out(L"QUEUE DROPPED " + std::to_wstring(g_bp.dropped));
        }
        bp_flush();
    }

    // WHAT: dispatch for "bp_*" commands (from run_cmd). Unknown names -> error.
    std::wstring bp_cmd(const std::string& cmd)
    {
        if (cmd == "bp_arm") return bp_arm_cmd();
        if (cmd == "bp_dump") return bp_dump_cmd();
        if (cmd == "bp_sig") return bp_sig_cmd();
        if (cmd.rfind("bp_place ", 0) == 0) return bp_place_cmd(cmd);
        if (cmd == "bp_cls") return bp_cls_cmd();
        if (cmd == "bp_bld") return bp_bld_cmd();
        if (cmd == "bp_cd" || cmd.rfind("bp_cd ", 0) == 0) return bp_cd_cmd(cmd);
        if (cmd.rfind("bp_send ", 0) == 0) return bp_send_cmd(cmd);
        if (cmd.rfind("bp_belts ", 0) == 0) return bp_belts_cmd(cmd);
        if (cmd.rfind("bp_kill ", 0) == 0) return bp_kill_cmd(cmd);
        if (cmd == "bp_run" || cmd.rfind("bp_run ", 0) == 0) return bp_run_cmd(cmd);
        if (cmd == "bp_aim" || cmd.rfind("bp_aim ", 0) == 0) return bp_aim_cmd(cmd);
        if (cmd == "bp_confirm") return bp_confirm_cmd();
        if (cmd == "bp_enter" || cmd.rfind("bp_enter ", 0) == 0) return bp_enter_cmd(cmd);
        if (cmd == "bp_paste" || cmd.rfind("bp_paste ", 0) == 0) return bp_paste_cmd(cmd);
        if (cmd == "bp_paste_selftest") return bp_paste_selftest_cmd();
        if (cmd.rfind("bp_import ", 0) == 0) return bp_import_cmd(cmd);
        if (cmd.rfind("bp_obj ", 0) == 0) return bp_obj_cmd(cmd);
        if (cmd == "bp_recipes") return bp_recipes_cmd();
        if (cmd == "bp_paste_station") return pp_fail("import station (blocks) is not implemented yet; use Import factory");
        if (cmd == "bp_disarm")
        {
            g_bp.armed = false;
            bp_out(L"=== bp_disarm");
            bp_flush();
            return STR("ok");
        }
        return STR("error: unknown bp cmd");
    }
}
