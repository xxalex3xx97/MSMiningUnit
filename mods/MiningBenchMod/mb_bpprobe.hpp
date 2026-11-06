// FILE: mods/MiningBenchMod/mb_bpprobe.hpp
// WHAT: M0 probe logger for the factory blueprint work. Off by default. MiningCmd.txt commands bp_arm, bp_dump, bp_disarm.
//       bp_arm hooks the build/place/construct functions of the player controller and the station-side classes, captures their
//       raw parameters on the hook (no logging there), and writes a decoded dump plus inventory snapshot diffs to dlls\BPProbe.txt.
//       bp_dump writes one station dump (allowed while unarmed). Output capped at 8 MB.
// WHY separate: the probe is a read-only research tool; it must not touch the mining loop. Everything it does is reflection reads
//       and log writes. It never calls RemoveItems / AddItems / any game function that changes state.
// DEPENDS ON: mb_common.hpp (alive, seh_run, g_seh, seh_describe, get_dll_dir, to_utf8, lower, log_line), mb_ue.hpp (obj_id, first_of,
//             get_obj_prop, get_int_prop, type_text, any_prop, pvec_text, find_by_id), mb_devcmd.hpp (station_ids, stack_read).
// USED BY: dllmain.cpp (include), mb_testcmd.hpp (run_cmd -> bp_cmd for "bp_*" commands; cmd_tick -> bp_tick every 0.25 s).
// GAME FACTS: the hook callback runs inside the game's ProcessEvent path. A native fault there kills the game, so the callback only
//       copies bytes (under seh_run). Every object read happens under seh_run too (dangling pointers are possible).
#pragma once

#include <mutex>
#include <deque>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>

