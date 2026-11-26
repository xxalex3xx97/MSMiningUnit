// FILE: mods/MiningBenchMod/mb_chunkhold.hpp
// WHAT: chunk hold policy. While an AI bench ship mines far from the player (more than CH_NEAR_CH chunks, or another system), the 3x3x3 cube
//       of chunks around its chunk stays loaded. The cube itself is set, moved and cleared by MSCore (MSCore_chunk_hold2 / _release): the mod
//       owns no native calls and no ChunkMap code.
//       chunk_hold_update(sid, ship)  set or move the cube of the ship's chunk (MSCore does the set, the move and the clear)
//       chunk_hold_release(sid)       drop the ship's cube
//       chunk_hold_tick(sid)          forced rock fallback passes at +1 s and +3 s after a set
//       chunk_hold_settling(sid)      true while the ship's cube was set less than 4 s ago (belt counters wait)
//       chunk_hold_keep_only(sids)    release the holds of ships that are no longer bench ships
//       chunk_hold_release_all()      every hold (Dev Menu "Release all chunk holds", game exit / mod unload)
// WHY: LoadChunks() streams only the player's surroundings (ChunkLoadingDistance 2 = 5^3 around PlayerCachedChunkLocation). Far belt
//      rocks exist only where their chunks are loaded, so the AI ship asks for its own cube (findings part9, tasks/2026-10-29-part9-findings.md).
// RULES (mod side): never for the ship the player flies; no hold while the ship is within CH_PLAYER_ZONE chunks of the player (same system:
//        the player's own streaming covers it); a ship up to CH_NEAR_CH chunks out holds only its own chunk (own_only).
// MSCORE SIDE: the owner id is CH_OWNER_BASE + ship id. MSCore keeps the player's zone, the chunk cap, the native checks, the SEH guards and
//        the canary, and logs the CHUNKHOLD set line. Results: 0 fail (MSCore's err text decides: "cap:" releases the ship's cube,
//        "disabled for this session" or "native" turns the feature off, anything else is logged throttled), 1 set, 2 unchanged.
// COMPAT: MSCore older than 0.8.7 (no chunk hold exports): ms_chunk_ok false, one CHUNKHOLD off line, feature off, mining runs.
// Log keyword: 'chunkhold' (every line starts with CHUNKHOLD).
// DEPENDS ON: mb_common.hpp (log_line, alive, STR), mb_mscore.hpp (g_ms, ms_chunk_ok), mb_compat.hpp (g_compat), mb_ue.hpp (first_of,
//             get_obj_prop, get_int_prop, obj_id, cached_ok), mb_warp.hpp (sec_axes, sys_id), mb_belt.hpp (ship_pos),
//             mb_rocks.hpp (rock_fallback_request, rock_list_ensure, g_rock_fb_pass_ms), mb_mining.hpp (the forward-declared chunk_hold_* API).
// USED BY: mb_mining.hpp (mine_step, mine_halt, mine_set_bench, belt arrival, CHECK), mb_testcmd.hpp (chunkhold:release_all),
//          dllmain.cpp (destructor -> chunk_hold_release_all). dllmain includes it after mb_mining.hpp.
#pragma once

namespace
{
    constexpr int64_t CH_NEAR_CH = 5;                   // ship this many chunks or less from the player (outside CH_PLAYER_ZONE): hold only its own chunk, beyond: the cube
    constexpr int64_t CH_PLAYER_ZONE = 2;               // the player's own loaded set: PlayerCachedChunkLocation +- this (ChunkLoadingDistance)
    constexpr ULONGLONG CH_SETTLE_MS = 4000;            // belt counters wait this long after a set (ms)
    constexpr ULONGLONG CH_FORCE_MS[2] = {1000, 3000};  // forced rock fallback passes after a set (ms)
    constexpr ULONGLONG CH_LOG_EVERY_MS = 10000;        // refusal lines that repeat every tick are logged at most this often (ms)
    constexpr int CH_OWNER_BASE = 1000000000;           // MSCore owner id of a ship's cube = CH_OWNER_BASE + ship id

    // one ship's cube: the chunk it is centred on, its system, when it was set (GetTickCount64), forced passes done, own chunk only
    struct ChRec
    {
        int sys = 0;
        int32_t c[3] = {0, 0, 0};
        ULONGLONG set_ms = 0;
        bool forced[2] = {false, false};
        bool one = false;   // only the ship's own chunk (actor read), not the 27-chunk cube
    };

    struct ChState
    {
        bool disabled = false;        // MSCore refused for the rest of the session: no holds
        bool warned = false;          // the "MSCore too old" line was logged
        std::map<int, ChRec> ships;   // ship id -> its cube (as set by MSCore)
        ULONGLONG log_ms = 0;         // last throttled log line
    };
    ChState g_ch;

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

    // WHAT: logs a refusal line, at most one per CH_LOG_EVERY_MS (refusals repeat every tick).
    void ch_log_throttled(const std::wstring& s)
    {
        const ULONGLONG now = GetTickCount64();
        if (g_ch.log_ms != 0 && now - g_ch.log_ms < CH_LOG_EVERY_MS) return;
        g_ch.log_ms = now;
        log_line(s);
    }
}

// ---- API (declared in mb_mining.hpp, which calls it from mine_step) ----
namespace
{
    // WHAT: sets (or moves) the ship's cube through MSCore. Called every mine_step tick in SEEK/MINE, at the belt arrival, and in CHECK for an
    //       undocked ship with nothing in range. MSCore returns at once when the cube is already there (2 = unchanged).
    // WHY: the cube, its player-zone rule, the chunk cap and the native safety live in MSCore (one copy for every mod).
    struct ChTarget { int sys = 0; int64_t c[3] = {0, 0, 0}; };
    std::map<int, ChTarget> g_ch_target;   // ship id -> chunk the cube is centred on instead of the ship's own (a save-data rock the ship should load)

