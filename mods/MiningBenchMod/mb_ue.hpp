// FILE: mods/MiningBenchMod/mb_ue.hpp
// WHAT: generic Unreal reflection helpers: read and write properties by name, PNum/PVector/Quat readers, ProcessEvent calls, struct and class dumps, inventory totals. No feature rules.
// WHY separate: shared plumbing used by every feature; kept free of feature rules so it can be reused.
// DEPENDS ON (exact): mb_common.hpp, mb_config.hpp (RAW_PER_METER, g_raw_per_m)
// USED BY: the later mb_*.hpp feature headers and dllmain.cpp (all include it before the feature headers)
// GAME FACTS:
//   ASSUMPTION: no game SDK headers in this build, so every field and function is found by reflection name (log 2026-10-27).
//   PNum = struct with one int32 or int64 field Value, fixed point, 65536 raw per m (ASSUMPTION, see mb_config.hpp).
//   PVector = X, Y, Z PNum fields. Positions of saved objects are CHUNK-RELATIVE: Location plus ChunkLocation (brief Update 28, log 2026-10-27). A Location alone is not the world position.
//   Quat = W, X, Y, Z PNum fields; divide by 65536 for a unit quaternion.
//   Actor getters such as K2_GetActorLocation return floats in UE units: 100 uu = 1 m (UE standard).
#pragma once

// Reflection helpers: anonymous namespace, internal to each .cpp. Everything is looked up by name because the game SDK headers are not available to this build (log 2026-10-27).
namespace
{
    // WHAT: number of BPO_MiningBenchBlock_C entries in the TArray property prop (e.g. the ship Subobjects list); -1 if missing.
    // WHY: identifies the mining block by its class name, so no header is needed. The caller passes a live ship (no alive() here).
    int count_bench(UObject* ship, const TCHAR* prop)
    {
        auto* arr = ship->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(prop);
        if (!arr) return -1;
        int n = 0;
        for (int32_t i = 0; i < arr->Num(); ++i)
        {
            UObject* e = (*arr)[i];
            if (e && e->GetClassPrivate())
            {
                auto nm = e->GetClassPrivate()->GetName();
                if (nm == STR("BPO_MiningBenchBlock_C") || nm == STR("BPO_MinerModuleBlock_C")) ++n;
            }
        }
        return n;
    }