namespace
{
    // WHAT: a hooked function and its capture state. Lives in g_bp.hooks (a deque, so the address given to RegisterHook stays valid).
    struct BpHook { std::wstring name; UFunction* fn = nullptr; bool input = false; int hits = 0; int cap = 40; std::vector<size_t> objoffs; };
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
        bool full = false;                   // 8 MB reached: output dropped
        std::deque<BpSnap> base;             // baseline snapshots, newest last (keep 3)
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
    };
    BpState g_bp;

    constexpr size_t kBpCapBytes = 8u * 1024u * 1024u;   // BPProbe.txt cap
    constexpr size_t kBpMaxHooks = 150;
    constexpr int kBpCapPerFn = 40;

    // WHAT: command classes whose captures are dropped (player controls, not factory work). Lowercase match, see bp_has.
    const wchar_t* const kBpSkip[] = { L"ShipMovement", L"ShipFiring", L"InputView", L"ViewTarget", L"Rotation", L"Throttle", L"Steering", L"Camera", L"CharacterMove", L"SetPiloting" };
    constexpr size_t kBpSkipN = sizeof(kBpSkip) / sizeof(kBpSkip[0]);
    // WHAT: function-name words that make a function a hook candidate (lowercase).
    const wchar_t* const kBpMatch[] = { L"build", L"place", L"construct", L"machine", L"disassemble", L"belt", L"conveyor" };
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

    // WHAT: writes the queued text to dlls\BPProbe.txt (append). Past 8 MB: one FILE FULL line, then nothing.
    void bp_flush()
    {
        if (g_bp.buf.empty()) return;
        std::string out;
        out.swap(g_bp.buf);
        if (g_bp.full) return;
        if (g_bp.file_bytes + out.size() > kBpCapBytes)
        {
            g_bp.full = true;
            out = bp_stamp() + "FILE FULL (8 MB) - output dropped\n";
        }
        std::ofstream o(get_dll_dir() / L"BPProbe.txt", std::ios::binary | std::ios::app);
        o << out;
        g_bp.file_bytes += out.size();
    }

    // WHAT: hook callback context: the UE4SS call context and our hook record.
    struct BpHookCtx { RC::Unreal::UnrealScriptFunctionCallableContext* c; BpHook* h; };

    // WHAT: runs inside seh_run on every hooked call while armed. Copies the parameter block and the object parameters, queues the capture.
    // WHY: nothing is logged or formatted here (the game thread is inside the game's own call). bp_format_fn does that later.
    void bp_capture_fn(void* p)
    {
        auto* x = static_cast<BpHookCtx*>(p);
        BpHook* h = x->h;
        if (!g_bp.armed) return;
        ++h->hits;
        if (!h->input && h->hits > h->cap) return;
        uint8_t* base = x->c->TheStack.Locals();
        if (!base) return;
        BpCap cap;
        cap.ms = GetTickCount64();
        cap.h = h;
        cap.hit = h->hits;
        cap.parms.assign(base, base + h->fn->GetParmsSize());
        UObject* self = x->c->Context;
        if (alive(self)) { cap.selfcls = self->GetClassPrivate()->GetName(); cap.selfid = obj_id(self); }
        for (size_t off : h->objoffs)
        {
            UObject* o = *reinterpret_cast<UObject* const*>(base + off);
            if (!alive(o)) continue;
            UClass* c = o->GetClassPrivate();
            const std::wstring cn = c->GetName();
            if (bp_has(cn, kBpSkip, kBpSkipN)) { ++g_bp.skipped[cn]; return; }
            BpObj b;
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
                        const int n = std::min<int>(ta->Num(), 64);
                        const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
                        if (n > 0 && ta->GetData())
                            b.arrs.push_back({ static_cast<size_t>(pr->GetOffset_Internal()), std::vector<uint8_t>(ta->GetData(), ta->GetData() + n * es) });
                    }
            cap.objs.push_back(std::move(b));
        }
        std::lock_guard<std::mutex> lk(g_bp.mu);
        if (g_bp.q.size() < 2000) g_bp.q.push_back(std::move(cap));
    }

    // WHAT: pre-hook: the SEH-guarded capture. A fault is counted, never thrown into the game.
    void bp_hook_pre(RC::Unreal::UnrealScriptFunctionCallableContext& ctx, void* custom)
    {
        BpHookCtx c{ &ctx, static_cast<BpHook*>(custom) };
        if (!g_bp.armed) return;
        if (!seh_run(bp_capture_fn, &c)) ++g_bp.faults;
    }

    // WHAT: post-hook placeholder (RegisterHook needs both callbacks). Does nothing.
    void bp_hook_post(RC::Unreal::UnrealScriptFunctionCallableContext&, void*) {}

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
            if (alive(it)) name = it->GetName();
            else { wchar_t hx[32]; swprintf(hx, 32, L"0x%llx", static_cast<unsigned long long>(item)); name = hx; }
            m[key + L"|" + name] += amt;
        }
    }

    // WHAT: one snapshot pass, under seh_run. ItemTotals of machine/construction inventories (via InventoryID), the player and every ship;
    //       the machine ids per station (id -> class).
    struct BpSnapCtx { BpSnap* out; };
    void bp_snap_fn(void* p)
    {
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
            if (alive(pl)) bp_inv_read(get_obj_prop(pl, STR("Inventory")), STR("player"), o.inv);
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* sh : ships)
            if (alive(sh) && sh->GetName().rfind(STR("Default__"), 0) != 0)
                bp_inv_read(get_obj_prop(sh, STR("Inventory")), L"ship" + std::to_wstring(obj_id(sh)), o.inv);
    }

    // WHAT: takes a snapshot (under seh_run) and sets g_bp.period_s = max(3 s, 30 x snapshot ms). false on a fault (logged).
    bool bp_snap(BpSnap& out)
    {
        out = BpSnap{};
        out.ms = GetTickCount64();
        const ULONGLONG t0 = GetTickCount64();
        BpSnapCtx c{ &out };
        const bool ok = seh_run(bp_snap_fn, &c);
        const double ms = static_cast<double>(GetTickCount64() - t0);
        g_bp.period_s = std::max(3.0, ms * 0.03);
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
            if (depth >= 4) return STR("{...}");
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
            std::wstring r = o->GetClassPrivate()->GetName() + L"#" + std::to_wstring(obj_id(o));
            if (top == 1) r += bp_live_scalars(reinterpret_cast<const uint8_t*>(o), o->GetClassPrivate(), depth);
            return r;
        }
        if (auto* ap = CastField<FArrayProperty>(p))
        {
            const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(d);
            const int num = ta->Num();
            FProperty* in = ap->GetInner();
            const size_t es = static_cast<size_t>(in->GetElementSize());
            std::wstring r = L"count=" + std::to_wstring(num) + L" [";
            const uint8_t* src = nullptr;
            int avail = 0;
            if (arr_buf) { src = arr_buf->data(); avail = es ? static_cast<int>(arr_buf->size() / es) : 0; }
            else if (ta->GetData()) { src = reinterpret_cast<const uint8_t*>(ta->GetData()); avail = num; }
            const int n = std::min(std::min(num, 16), avail);
            for (int i = 0; src && i < n; ++i)
            {
                if (i) r += L",";
                const uint8_t* e = src + static_cast<size_t>(i) * es;
                if (CastField<FArrayProperty>(in)) r += L"count=" + std::to_wstring(reinterpret_cast<const TArray<uint8_t>*>(e)->Num());
                else r += bp_val(e, in, e, depth + 1, 0);
            }
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
            s += L"  OBJ param@" + std::to_wstring(b.off) + L" class=" + b.cls->GetName() + L" id=" + std::to_wstring(b.id) + L"\n";
            bp_dump(s, b.bytes.data(), b.cls, &b, b.bytes.size());
        }
    }

    // WHAT: the trigger: a capture starts a +1.5 s / +5 s check (first capture only); later captures add their hook name to the same check.
    // WHY: the diff needs a baseline older than the capture, so the newest baseline with ms <= trigger time is used.
    void bp_trigger(const BpCap& cap)
    {
        if (g_bp.pending) { g_bp.trig_what += L" + " + cap.h->name; return; }
        g_bp.pending = true;
        g_bp.trig_ms = cap.ms;
        g_bp.stage = 0;
        g_bp.trig_what = cap.h->name + (cap.objs.empty() ? std::wstring() : L" " + cap.objs[0].cls->GetName());
        if (g_bp.base.empty())
        {
            BpSnap s;
            if (bp_snap(s)) g_bp.ref = s;
            bp_out(L"no baseline: snapshot now");
            return;
        }
        const BpSnap* pick = &g_bp.base.front();
        for (auto& b : g_bp.base) if (b.ms <= cap.ms) pick = &b;
        g_bp.ref = *pick;
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
    struct BpScanCtx { int r = -1; };
    bool bp_add_hook(UFunction* fn, bool input)
    {
        if (!fn || g_bp.hooks.size() >= kBpMaxHooks) return false;
        std::wstring path = fn->GetFullName();   // "Function /Script/...:Name" -> strip the type word
        { size_t sp = path.find(L' '); if (sp != std::wstring::npos) path = path.substr(sp + 1); }
        if (g_bp.hooked.count(path)) return false;
        g_bp.hooks.emplace_back();
        BpHook& h = g_bp.hooks.back();
        h.name = path;
        h.fn = fn;
        h.input = input;
        h.cap = input ? 0 : kBpCapPerFn;
        for (FProperty* pr : fn->ForEachProperty())
            if (CastField<FObjectProperty>(pr) && !pr->HasAnyPropertyFlags(CPF_ReturnParm)) h.objoffs.push_back(static_cast<size_t>(pr->GetOffset_Internal()));
        try { UObjectGlobals::RegisterHook(path, bp_hook_pre, bp_hook_post, &h); }
        catch (...) { bp_out(L"HOOK FAIL " + path); g_bp.hooks.pop_back(); return false; }
        g_bp.hooked.insert(path);
        bp_out(L"HOOK + " + path);
        return true;
    }

    void bp_scan_fn(void* p)
    {
        auto* c = static_cast<BpScanCtx*>(p);
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { c->r = -1; return; }
        const size_t before = g_bp.hooks.size();
        bp_add_hook(ctrl->GetFunctionByNameInChain(STR("AddInputCommand")), true);
        std::set<UClass*> classes;
        classes.insert(ctrl->GetClassPrivate());
        std::vector<UObject*> stations;
        UObjectGlobals::FindAllOf(STR("StationObject"), stations);
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("SaveableObject"), all);
        std::map<int, UObject*> byid;
        for (UObject* o : all) if (alive(o)) byid[obj_id(o)] = o;
        for (UObject* st : stations)
        {
            if (!alive(st) || st->GetName().rfind(STR("Default__"), 0) == 0) continue;
            classes.insert(st->GetClassPrivate());
            std::vector<int> ids = station_ids(st, STR("MachineIDs"));
            for (int id : station_ids(st, STR("ConstructionObjectIDs"))) ids.push_back(id);
            for (int id : station_ids(st, STR("PlatformIDs"))) ids.push_back(id);
            for (int id : ids)
            {
                auto it = byid.find(id);
                if (it != byid.end() && alive(it->second)) classes.insert(it->second->GetClassPrivate());
            }
        }
        for (UClass* c : classes)
            for (UStruct* u = c; u && g_bp.hooks.size() < kBpMaxHooks; u = u->GetSuperStruct())
            {
                if (u->GetFullName().find(L"/Script/Engine.") != std::wstring::npos) break;
                if (UClass* uc = Cast<UClass>(u))
                    for (UFunction* f : uc->ForEachFunction())
                    {
                        const std::wstring n = lower(f->GetName());
                        if (!bp_has(n, kBpMatch, kBpMatchN) || bp_starts(n, kBpExcl, kBpExclN)) continue;
                        bp_add_hook(f, false);
                    }
            }
        c->r = static_cast<int>(g_bp.hooks.size() - before);
        bp_out(L"HOOK total " + std::to_wstring(g_bp.hooks.size()) + L" new " + std::to_wstring(c->r));
    }

    int bp_scan_hooks()
    {
        BpScanCtx c;
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

    // WHAT: station dump context: one string filled under seh_run.
    struct BpDumpCtx { std::wstring out; };
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

    // WHAT: bp_dump command: one station dump now, allowed while the probe is not armed.
    std::wstring bp_dump_cmd()
    {
        bp_out(L"=== STATION DUMP");
        BpDumpCtx c;
        const bool ok = seh_run(bp_station_fn, &c);
        bp_out(c.out);
        if (!ok) bp_out(L"FAULT station dump " + seh_describe(g_seh));
        bp_flush();
        return ok ? STR("ok dump written to BPProbe.txt") : STR("error: station dump fault");
    }

    // WHAT: bp_arm: truncates BPProbe.txt if it is over 8 MB, arms the hooks (rescan), next baseline now.
    std::wstring bp_arm_cmd()
    {
        const auto path = get_dll_dir() / L"BPProbe.txt";
        std::error_code ec;
        const auto sz = std::filesystem::file_size(path, ec);
        if (!ec && sz > kBpCapBytes) { std::ofstream t(path, std::ios::binary | std::ios::trunc); g_bp.file_bytes = 0; }
        else g_bp.file_bytes = ec ? 0 : static_cast<size_t>(sz);
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
        g_bp.next_base = 0;
        g_bp.next_skip_log = GetTickCount64() + 60000;
        return L"ok hooks=" + std::to_wstring(g_bp.hooks.size());
    }

    // WHAT: one 0.25 s step (from cmd_tick). Idle unless armed. Drains up to 20 captures, runs the +1.5 s / +5 s checks,
    //       takes baselines every period_s, reports faults and skip counts, flushes the file.
    void bp_tick(double)
    {
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
                }
                g_bp.stage = 1;
            }
            if (g_bp.stage == 1 && now >= g_bp.trig_ms + 5000)
            {
                BpSnap s;
                const bool ok = bp_snap(s);
                bp_out(L"SNAPSHOT +5s trigger=" + g_bp.trig_what);
                if (ok) bp_diff(g_bp.ref, s);
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
                while (g_bp.base.size() > 3) g_bp.base.pop_front();
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
        bp_flush();
    }

    // WHAT: dispatch for "bp_*" commands (from run_cmd). Unknown names -> error.
    std::wstring bp_cmd(const std::string& cmd)
    {
        if (cmd == "bp_arm") return bp_arm_cmd();
        if (cmd == "bp_dump") return bp_dump_cmd();
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
