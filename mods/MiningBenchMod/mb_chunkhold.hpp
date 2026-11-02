// FILE: mods/MiningBenchMod/mb_chunkhold.hpp
// WHAT: chunk hold. While an AI bench ship mines far from the player (more than CH_NEAR_CH chunks, or another system), the 3x3x3 cube
//       of chunks around its chunk stays loaded: SetLevel(S,&key,1) + Apply(S,&key) per chunk (native, the calls of the chunkload
//       experiment 9de44a8: 0 -> 316 rocks within 1 s, stable, no crash).
//       chunk_hold_update(sid, ship)  set or move the cube of the ship's chunk (new cube first, then the old chunks no ship needs are cleared)
//       chunk_hold_release(sid)       drop the ship's cube
//       chunk_hold_tick(sid)          forced rock fallback passes at +1 s and +3 s after a set
//       chunk_hold_settling(sid)      true while the ship's cube was set less than 4 s ago (belt counters wait)
//       chunk_hold_keep_only(sids)    release the holds of ships that are no longer bench ships
//       chunk_hold_release_all()      every hold (Dev Menu "Release all chunk holds", game exit / mod unload)
// WHY: LoadChunks() streams only the player's surroundings (ChunkLoadingDistance 2 = 5^3 around PlayerCachedChunkLocation). Far belt
//      rocks exist only where their chunks are loaded, so the AI ship asks for its own cube (findings part9, tasks/2026-10-29-part9-findings.md).
// RULES: never for the ship the player flies; no hold while the ship is within CH_NEAR_CH chunks of the player (same system: the player's
//        own streaming covers it); the player's own loaded set (player chunk +- CH_PLAYER_ZONE) is never set or cleared (deferred instead);
//        a chunk held by several ships is cleared when the last one drops it; at most CH_MAX_CHUNKS held chunks; only chunks that have a
//        Chunk object are set (the tested behaviour, the rest is counted as no_object in the log).
// SAFETY: game thread only (mine_step via mining_tick). The natives are found by byte pattern (mb_natives.hpp, nat_resolve: exactly one
//         match each, else the feature is off) and ch_native_check (get_system(PlayerObject) == FindAllOf SolarSystem) runs once per session
//         before the first set. Every native call is inside a ch_seh_* helper (SEH). A native failure -> ch_disable: level 0 on every held chunk
//         outside the player zone, feature off for the session, CHUNKHOLD log line. The first set is verified by a canary (chunk object still
//         there after a re-read of the map). The controller is never written. bForceLoadChunks is never written.
// DEPENDS ON: mb_common.hpp (log_line, alive, STR, Windows.h), mb_natives.hpp (g_nat, nat_resolve), mb_compat.hpp (g_compat), mb_ue.hpp (first_of, find_by_id, get_obj_prop), mb_warp.hpp (sec_axes, sys_id),
//             mb_belt.hpp (ship_pos), mb_rocks.hpp (rock_fallback_request, rock_list_ensure, g_rock_fb_pass_ms), mb_mining.hpp (player_flies,
//             and the forward-declared chunk_hold_* API).
// USED BY: mb_mining.hpp (mine_step, mine_halt, mine_set_bench, belt arrival, CHECK), mb_testcmd.hpp (chunkhold:release_all),
//          dllmain.cpp (destructor -> chunk_hold_release_all). dllmain includes it after mb_mining.hpp.
// Log keyword: 'chunkhold' (every line starts with CHUNKHOLD).
#pragma once

namespace
{
    constexpr int64_t CH_NEAR_CH = 5;                   // no hold when the ship is this many chunks or less from the player (same system)
    constexpr int64_t CH_PLAYER_ZONE = 2;               // the player's own loaded set: PlayerCachedChunkLocation +- this (ChunkLoadingDistance)
    constexpr size_t CH_MAX_CHUNKS = 150;               // cap on distinct held chunks (all ships together)
    constexpr ULONGLONG CH_SETTLE_MS = 4000;            // belt counters wait this long after a set (ms)
    constexpr ULONGLONG CH_FORCE_MS[2] = {1000, 3000};  // forced rock fallback passes after a set (ms)
    constexpr ULONGLONG CH_LOG_EVERY_MS = 10000;        // refusal lines that repeat every tick are logged at most this often (ms)
    constexpr size_t CH_KEY_SIZE = 12;