    void chunk_hold_target(int sid, int sys, const int64_t c[3])
    {
        ChTarget& t = g_ch_target[sid];
        t.sys = sys;
        for (int i = 0; i < 3; ++i) t.c[i] = c[i];
    }

    void chunk_hold_target_clear(int sid) { g_ch_target.erase(sid); }

    void chunk_hold_update(int sid, UObject* ship, bool own_only)
    {
        const std::wstring sd = std::to_wstring(sid);
        if (g_ch.disabled || !g_compat.usable(F_CHUNKHOLD) || !alive(ship)) { ch_log_throttled(STR("CHUNKHOLD skip ship=") + sd + STR(": disabled/unusable/dead")); return; }
        if (ch_player_flies(ship)) { chunk_hold_release(sid); ch_log_throttled(STR("CHUNKHOLD skip ship=") + sd + STR(": player flies it")); return; }
        int sys = 0;
        int64_t sch[3] = {0, 0, 0};
        if (!ship_pos(ship, sys, sch)) { ch_log_throttled(STR("CHUNKHOLD skip ship=") + sd + STR(": ship_pos unreadable sys=") + std::to_wstring(sys)); return; }
        const auto tgt = g_ch_target.find(sid);
        const bool has_tgt = tgt != g_ch_target.end();
        if (has_tgt) { sys = tgt->second.sys; for (int i = 0; i < 3; ++i) sch[i] = tgt->second.c[i]; }
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        if (!ch_player(psys, pch)) { ch_log_throttled(STR("CHUNKHOLD skip ship=") + sd + STR(": player chunk unreadable")); return; }
        // inside the player's zone: its own streaming covers the ship. Up to CH_NEAR_CH chunks out: hold only the ship's own chunk (actor read), not the cube.
        if (!has_tgt && sys == psys && chunk_cheb(sch, pch) > CH_PLAYER_ZONE && chunk_cheb(sch, pch) <= CH_NEAR_CH) own_only = true;
        if (!has_tgt && sys == psys && chunk_cheb(sch, pch) <= CH_PLAYER_ZONE)
        {
            chunk_hold_release(sid);
            return;
        }
        std::wstring why;
        if (!ms_chunk_ok(why))
        {
            if (!g_ch.warned) { g_ch.warned = true; log_line(STR("CHUNKHOLD off: ") + why); }
            return;
        }
        const int32_t c[3] = {static_cast<int32_t>(sch[0]), static_cast<int32_t>(sch[1]), static_cast<int32_t>(sch[2])};
        const auto old = g_ch.ships.find(sid);
        if (old != g_ch.ships.end() && old->second.sys == sys && old->second.c[0] == c[0] && old->second.c[1] == c[1] && old->second.c[2] == c[2] && old->second.one == own_only) return;
        const long long c3[3] = {sch[0], sch[1], sch[2]};
        wchar_t e[256] = {0};
        const int r = g_ms.chunk_hold2(CH_OWNER_BASE + sid, sys, c3, own_only ? 0 : 1, e, 256);
        const std::wstring err = e;
        if (r == 0)
        {
            if (err.rfind(L"cap:", 0) == 0) { chunk_hold_release(sid); return; }
            if (err.find(L"disabled for this session") != std::wstring::npos || err.find(L"native") != std::wstring::npos)
            {
                g_ch.disabled = true;
                g_ch.ships.clear();
                log_line(STR("CHUNKHOLD disabled: ") + err);
                return;
            }
            ch_log_throttled(STR("CHUNKHOLD ") + err);
            return;
        }
        // 1 = set (or moved), 2 = unchanged: keep the record; a set starts the settle timer and the forced passes
        ChRec rec;
        rec.sys = sys;
        rec.one = own_only;
        for (int i = 0; i < 3; ++i) rec.c[i] = c[i];
        if (r == 1) rec.set_ms = GetTickCount64();
        else if (old != g_ch.ships.end())
        {
            rec.set_ms = old->second.set_ms;   // unchanged: the settle timer and the passes of the earlier set stand
            rec.forced[0] = old->second.forced[0];
            rec.forced[1] = old->second.forced[1];
        }
        else rec.forced[0] = rec.forced[1] = true;   // unchanged but not known here: no passes
        g_ch.ships[sid] = rec;
    }

    // WHAT: drops the ship's cube (MSCore clears its chunks no other ship holds). No-op without a cube.
    void chunk_hold_release(int sid)
    {
        g_ch_target.erase(sid);
        const auto it = g_ch.ships.find(sid);
        if (it == g_ch.ships.end()) return;
        const ChRec rec = it->second;
        g_ch.ships.erase(it);
        const int ok = g_ms.chunk_release ? g_ms.chunk_release(CH_OWNER_BASE + sid) : 0;
        log_line(STR("CHUNKHOLD released ship=") + std::to_wstring(sid) + STR(" sys=") + std::to_wstring(rec.sys) + STR(" centre=") + ch_key_text(rec.c) +
                 STR(" msc_ok=") + std::to_wstring(ok));
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

    // WHAT: releases every hold of this mod. Dev Menu "Release all chunk holds", game exit, mod unload. MSCore releases only the owners given here.
    void chunk_hold_release_all()
    {
        std::vector<int> sids;
        for (const auto& p : g_ch.ships) sids.push_back(p.first);
        for (int sid : sids) chunk_hold_release(sid);
        log_line(STR("CHUNKHOLD release_all ships=") + std::to_wstring(sids.size()));
    }
}