    // WHAT: reads an int32 field named field from struct property sprop of obj; false if the layout differs.
    // WHY: nested int fields (e.g. PNum.Value) are read at their offset, and a missing field returns false instead of crashing.
    bool read_struct_int(UObject* obj, const TCHAR* sprop, const TCHAR* field, int32_t& out)
    {
        auto* base = obj->GetValuePtrByPropertyNameInChain<uint8_t>(sprop);
        FProperty* p = obj->GetPropertyByNameInChain(sprop);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!base || !sp || !sp->GetStruct()) return false;
        FProperty* f = sp->GetStruct()->FindProperty(FName(field, FNAME_Add));
        if (!f) return false;
        out = *reinterpret_cast<int32_t*>(base + f->GetOffset_Internal());
        return true;
    }

    // WHAT: save object ID (int32 property ID), or -1 if missing.
    // WHY: saved objects (docks, ships, beacons) are addressed by this ID in the INI and in game calls.
    int obj_id(UObject* o)
    {
        auto* p = o->GetValuePtrByPropertyNameInChain<int32_t>(STR("ID"));
        return p ? *p : -1;
    }

    // WHAT: text of a struct's fields, recursively, with raw int values.
    // WHY: trace logs; raw values are shown unscaled so the units can be checked (scaled output is dump_struct_div).
    std::wstring dump_struct(const uint8_t* base, UStruct* st)
    {
        std::wstring s = STR("{");
        if (!st) return s + STR("?}");
        for (FProperty* p : st->ForEachProperty())
        {
            const uint8_t* d = base + p->GetOffset_Internal();
            s += p->GetName() + STR("=");
            if (CastField<FIntProperty>(p)) s += std::to_wstring(*reinterpret_cast<const int32_t*>(d));
            else if (CastField<FInt64Property>(p)) s += std::to_wstring(*reinterpret_cast<const int64_t*>(d));
            else if (CastField<FFloatProperty>(p)) s += std::to_wstring(*reinterpret_cast<const float*>(d));
            else if (CastField<FDoubleProperty>(p)) s += std::to_wstring(*reinterpret_cast<const double*>(d));
            else if (auto* sp = CastField<FStructProperty>(p)) s += dump_struct(d, sp->GetStruct());
            else s += STR("?");
            s += STR(" ");
        }
        return s + STR("}");
    }

    // WHAT: dump_struct of a struct property by name; "n/a" if missing.
    // WHY: one-line log of a struct such as a location.
    std::wstring struct_prop_text(UObject* obj, const TCHAR* prop)
    {
        auto* base = obj->GetValuePtrByPropertyNameInChain<uint8_t>(prop);
        FProperty* p = obj->GetPropertyByNameInChain(prop);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!base || !sp) return STR("n/a");
        return dump_struct(base, sp->GetStruct());
    }

    // WHAT: calls a no-argument UFunction via ProcessEvent and returns its String or Text result; "" otherwise.
    // WHY: Blueprint functions have no C++ entry point here. The parameter buffer is GetParmsSize plus 16 bytes of padding, and the return value sits at its return property offset.
    std::wstring call_text(UObject* o, const TCHAR* fname)
    {
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        if (!fn) return STR("");
        FProperty* rp = fn->GetReturnProperty();
        if (!rp) return STR("");
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        auto cls = rp->GetClass().GetFName().ToString();
        uint8_t* d = buf.data() + rp->GetOffset_Internal();
        if (cls == STR("StrProperty")) return std::wstring(**reinterpret_cast<FString*>(d));
        if (cls == STR("TextProperty")) return reinterpret_cast<FText*>(d)->ToString();
        return STR("");
    }

    // WHAT: calls a UFunction via ProcessEvent and dumps its struct return value; "n/a" if not a struct.
    // WHY: same ProcessEvent route as call_text, for struct results.
    std::wstring call_struct_text(UObject* o, const TCHAR* fname)
    {
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        if (!fn) return STR("n/a");
        FProperty* rp = fn->GetReturnProperty();
        auto* sp = rp ? CastField<FStructProperty>(rp) : nullptr;
        if (!sp) return STR("n/a");
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        return dump_struct(buf.data() + rp->GetOffset_Internal(), sp->GetStruct());
    }

    // WHAT: copies n bytes into a UFunction parameter buffer at the named parameter's offset; false if missing or it would overflow.
    // WHY: sets inputs for ProcessEvent calls without a header; the bounds check stops a wrong name from writing past the buffer.
    bool put(UFunction* fn, std::vector<uint8_t>& buf, const TCHAR* name, const void* src, size_t n)
    {
        FProperty* p = fn->FindProperty(FName(name, FNAME_Add));
        if (!p || static_cast<size_t>(p->GetOffset_Internal()) + n > buf.size()) return false;
        std::memcpy(buf.data() + p->GetOffset_Internal(), src, n);
        return true;
    }

    // WHAT: first live UObject of class cls, or nullptr.
    // WHY: FindAllOf walks every UObject in the game, so only the first hit is returned, not the whole list. Slow: do not call it every tick.
    UObject* first_of(const TCHAR* cls)
    {
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(cls, v);
        for (UObject* o : v) if (o) return o;
        return nullptr;
    }

    // WHAT: finds the object of class cls whose save ID is id. Result cached per (class, id).
    // WHY: FindAllOf walks every UObject and is very slow. The move and steer jobs call this every tick and made the game lag, so the last hit is kept and rescanned only when that object is gone (alive() false) or its ID changed.
    // WHAT: alive(o) && obj_id(o) == id, but a freed (dangling) pointer returns false instead of crashing the game.
    // WHY: a cached UObject can be freed by a save reload or dismantle; alive() then reads freed memory (access violation, crash log 2026-10-27).
    bool cached_ok(UObject* o, int id)
    {
        __try { return alive(o) && obj_id(o) == id; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    UObject* find_by_id(const TCHAR* cls, int id)
    {
        static std::map<std::pair<std::wstring, int>, UObject*> cache;
        const auto key = std::make_pair(std::wstring(cls), id);
        if (auto it = cache.find(key); it != cache.end())
        {
            if (cached_ok(it->second, id)) return it->second;
            cache.erase(it);
        }
        // WHY: a missing ship (removed, other sector) would run a full FindAllOf on every call; remember the miss for 3 s.
        static std::map<std::pair<std::wstring, int>, ULONGLONG> miss;
        const ULONGLONG now = GetTickCount64();
        if (auto m = miss.find(key); m != miss.end()) { if (now - m->second < 3000) return nullptr; miss.erase(m); }
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(cls, v);
        for (UObject* o : v) if (o && obj_id(o) == id) { cache[key] = o; return o; }
        miss[key] = now;
        return nullptr;
    }

    // read a TextProperty (e.g. DisplayName) directly; empty when missing or not a TextProperty
    // WHAT: text value of a TextProperty (e.g. DisplayName) read straight from memory; empty when missing or not text.
    // WHY: no ProcessEvent call needed. The first 6 reads are logged as a sanity check of this memory path.
    std::wstring read_text_prop(UObject* o, const TCHAR* prop)
    {
        if (!o) return STR("");
        FProperty* p = o->GetPropertyByNameInChain(prop);
        if (!p || p->GetClass().GetFName().ToString() != STR("TextProperty")) return STR("");
        auto* t = o->GetValuePtrByPropertyNameInChain<FText>(prop);
        // null text data = unset FText; ToString would dereference null (crash seen on a map-selected SolarSystem)
        std::wstring r = (t && t->GetTextData()) ? t->ToString() : STR("");
        static int dbg = 0;
        if (dbg < 6)
        {
            ++dbg;
            log_line(STR("read_text_prop ") + std::wstring(prop) + STR(" data=") + std::to_wstring(t ? reinterpret_cast<uintptr_t>(t->GetTextData()) : 0) + STR(" -> '") + r + STR("'"));
        }
        return r;
    }

    // WHAT: in-game display name of a save object.
    // WHY: DisplayName is read from memory first (cheap); the GetDisplayName Blueprint function is the fallback.
    std::wstring obj_display(UObject* o)
    {
        if (!o) return STR("");
        std::wstring r = read_text_prop(o, STR("DisplayName"));
        return r.empty() ? call_text(o, STR("GetDisplayName")) : r;
    }

    // WHAT: true when property prop exists on o and its type name equals type (e.g. IntProperty).
    // WHY: check the type before a typed read, otherwise the raw bytes are misread.
    bool prop_is(UObject* o, const TCHAR* prop, const TCHAR* type) { FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr; return p && p->GetClass().GetFName().ToString() == type; }

    // WHAT: bool property as "1" or "0", or "n/a".
    // WHY: text for logs; CastField does the type check.
    std::wstring prop_bool(UObject* o, const TCHAR* prop) { FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr; auto* bp = p ? CastField<FBoolProperty>(p) : nullptr; if (!bp) return STR("n/a"); return bp->GetPropertyValueInContainer(o) ? STR("1") : STR("0"); }

    // WHAT: IntProperty value as text, or "n/a".
    // WHY: prop_is guard before reading the int32.
    std::wstring prop_int(UObject* o, const TCHAR* prop) { if (!prop_is(o, prop, STR("IntProperty"))) return STR("n/a"); auto* v = o->GetValuePtrByPropertyNameInChain<int32_t>(prop); return v ? std::to_wstring(*v) : STR("n/a"); }

    // WHAT: element count of an array property as text, or "n/a".
    // WHY: only the count is needed, so a uint8 view avoids needing the element type.
    std::wstring prop_count(UObject* o, const TCHAR* prop) { if (!prop_is(o, prop, STR("ArrayProperty"))) return STR("n/a"); auto* a = o->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(prop); return a ? std::to_wstring(a->Num()) : STR("n/a"); }

    // WHAT: name of the class or object a Class or Object property points to; "null" or "n/a".
    // WHY: logs which object a field holds.
    std::wstring prop_class(UObject* o, const TCHAR* prop) { if (!prop_is(o, prop, STR("ClassProperty")) && !prop_is(o, prop, STR("ObjectProperty"))) return STR("n/a"); auto* pp = o->GetValuePtrByPropertyNameInChain<UObject*>(prop); if (!pp || !*pp) return STR("null"); return (*pp)->GetName(); }

    // WHAT: class names of the first 20 elements of an object array, comma-separated; "none" or "n/a".
    // WHY: compact log line; the cap stops long arrays from flooding the log.
    std::wstring prop_classes(UObject* o, const TCHAR* prop)
    {
        if (!prop_is(o, prop, STR("ArrayProperty"))) return STR("n/a");
        auto* arr = o->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(prop);
        if (!arr) return STR("n/a");
        std::wstring s;
        for (int32_t i = 0; i < arr->Num() && i < 20; ++i) { UObject* e = (*arr)[i]; s += (e && e->GetClassPrivate()) ? e->GetClassPrivate()->GetName() : std::wstring(STR("null")); s += STR(","); }
        return s.empty() ? std::wstring(STR("none")) : s;
    }

    // ---------------- trace + class dump ----------------
    // WHAT: like dump_struct, but int fields are divided by RAW_PER_METER and shown in m.
    // WHY: readable distances in trace logs. Caveat: every int field is divided, not only PNum values.
    std::wstring dump_struct_div(const uint8_t* base, UStruct* st)
    {
        std::wstring s = STR("{");
        if (!st) return s + STR("?}");
        for (FProperty* p : st->ForEachProperty())
        {
            const uint8_t* d = base + p->GetOffset_Internal();
            s += p->GetName() + STR("=");
            if (CastField<FIntProperty>(p)) s += std::to_wstring(*reinterpret_cast<const int32_t*>(d) / RAW_PER_METER);
            else if (CastField<FInt64Property>(p)) s += std::to_wstring(*reinterpret_cast<const int64_t*>(d) / RAW_PER_METER);
            else if (auto* sp = CastField<FStructProperty>(p)) s += dump_struct_div(d, sp->GetStruct());
            else s += STR("?");
            s += STR(" ");
        }
        return s + STR("}");
    }

    // WHAT: type name of a property, including struct, object and array element types.
    // WHY: class dumps use it to find the layout of game objects.
    std::wstring type_text(FProperty* p)
    {
        if (!p) return STR("?");
        std::wstring t = p->GetClass().GetFName().ToString();
        if (auto* sp = CastField<FStructProperty>(p)) { if (sp->GetStruct()) t += STR("<") + sp->GetStruct()->GetName() + STR(">"); }
        else if (auto* op = CastField<FObjectProperty>(p)) { if (op->GetPropertyClass()) t += STR("<") + op->GetPropertyClass()->GetName() + STR(">"); }
        else if (auto* ap = CastField<FArrayProperty>(p)) { t += STR("<") + type_text(ap->GetInner()) + STR(">"); }
        return t;
    }

    // WHAT: text of any supported property by name (bool, int, float, double, struct, object, array).
    // WHY: one call for the trace and dump code. Struct values show raw and raw/65536 (ASSUMPTION: 65536 is the fixed-point scale).
    std::wstring any_prop(UObject* o, const TCHAR* name)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(name) : nullptr;
        if (!p) return STR("n/a");
        uint8_t* base = reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal();
        if (CastField<FBoolProperty>(p)) return prop_bool(o, name);
        if (CastField<FIntProperty>(p)) return std::to_wstring(*reinterpret_cast<int32_t*>(base));
        if (CastField<FFloatProperty>(p)) return std::to_wstring(*reinterpret_cast<float*>(base));
        if (CastField<FDoubleProperty>(p)) return std::to_wstring(*reinterpret_cast<double*>(base));
        if (auto* sp = CastField<FStructProperty>(p)) return dump_struct(base, sp->GetStruct()) + STR(" /65536=") + dump_struct_div(base, sp->GetStruct());
        if (CastField<FObjectProperty>(p)) return prop_class(o, name);
        if (CastField<FArrayProperty>(p)) return STR("count=") + prop_count(o, name);
        return STR("?");
    }

    // WHAT: reads the X, Y, Z PNum fields of a struct into doubles, in raw units (no scaling). False if a field is missing.
    // WHY: positions are fixed-point ints; doubles make the later math simple and the values stay in raw units.
    bool vec3(const uint8_t* base, UStruct* st, double out[3])
    {
        if (!base || !st) return false;
        const TCHAR* names[3] = {STR("X"), STR("Y"), STR("Z")};
        for (int i = 0; i < 3; ++i)
        {
            FProperty* f = st->FindProperty(FName(names[i], FNAME_Add));
            auto* sp = f ? CastField<FStructProperty>(f) : nullptr;
            if (!sp || !sp->GetStruct()) return false;
            FProperty* v = sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
            if (!v) return false;
            const uint8_t* d = base + f->GetOffset_Internal() + v->GetOffset_Internal();
            if (CastField<FInt64Property>(v)) out[i] = static_cast<double>(*reinterpret_cast<const int64_t*>(d));
            else if (CastField<FIntProperty>(v)) out[i] = static_cast<double>(*reinterpret_cast<const int32_t*>(d));
            else return false;
        }
        return true;
    }

    // WHAT: location of an object in raw units: GetLocation (Blueprint function) first, the Location property as fallback.
    // WHY: which of the two is right for every object type is not known (ported as is). UNVERIFIED: whether either is chunk-relative (see banner).
    bool get_loc(UObject* o, double out[3])
    {
        if (!o) return false;
        UFunction* fn = o->GetFunctionByNameInChain(STR("GetLocation"));
        if (fn)
        {
            FProperty* rp = fn->GetReturnProperty();
            auto* rsp = rp ? CastField<FStructProperty>(rp) : nullptr;
            if (rsp && rsp->GetStruct())
            {
                std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
                o->ProcessEvent(fn, buf.data());
                return vec3(buf.data() + rp->GetOffset_Internal(), rsp->GetStruct(), out);
            }
        }
        FProperty* p = o->GetPropertyByNameInChain(STR("Location"));
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        auto* base = sp ? o->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Location")) : nullptr;
        return base && vec3(base, sp->GetStruct(), out);
    }

    // WHAT: address of one axis (X=0, Y=1, Z=2) Value field inside a PVector property, and is64 set to its width. nullptr when the layout is not found.
    // WHY: lets the mod read or write an input axis in place, without a ProcessEvent call; is64 tells the caller which width to use.
    uint8_t* pvec_axis_ptr(UObject* o, const TCHAR* prop, int axis, bool& is64)
    {
        static const TCHAR* names[3] = {STR("X"), STR("Y"), STR("Z")};
        if (!o || axis < 0 || axis > 2) return nullptr;
        FProperty* p = o->GetPropertyByNameInChain(prop);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return nullptr;
        uint8_t* base = o->GetValuePtrByPropertyNameInChain<uint8_t>(prop);
        if (!base) return nullptr;
        FProperty* f = sp->GetStruct()->FindProperty(FName(names[axis], FNAME_Add));
        auto* fsp = f ? CastField<FStructProperty>(f) : nullptr;
        if (!fsp || !fsp->GetStruct()) return nullptr;
        FProperty* v = fsp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
        if (!v) return nullptr;
        if (CastField<FInt64Property>(v)) is64 = true;
        else if (CastField<FIntProperty>(v)) is64 = false;
        else return nullptr;
        return base + f->GetOffset_Internal() + v->GetOffset_Internal();
    }

    // WHAT: sets one raw axis of a PVector property in place. Units: raw.
    // WHY: direct write for input vectors (rot_zero uses it on RotationImpulseInput). The game may overwrite the value on the next tick (brief Update 22: readback 0).
    bool pvec_set_axis(UObject* o, const TCHAR* prop, int axis, int64_t val)
    {
        bool is64 = false;
        uint8_t* d = pvec_axis_ptr(o, prop, axis, is64);
        if (!d) return false;
        if (is64) *reinterpret_cast<int64_t*>(d) = val;
        else *reinterpret_cast<int32_t*>(d) = static_cast<int32_t>(val);
        return true;
    }

    // WHAT: "(x,y,z)" in raw units as whole numbers, or "n/a".
    // WHY: short log form; the fraction is dropped.
    std::wstring pvec_text(UObject* o, const TCHAR* prop)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return STR("n/a");
        uint8_t* base = o->GetValuePtrByPropertyNameInChain<uint8_t>(prop);
        double v[3] = {0, 0, 0};
        if (!base || !vec3(base, sp->GetStruct(), v)) return STR("n/a");
        return STR("(") + std::to_wstring(static_cast<long long>(v[0])) + STR(",") + std::to_wstring(static_cast<long long>(v[1])) +
               STR(",") + std::to_wstring(static_cast<long long>(v[2])) + STR(")");
    }

    // WHAT: reads an IntProperty as int32; false when missing or not an int.
    // WHY: type-checked read, so a wrong property type is never misread.
    bool get_int_prop(UObject* o, const TCHAR* prop, int32_t& out)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        if (!p || !CastField<FIntProperty>(p)) return false;
        auto* pv = o->GetValuePtrByPropertyNameInChain<int32_t>(prop);
        if (!pv) return false;
        out = *pv;
        return true;
    }

    // WHAT: writes an IntProperty; false when missing or not an int.
    // WHY: type-checked write, same reason as get_int_prop.
    bool set_int_prop(UObject* o, const TCHAR* prop, int32_t val)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        if (!p || !CastField<FIntProperty>(p)) return false;
        auto* pv = o->GetValuePtrByPropertyNameInChain<int32_t>(prop);
        if (!pv) return false;
        *pv = val;
        return true;
    }

    // WHAT: reads a bool property through FBoolProperty; false when missing or not a bool.
    // WHY: UE packs bools into bit fields, so the typed reader is needed.
    bool get_bool_prop(UObject* o, const TCHAR* prop, bool& out)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
        if (!bp) return false;
        out = bp->GetPropertyValueInContainer(o);
        return true;
    }

    // WHAT: writes a bool through FBoolProperty; false when not a bool.
    // WHY: a raw byte write would corrupt the other bits of the bit field.
    bool set_bool_prop(UObject* o, const TCHAR* prop, bool val)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
        if (!bp) return false;
        bp->SetPropertyValueInContainer(o, val);
        return true;
    }

    // WHAT: UObject* value of an ObjectProperty, or nullptr.
    // WHY: type-checked read. The caller still runs alive() before using the object.
    UObject* get_obj_prop(UObject* o, const TCHAR* prop)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        if (!p || !CastField<FObjectProperty>(p)) return nullptr;
        auto* pv = o->GetValuePtrByPropertyNameInChain<UObject*>(prop);
        return pv ? *pv : nullptr;
    }

    // WHAT: total Amount of one item in the ship Inventory ItemTotals list; -1 when unreadable. Units: items.
    // WHY: cargo before/after logs and ore amount checks. Walks the raw TArray by the struct stride (Item, Amount), because no header is available.
    int64_t inv_total(UObject* ship, UObject* item)
    {
        if (!ship || !item) return -1;
        FProperty* ip = ship->GetPropertyByNameInChain(STR("Inventory"));
        auto* ob = ip ? CastField<FObjectProperty>(ip) : nullptr;
        auto* ipv = ob ? ship->GetValuePtrByPropertyNameInChain<UObject*>(STR("Inventory")) : nullptr;
        UObject* inv = ipv ? *ipv : nullptr;
        if (!inv) return -1;
        FProperty* tp = inv->GetPropertyByNameInChain(STR("ItemTotals"));
        auto* ap = tp ? CastField<FArrayProperty>(tp) : nullptr;
        auto* arr = ap ? inv->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("ItemTotals")) : nullptr;
        if (!arr) return -1;
        FProperty* inner = ap->GetInner();
        auto* isp = inner ? CastField<FStructProperty>(inner) : nullptr;
        if (!isp || !isp->GetStruct()) return -1;
        FProperty* fi = isp->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add));
        FProperty* fa = isp->GetStruct()->FindProperty(FName(STR("Amount"), FNAME_Add));
        int32_t stride = inner->GetElementSize();
        if (!fi || !fa || stride <= 0) return -1;
        int64_t total = 0;
        for (int32_t i = 0; i < arr->Num(); ++i)
        {
            uint8_t* e = &(*arr)[0] + static_cast<size_t>(i) * static_cast<size_t>(stride);
            if (*reinterpret_cast<UObject**>(e + fi->GetOffset_Internal()) == item) total += *reinterpret_cast<int32_t*>(e + fa->GetOffset_Internal());
        }
        return total;
    }

    // WHAT: calls a getter that returns a PNum struct and reads its Value (int32 or int64, raw). False when the function or layout is missing.
    // WHY: getters like GetMaxSpeed return raw fixed point; ProcessEvent is the only way to call them without a header.
    bool call_pnum(UObject* o, const TCHAR* fname, int64_t& out)
    {
        if (!alive(o)) return false;
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        auto* sp = rp ? CastField<FStructProperty>(rp) : nullptr;
        if (!sp || !sp->GetStruct()) return false;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        FProperty* v = sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
        if (!v) return false;
        const uint8_t* d = buf.data() + rp->GetOffset_Internal() + v->GetOffset_Internal();
        if (CastField<FInt64Property>(v)) out = *reinterpret_cast<const int64_t*>(d);
        else if (CastField<FIntProperty>(v)) out = *reinterpret_cast<const int32_t*>(d);
        else return false;
        return true;
    }

    // WHAT: calls a getter that returns an int32; false when missing.
    // WHY: same ProcessEvent route as call_pnum, for plain int results.
    bool call_int(UObject* o, const TCHAR* fname, int32_t& out)
    {
        if (!alive(o)) return false;
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        if (!rp || !CastField<FIntProperty>(rp)) return false;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        out = *reinterpret_cast<int32_t*>(buf.data() + rp->GetOffset_Internal());
        return true;
    }

    // WHAT: calls a getter that returns a double (e.g. GetShields); false when missing.
    bool call_double(UObject* o, const TCHAR* fname, double& out)
    {
        if (!alive(o)) return false;
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        if (!rp || !CastField<FDoubleProperty>(rp)) return false;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        out = *reinterpret_cast<double*>(buf.data() + rp->GetOffset_Internal());
        return true;
    }

    // WHAT: reads Quat {W,X,Y,Z} (each a PNum Value) into unitless doubles, dividing by 65536. False when missing.
    // WHY: the rotation math needs unit quaternions; the raw values are fixed point.
    bool quat4(UObject* o, double q[4])
    {
        if (!alive(o)) return false;
        FProperty* p = o->GetPropertyByNameInChain(STR("Quat"));
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        uint8_t* base = (sp && sp->GetStruct()) ? o->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Quat")) : nullptr;
        if (!base) return false;
        static const TCHAR* names[4] = {STR("W"), STR("X"), STR("Y"), STR("Z")};
        for (int i = 0; i < 4; ++i)
        {
            FProperty* f = sp->GetStruct()->FindProperty(FName(names[i], FNAME_Add));
            auto* fsp = f ? CastField<FStructProperty>(f) : nullptr;
            if (!fsp || !fsp->GetStruct()) return false;
            FProperty* v = fsp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
            if (!v) return false;
            const uint8_t* d = base + f->GetOffset_Internal() + v->GetOffset_Internal();
            if (CastField<FInt64Property>(v)) q[i] = static_cast<double>(*reinterpret_cast<const int64_t*>(d)) / 65536.0;
            else if (CastField<FIntProperty>(v)) q[i] = static_cast<double>(*reinterpret_cast<const int32_t*>(d)) / 65536.0;
            else return false;
        }
        return true;
    }

    // WHAT: sets the three RotationImpulseInput axes to 0.
    // WHY: part of the old rotation route. The sim overwrote those writes (brief Update 22), and steering now uses ViewInput (Update 28). Still called from mb_steer.hpp.
    void rot_zero(UObject* ship)
    {
        for (int a = 0; a < 3; ++a) pvec_set_axis(ship, STR("RotationImpulseInput"), a, 0);
    }

    // WHAT: raw Value of a PNum struct property as int64; false when missing. Units: raw.
    // WHY: int64 holds both the int32 and the int64 layouts.
    bool pnum_prop(UObject* o, const TCHAR* prop, int64_t& out)
    {
        if (!alive(o)) return false;
        FProperty* p = o->GetPropertyByNameInChain(prop);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return false;
        uint8_t* base = o->GetValuePtrByPropertyNameInChain<uint8_t>(prop);
        FProperty* v = base ? sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add)) : nullptr;
        if (!v) return false;
        const uint8_t* d = base + v->GetOffset_Internal();
        if (CastField<FInt64Property>(v)) out = *reinterpret_cast<const int64_t*>(d);
        else if (CastField<FIntProperty>(v)) out = *reinterpret_cast<const int32_t*>(d);
        else return false;
        return true;
    }

    // Forward declarations of obj_prop and actor_vec_call (defined below). This block declares them twice; harmless, kept because this pass changes no code.
    UObject* obj_prop(UObject* o, const TCHAR* prop);

    bool actor_vec_call(UObject* o, const TCHAR* fname, double out[3], std::wstring& why);

    // (Second copy of the same two declarations, see above.)
    UObject* obj_prop(UObject* o, const TCHAR* prop);

    bool actor_vec_call(UObject* o, const TCHAR* fname, double out[3], std::wstring& why);

    // WHAT: UObject* of an ObjectProperty, or nullptr.
    // WHY: type-checked with prop_is. There is no alive() check here, so the caller guards the result.
    UObject* obj_prop(UObject* o, const TCHAR* prop)
    {
        if (!o || !prop_is(o, prop, STR("ObjectProperty"))) return nullptr;
        auto* pp = o->GetValuePtrByPropertyNameInChain<UObject*>(prop);
        return pp ? *pp : nullptr;
    }

    // FVector-like getter return (X,Y,Z float or double). why says what was missing; false = not usable.
    // WHAT: calls a getter that returns an FVector-like struct (X, Y, Z as float or double) into out. why is set to the reason when it returns false.
    // WHY: actor world positions come from functions such as K2_GetActorLocation, which return floats in UE units (100 uu = 1 m). The caller checks the object first.
    bool actor_vec_call(UObject* o, const TCHAR* fname, double out[3], std::wstring& why)
    {
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        if (!fn) { why = STR("fn missing"); return false; }
        FProperty* rp = fn->GetReturnProperty();
        auto* sp = rp ? CastField<FStructProperty>(rp) : nullptr;
        if (!sp || !sp->GetStruct()) { why = STR("no struct return"); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        const uint8_t* base = buf.data() + rp->GetOffset_Internal();
        static const TCHAR* names[3] = {STR("X"), STR("Y"), STR("Z")};
        for (int i = 0; i < 3; ++i)
        {
            FProperty* f = sp->GetStruct()->FindProperty(FName(names[i], FNAME_Add));
            if (!f) { why = STR("no X/Y/Z field"); return false; }
            const uint8_t* d = base + f->GetOffset_Internal();
            if (CastField<FFloatProperty>(f)) out[i] = *reinterpret_cast<const float*>(d);
            else if (CastField<FDoubleProperty>(f)) out[i] = *reinterpret_cast<const double*>(d);
            else { why = STR("not float/double"); return false; }
        }
        return true;
    }
}