    // one held chunk: solar system id + chunk key (the ChunkMap key, 3 x int32)
    struct ChKey
    {
        int sys = 0;
        int32_t k[3] = {0, 0, 0};
    };
    bool operator<(const ChKey& a, const ChKey& b)
    {
        if (a.sys != b.sys) return a.sys < b.sys;
        for (int i = 0; i < 3; ++i) if (a.k[i] != b.k[i]) return a.k[i] < b.k[i];
        return false;
    }
    bool operator==(const ChKey& a, const ChKey& b) { return !(a < b) && !(b < a); }

    // one ship's cube: the chunks it references, its centre, and when it was set (GetTickCount64)
    struct ChRec
    {
        int sys = 0;
        int32_t c[3] = {0, 0, 0};
        std::vector<ChKey> keys;
        ULONGLONG set_ms = 0;
        bool forced[2] = {false, false};
    };

    struct ChState
    {
        bool disabled = false;        // native failure or check failed: no holds for the rest of the session
        bool checked = false;         // exe + native check passed (once, before the first set)
        std::map<ChKey, int> refs;    // held chunk -> number of ships holding it (level 1 applied by us)
        std::set<ChKey> deferred;     // chunks to clear later (player zone, or the System object not found)
        std::map<int, ChRec> ships;   // ship id -> its cube
        ULONGLONG log_ms = 0;         // last throttled log line
        bool canary_done = false;     // the first successful set was re-read (ok or disabled)
    };
    ChState g_ch;

    // ChunkMap pair: key and Chunk object (null when dead)
    struct ChEntry { int32_t k[3] = {0, 0, 0}; UObject* o = nullptr; };

    // ---- SEH helpers: POD arguments only (MSVC forbids __try in functions with C++ objects) ----

    // WHAT: copies n bytes from src to dst. false when the read faults.
    bool ch_seh_copy(const void* src, void* dst, size_t n)
    {
        __try { memcpy(dst, src, n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: native get_system(P) -> out. false when the call faults.
    bool ch_seh_getsys(const void* fn, void* P, void*& out)
    {
        __try { out = reinterpret_cast<void* (*)(void*)>(const_cast<void*>(fn))(P); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: native set_level(S, &key, level). false when the call faults.
    bool ch_seh_setlevel(const void* fn, void* S, const int32_t* key, int32_t level)
    {
        __try { reinterpret_cast<void (*)(void*, const int32_t*, int32_t)>(const_cast<void*>(fn))(S, key, level); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: native apply(S, &key). false when the call faults.
    bool ch_seh_apply(const void* fn, void* S, const int32_t* key)
    {
        __try { reinterpret_cast<void (*)(void*, const int32_t*)>(const_cast<void*>(fn))(S, key); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ---- helpers ----

    // WHAT: "0x<hex>".
    std::wstring ch_hex(uint64_t v) { wchar_t b[32]; swprintf(b, 32, L"0x%llx", static_cast<unsigned long long>(v)); return b; }

    // WHAT: "(x,y,z)" of a chunk key.
    std::wstring ch_key_text(const int32_t k[3])
    {
        return STR("(") + std::to_wstring(k[0]) + STR(",") + std::to_wstring(k[1]) + STR(",") + std::to_wstring(k[2]) + STR(")");
    }

    // WHAT: largest per-axis chunk distance (Chebyshev).
    int64_t chunk_cheb(const int64_t a[3], const int64_t b[3])
    {
        return std::max({std::llabs(a[0] - b[0]), std::llabs(a[1] - b[1]), std::llabs(a[2] - b[2])});
    }

    // WHAT: get_system(PlayerObject) must be the FindAllOf SolarSystem object of the player's system (the object the set/apply calls get).
    bool ch_native_check(UObject* ctrl, int psys)
    {
        UObject* pobj = get_obj_prop(ctrl, STR("PlayerObject"));
        UObject* ref = psys > 0 ? find_by_id(STR("SolarSystem"), psys) : nullptr;
        if (!alive(pobj) || !alive(ref)) { log_line(STR("CHUNKHOLD native check: player object or system object missing")); return false; }
        void* got = nullptr;
        const bool called = ch_seh_getsys(g_nat.system, pobj, got);
        const bool same = called && got == static_cast<void*>(ref);
        log_line(STR("CHUNKHOLD native check get_system(PlayerObject)=") + ch_hex(reinterpret_cast<uint64_t>(got)) +
                 STR(" FindAllOf=") + ch_hex(reinterpret_cast<uint64_t>(ref)) + STR(" ") + (same ? STR("same") : STR("DIFFERENT")));
        return same;
    }

    // WHAT: all pairs of S.ChunkMap (key = 12 bytes at pair+0, value = Chunk* at pair+ValueOffset). info gets the layout or the error.
    // WHY: FMapProperty + FScriptMap from UE4SS (no game SDK). Pointers are read inside SEH; dead objects are kept with o = null.
    bool ch_map_read(UObject* S, std::vector<ChEntry>& out, std::wstring& info)
    {
        out.clear();
        FProperty* p = alive(S) ? S->GetPropertyByNameInChain(STR("ChunkMap")) : nullptr;
        auto* mp = p ? CastField<FMapProperty>(p) : nullptr;
        if (!mp) { info = STR("ChunkMap is not a map property"); return false; }
        FProperty* kp = mp->GetKeyProp();
        FProperty* vp = mp->GetValueProp();
        if (!kp || !vp || kp->GetElementSize() != CH_KEY_SIZE) { info = STR("ChunkMap key size is not 12"); return false; }
        auto* sm = reinterpret_cast<FScriptMap*>(reinterpret_cast<uint8_t*>(S) + p->GetOffset_Internal());
        FScriptMapLayout& lay = mp->GetMapLayout();
        for (int32_t i = 0; i < sm->GetMaxIndex(); ++i)
        {
            if (!sm->IsValidIndex(i)) continue;
            const uint8_t* pair = static_cast<const uint8_t*>(sm->GetData(i, lay));
            ChEntry e;
            if (!pair || !ch_seh_copy(pair, e.k, CH_KEY_SIZE)) continue;
            UObject* v = nullptr;
            if (!ch_seh_copy(pair + lay.ValueOffset, &v, sizeof(v))) continue;
            e.o = alive(v) ? v : nullptr;
            out.push_back(e);
        }
        return true;
    }

    // WHAT: the Chunk object of key k in v, or null (also null when the object is dead).
    UObject* ch_find(const std::vector<ChEntry>& v, const int32_t k[3])
    {
        for (const ChEntry& e : v) if (e.k[0] == k[0] && e.k[1] == k[1] && e.k[2] == k[2]) return e.o;
        return nullptr;
    }

    // WHAT: SetLevel(S,&k,level) then Apply(S,&k), both inside SEH. false when S is gone or either call faults.
    bool ch_native(UObject* S, const int32_t k[3], int32_t level)
    {
        if (!alive(S) || !g_nat.setlevel || !g_nat.apply) return false;
        if (!ch_seh_setlevel(g_nat.setlevel, S, k, level)) return false;
        return ch_seh_apply(g_nat.apply, S, k);
    }

    // WHAT: the player controller. Cached: first_of is a full UObject walk and this runs every tick of a seeking ship.
    // WHY: the cache is checked with cached_ok (SEH), so a freed controller is looked up again.
    UObject* ch_ctrl()
    {
        static UObject* c = nullptr;
        static int cid = 0;
        if (c && cached_ok(c, cid)) return c;
        c = first_of(STR("DMSPlayerController"));
        cid = alive(c) ? obj_id(c) : 0;
        return c;
    }

    // WHAT: true when the player pilots the ship (same test as player_flies, with the cached controller).
    bool ch_player_flies(UObject* ship)
    {
        UObject* ctrl = ch_ctrl();
        int32_t p = -1;
        return alive(ctrl) && get_int_prop(ship, STR("PilotID"), p) && p == obj_id(ctrl);
    }

    // WHAT: player chunk (PlayerCachedChunkLocation) and the player's solar system. false when unreadable: then nothing is set or cleared.
    bool ch_player(int& psys, int64_t pch[3])
    {
        UObject* ctrl = ch_ctrl();
        if (!alive(ctrl) || !sec_axes(ctrl, STR("PlayerCachedChunkLocation"), pch)) return false;
        UObject* pobj = get_obj_prop(ctrl, STR("PlayerObject"));
        psys = alive(pobj) ? sys_id(pobj) : 0;
        return psys > 0;
    }

    // WHAT: true when key lies in the player's own loaded set: same system, every axis within CH_PLAYER_ZONE of the player chunk.
    bool ch_in_zone(const ChKey& key, int psys, const int64_t pch[3])
    {
        if (key.sys != psys) return false;
        for (int i = 0; i < 3; ++i) if (std::llabs(static_cast<int64_t>(key.k[i]) - pch[i]) > CH_PLAYER_ZONE) return false;
        return true;
    }

    // WHAT: logs a refusal line, at most one per CH_LOG_EVERY_MS (refusals repeat every tick).
    void ch_log_throttled(const std::wstring& s)
    {
        const ULONGLONG now = GetTickCount64();
        if (g_ch.log_ms != 0 && now - g_ch.log_ms < CH_LOG_EVERY_MS) return;
        g_ch.log_ms = now;
        log_line(s);
    }

    // WHAT: native failure: level 0 on every held chunk outside the player zone (zone chunks are kept as deferred), then the feature is off.
    void ch_disable(const std::wstring& why)
    {
        log_line(STR("CHUNKHOLD disabled for this session: ") + why + STR(" (held chunks released, feature off)"));
        g_ch.disabled = true;
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        const bool have = ch_player(psys, pch);
        std::set<ChKey> all;
        for (const auto& p : g_ch.refs) all.insert(p.first);
        for (const ChKey& key : g_ch.deferred) all.insert(key);
        for (const ChKey& key : all)
        {
            if (!have || ch_in_zone(key, psys, pch)) continue;   // never touch the player's zone
            ch_native(find_by_id(STR("SolarSystem"), key.sys), key.k, 0);   // result ignored: already failing
        }
        g_ch.refs.clear();
        g_ch.deferred.clear();
        g_ch.ships.clear();
    }

    // WHAT: drops one ship's references. A chunk no ship holds is cleared (level 0 + apply), unless it is in the player zone or its System
    //       object is gone: then it is deferred (ch_drain retries). false = a native call failed (feature disabled).
    bool ch_drop(const ChRec& r, int& cleared, int& shared)
    {
        for (const ChKey& key : r.keys)
        {
            auto ref = g_ch.refs.find(key);
            if (ref == g_ch.refs.end()) continue;
            if (--ref->second > 0) { ++shared; continue; }
            g_ch.refs.erase(ref);
            int psys = 0;
            int64_t pch[3] = {0, 0, 0};
            const bool have = ch_player(psys, pch);
            UObject* S = find_by_id(STR("SolarSystem"), key.sys);
            if (!have || ch_in_zone(key, psys, pch) || !alive(S)) { g_ch.deferred.insert(key); continue; }
            if (!ch_native(S, key.k, 0)) { ch_disable(STR("level 0 failed at chunk ") + ch_key_text(key.k)); return false; }
            ++cleared;
        }
        return true;
    }

    // WHAT: retries the deferred clears: a chunk nobody holds, outside the player zone, gets level 0 + apply.
    void ch_drain()
    {
        if (g_ch.deferred.empty()) return;
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        if (!ch_player(psys, pch)) return;
        for (auto it = g_ch.deferred.begin(); it != g_ch.deferred.end();)
        {
            if (g_ch.refs.count(*it)) { it = g_ch.deferred.erase(it); continue; }
            if (ch_in_zone(*it, psys, pch)) { ++it; continue; }
            UObject* S = find_by_id(STR("SolarSystem"), it->sys);
            if (!alive(S)) { ++it; continue; }
            if (!ch_native(S, it->k, 0)) { ch_disable(STR("level 0 failed on deferred chunk ") + ch_key_text(it->k)); return; }
            it = g_ch.deferred.erase(it);
        }
    }

    // WHAT: exe check and native check, once per session, before the first set. false = not ready (retried next call) or disabled.
    bool ch_ready()
    {
        if (g_ch.disabled) return false;
        if (g_ch.checked) return true;
        UObject* ctrl = ch_ctrl();
        UObject* pobj = alive(ctrl) ? get_obj_prop(ctrl, STR("PlayerObject")) : nullptr;
        const int psys = alive(pobj) ? sys_id(pobj) : 0;
        if (psys <= 0) return false;
        g_ch.checked = true;
        if (!g_nat.resolved && !nat_resolve())
        {
            ch_disable(STR("native pattern: ") + g_nat.why);
            g_compat.add(F_CHUNKHOLD, "natives", false, std::string(g_nat.why.begin(), g_nat.why.end()));
            return false;
        }
        if (!ch_native_check(ctrl, psys)) { ch_disable(STR("native check failed")); return false; }
        return true;
    }
}

// ---- API (declared in mb_mining.hpp, which calls it from mine_step) ----
namespace
{
    // WHAT: sets (or moves) the ship's cube. Called every mine_step tick in SEEK/MINE, at the belt arrival, and in CHECK for an undocked ship
    //       with nothing in range. Returns at once when the ship is on the same chunk as its cube.
    // WHY: the new cube is set before the old one is cleared, so chunks both cubes use never unload.
    void chunk_hold_update(int sid, UObject* ship)
    {
        if (g_ch.disabled || !g_compat.usable(F_CHUNKHOLD) || !alive(ship)) return;
        if (ch_player_flies(ship)) { chunk_hold_release(sid); return; }
        int sys = 0;
        int64_t sch[3] = {0, 0, 0};
        if (!ship_pos(ship, sys, sch)) return;
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        if (!ch_player(psys, pch)) return;
        ch_drain();
        if (sys == psys && chunk_cheb(sch, pch) <= CH_NEAR_CH) { chunk_hold_release(sid); return; }   // the player's own streaming covers it
        if (!ch_ready()) return;
        const int32_t c[3] = {static_cast<int32_t>(sch[0]), static_cast<int32_t>(sch[1]), static_cast<int32_t>(sch[2])};
        const auto old = g_ch.ships.find(sid);
        if (old != g_ch.ships.end() && old->second.sys == sys && old->second.c[0] == c[0] && old->second.c[1] == c[1] && old->second.c[2] == c[2]) return;
        UObject* S = find_by_id(STR("SolarSystem"), sys);
        if (!alive(S)) { ch_log_throttled(STR("CHUNKHOLD no SolarSystem object for sys=") + std::to_wstring(sys) + STR(" ship=") + std::to_wstring(sid)); return; }
        std::vector<ChEntry> v;
        std::wstring info;
        if (!ch_map_read(S, v, info)) { ch_log_throttled(STR("CHUNKHOLD ChunkMap unreadable: ") + info); return; }
        // the cube: 27 chunks, only those with a Chunk object; the player's own zone is never set
        std::vector<ChKey> want;
        int no_obj = 0, zone = 0;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    ChKey key;
                    key.sys = sys;
                    key.k[0] = c[0] + dx; key.k[1] = c[1] + dy; key.k[2] = c[2] + dz;
                    if (ch_in_zone(key, psys, pch)) { ++zone; continue; }
                    if (!ch_find(v, key.k)) { ++no_obj; continue; }
                    want.push_back(key);
                }
        // cap: distinct held chunks after this move (the old cube's chunks that only it holds are freed first)
        const ChRec* prev = old != g_ch.ships.end() ? &old->second : nullptr;
        size_t need = 0, freed = 0;
        for (const ChKey& key : want) if (!g_ch.refs.count(key)) ++need;
        if (prev)
            for (const ChKey& key : prev->keys)
            {
                const auto r = g_ch.refs.find(key);
                if (r != g_ch.refs.end() && r->second == 1 && std::find(want.begin(), want.end(), key) == want.end()) ++freed;
            }
        if (g_ch.refs.size() + need - freed > CH_MAX_CHUNKS)
        {
            ch_log_throttled(STR("CHUNKHOLD cap: ") + std::to_wstring(g_ch.refs.size()) + STR(" held, ") + std::to_wstring(need) + STR(" new chunks needed for ship=") +
                             std::to_wstring(sid) + STR(", max ") + std::to_wstring(CH_MAX_CHUNKS) + STR(", no hold"));
            chunk_hold_release(sid);
            return;
        }
        // set the new cube first (a chunk another ship already holds only gets one more reference)
        ChRec rec;
        rec.sys = sys;
        for (int i = 0; i < 3; ++i) rec.c[i] = c[i];
        int set_n = 0, shared = 0;
        for (const ChKey& key : want)
        {
            rec.keys.push_back(key);
            auto ref = g_ch.refs.find(key);
            if (ref != g_ch.refs.end()) { ++ref->second; ++shared; continue; }
            g_ch.refs[key] = 1;   // tracked before the call: ch_disable clears it when the call fails
            if (!ch_native(S, key.k, 1)) { ch_disable(STR("set level 1 failed at chunk ") + ch_key_text(key.k)); return; }
            ++set_n;
            if (!g_ch.canary_done)
            {
                // canary: the first set must show up in a fresh read of the map (the chunk object is still alive)
                g_ch.canary_done = true;
                std::vector<ChEntry> v2;
                std::wstring info2;
                if (!ch_map_read(S, v2, info2)) { ch_disable(STR("canary: ") + info2); return; }
                if (!ch_find(v2, key.k)) { ch_disable(STR("canary: chunk object not alive after set at ") + ch_key_text(key.k)); return; }
            }
        }
        // then the old cube: the chunks the new one does not use are cleared
        int cleared = 0, kept = 0;
        std::wstring from;
        if (prev)
        {
            const ChRec old_rec = *prev;   // copy: g_ch.ships changes below
            from = STR(" from=") + ch_key_text(old_rec.c);
            g_ch.ships.erase(sid);
            if (!ch_drop(old_rec, cleared, kept)) return;
        }
        rec.set_ms = GetTickCount64();
        g_ch.ships[sid] = rec;
        log_line(STR("CHUNKHOLD set ship=") + std::to_wstring(sid) + STR(" sys=") + std::to_wstring(sys) + STR(" centre=") + ch_key_text(c) + from +
                 STR(" cube=") + std::to_wstring(want.size()) + STR(" new=") + std::to_wstring(set_n) + STR(" shared=") + std::to_wstring(shared) +
                 STR(" no_object=") + std::to_wstring(no_obj) + STR(" player_zone=") + std::to_wstring(zone) + STR(" cleared=") + std::to_wstring(cleared) +
                 STR(" held=") + std::to_wstring(g_ch.refs.size()) + STR(" deferred=") + std::to_wstring(g_ch.deferred.size()));
    }

    // WHAT: drops the ship's cube (its chunks no other ship holds are cleared). No-op without a cube.
    void chunk_hold_release(int sid)
    {
        const auto it = g_ch.ships.find(sid);
        if (it == g_ch.ships.end()) return;
        const ChRec rec = it->second;
        g_ch.ships.erase(it);
        int cleared = 0, kept = 0;
        if (!ch_drop(rec, cleared, kept)) return;
        ch_drain();
        log_line(STR("CHUNKHOLD released ship=") + std::to_wstring(sid) + STR(" sys=") + std::to_wstring(rec.sys) + STR(" centre=") + ch_key_text(rec.c) +
                 STR(" cleared=") + std::to_wstring(cleared) + STR(" shared_kept=") + std::to_wstring(kept) + STR(" held=") + std::to_wstring(g_ch.refs.size()) +
                 STR(" deferred=") + std::to_wstring(g_ch.deferred.size()));
    }

    // WHAT: forced rock passes. 1 s and 3 s after a set the rock list is refreshed at once (FindAllOf over the fallback class), so the rocks
    //       of the new cube are in the list before the belt counters start. Called every mine_step tick.
    void chunk_hold_tick(int sid)
    {
        const auto it = g_ch.ships.find(sid);
        if (it == g_ch.ships.end()) return;
        ChRec& r = it->second;
        const ULONGLONG age = GetTickCount64() - r.set_ms;
        for (int i = 0; i < 2; ++i)
        {
            if (r.forced[i] || age < CH_FORCE_MS[i]) continue;
            r.forced[i] = true;
            rock_fallback_request();
            g_rock_fb_pass_ms = 0;   // the next rock_list_ensure runs the FindAllOf pass now
            rock_list_ensure();
        }
    }

    // WHAT: true while the ship's cube was set less than CH_SETTLE_MS ago. The belt counters (CHECK empty_t, SEEK rng_t) do not count then.
    bool chunk_hold_settling(int sid)
    {
        const auto it = g_ch.ships.find(sid);
        return it != g_ch.ships.end() && GetTickCount64() - it->second.set_ms < CH_SETTLE_MS;
    }

    // WHAT: releases the holds of every ship not in sids (bench list changed).
    void chunk_hold_keep_only(const std::vector<int>& sids)
    {
        std::vector<int> drop;
        for (const auto& p : g_ch.ships) if (std::find(sids.begin(), sids.end(), p.first) == sids.end()) drop.push_back(p.first);
        for (int sid : drop) chunk_hold_release(sid);
    }

    // WHAT: releases every hold, then retries the deferred clears. Dev Menu "Release all chunk holds", game exit, mod unload.
    void chunk_hold_release_all()
    {
        std::vector<int> sids;
        for (const auto& p : g_ch.ships) sids.push_back(p.first);
        for (int sid : sids) chunk_hold_release(sid);
        ch_drain();
        log_line(STR("CHUNKHOLD release_all ships=") + std::to_wstring(sids.size()) + STR(" held=") + std::to_wstring(g_ch.refs.size()) +
                 STR(" deferred=") + std::to_wstring(g_ch.deferred.size()));
    }
}
