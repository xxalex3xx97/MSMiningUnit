// FILE: mods/MiningBenchMod/mb_chunkhold.hpp
// WHAT: chunk hold, thin layer over MSCore (API 3, msc_chunks.hpp). While an AI bench ship mines far from the player (more than CH_NEAR_CH
//       chunks, or another system), the 3x3x3 cube of chunks around its chunk stays loaded. The natives, the cube, the cap and the safety
//       checks live in MSCore; this file decides WHEN a ship asks and keeps the per-ship state the mining loop reads.
//       chunk_hold_update(sid, ship)  ask MSCore to set or move the ship's cube (MSCore returns set / unchanged / refused)
//       chunk_hold_release(sid)       drop the ship's cube
//       chunk_hold_tick(sid)          forced rock fallback passes at +1 s and +3 s after a set
//       chunk_hold_settling(sid)      true while the ship's cube was set less than 4 s ago (belt counters wait)
//       chunk_hold_keep_only(sids)    release the holds of ships that are no longer bench ships
//       chunk_hold_release_all()      every hold (Dev Menu "Release all chunk holds", game exit / mod unload)
// WHY: LoadChunks() streams only the player's surroundings (ChunkLoadingDistance 2 = 5^3 around PlayerCachedChunkLocation). Far belt
//      rocks exist only where their chunks are loaded, so the AI ship asks for its own cube (findings part9, tasks/2026-10-29-part9-findings.md).
// RULES (here): never for the ship the player flies; no hold while the ship is within CH_NEAR_CH chunks of the player (same system: the
//        player's own streaming covers it). The cube rules (player zone, shared chunks, cap, canary, natives) are MSCore's.
// DEPENDS ON: mb_common.hpp (log_line, alive, STR), mb_mscore.hpp (ms_load, g_ms), mb_compat.hpp (g_compat), mb_ue.hpp (first_of, obj_id,
//             get_int_prop, get_obj_prop), mb_warp.hpp (sec_axes, sys_id), mb_belt.hpp (ship_pos), mb_rocks.hpp (rock_fallback_request,
//             rock_list_ensure, g_rock_fb_pass_ms), mb_mining.hpp (the forward-declared chunk_hold_* API).
// USED BY: mb_mining.hpp (mine_step, mine_halt, mine_set_bench, belt arrival, CHECK), mb_testcmd.hpp (chunkhold:release_all),
//          dllmain.cpp (destructor -> chunk_hold_release_all), mb_compat_run.hpp (chunk_hold_release_all). dllmain includes it after mb_mining.hpp.
// Log keyword: 'CHUNKHOLD' (every line starts with CHUNKHOLD).
#pragma once

namespace
{
    constexpr int64_t CH_NEAR_CH = 5;                   // no hold when the ship is this many chunks or less from the player (same system)
    constexpr ULONGLONG CH_SETTLE_MS = 4000;            // belt counters wait this long after a set (ms)
    constexpr ULONGLONG CH_FORCE_MS[2] = {1000, 3000};  // forced rock fallback passes after a set (ms)
    constexpr ULONGLONG CH_LOG_EVERY_MS = 10000;        // refusal lines that repeat every tick are logged at most this often (ms)

    // one ship's hold as the mod last set it: solar system, centre chunk, when it was set (GetTickCount64), forced passes done
    struct ChLocal
    {
        int sys = 0;
        int64_t c[3] = {0, 0, 0};
        ULONGLONG set_ms = 0;
        bool forced[2] = {false, false};
    };
    std::map<int, ChLocal> g_ch_local;   // ship id -> its hold
    ULONGLONG g_ch_log_ms = 0;           // last throttled log line

    // WHAT: largest per-axis chunk distance (Chebyshev).
    int64_t chunk_cheb(const int64_t a[3], const int64_t b[3])
    {
        return std::max({std::llabs(a[0] - b[0]), std::llabs(a[1] - b[1]), std::llabs(a[2] - b[2])});
    }

    // WHAT: "(x,y,z)" of a chunk.
    std::wstring ch_c3_text(const int64_t c[3])
    {
        return STR("(") + std::to_wstring(c[0]) + STR(",") + std::to_wstring(c[1]) + STR(",") + std::to_wstring(c[2]) + STR(")");
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
        if (g_ch_log_ms != 0 && now - g_ch_log_ms < CH_LOG_EVERY_MS) return;
        g_ch_log_ms = now;
        log_line(s);
    }
}

// ---- API (declared in mb_mining.hpp, which calls it from mine_step) ----
namespace
{
    // WHAT: chunk the ship's cube is centred on instead of the ship's own (a save-data rock the ship should load).
    struct ChTarget { int sys = 0; int64_t c[3] = {0, 0, 0}; };
    std::map<int, ChTarget> g_ch_target;   // ship id -> target chunk

    void chunk_hold_target(int sid, int sys, const int64_t c[3])
    {
        ChTarget& t = g_ch_target[sid];
        t.sys = sys;
        for (int i = 0; i < 3; ++i) t.c[i] = c[i];
    }

    void chunk_hold_target_clear(int sid) { g_ch_target.erase(sid); }

    // WHAT: asks MSCore to set (or move) the ship's cube. Called every mine_step tick in SEEK/MINE, at the belt arrival, and in CHECK for an undocked ship
    //       with nothing in range. Returns at once when the ship is on the same chunk as its last hold.
    void chunk_hold_update(int sid, UObject* ship)
    {
        if (!g_compat.usable(F_CHUNKHOLD) || !alive(ship)) return;
        if (ch_player_flies(ship)) { chunk_hold_release(sid); return; }
        int sys = 0;
        int64_t sch[3] = {0, 0, 0};
        if (!ship_pos(ship, sys, sch)) return;
        const auto tgt = g_ch_target.find(sid);
        const bool has_tgt = tgt != g_ch_target.end();
        if (has_tgt) { sys = tgt->second.sys; for (int i = 0; i < 3; ++i) sch[i] = tgt->second.c[i]; }
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        if (!ch_player(psys, pch)) return;
        if (!has_tgt && sys == psys && chunk_cheb(sch, pch) <= CH_NEAR_CH) { chunk_hold_release(sid); return; }   // the player's own streaming covers it
        const auto old = g_ch_local.find(sid);
        if (old != g_ch_local.end() && old->second.sys == sys && old->second.c[0] == sch[0] && old->second.c[1] == sch[1] && old->second.c[2] == sch[2]) return;
        std::wstring e;
        if (!ms_load(e)) { ch_log_throttled(STR("CHUNKHOLD MSCore unavailable: ") + e); return; }
        wchar_t err[256] = {0};
        const long long c3[3] = {sch[0], sch[1], sch[2]};
        const int r = g_ms.chunk_hold(sid, sys, c3, err, 256);   // 0 fail (err says why), 1 set, 2 unchanged
        if (r == 0) { ch_log_throttled(STR("CHUNKHOLD refused ship=") + std::to_wstring(sid) + STR(": ") + std::wstring(err)); return; }
        ChLocal& L = g_ch_local[sid];
        const bool changed = r == 1;
        L.sys = sys;
        for (int i = 0; i < 3; ++i) L.c[i] = sch[i];
        if (changed || L.set_ms == 0) { L.set_ms = GetTickCount64(); L.forced[0] = L.forced[1] = false; }
        if (changed) log_line(STR("CHUNKHOLD set ship=") + std::to_wstring(sid) + STR(" sys=") + std::to_wstring(sys) + STR(" centre=") + ch_c3_text(sch));
    }

    // WHAT: drops the ship's cube (MSCore clears the chunks no other ship holds). No-op without a hold.
    void chunk_hold_release(int sid)
    {
        g_ch_target.erase(sid);
        const auto it = g_ch_local.find(sid);
        if (it == g_ch_local.end()) return;
        g_ch_local.erase(it);
        std::wstring e;
        if (ms_load(e)) g_ms.chunk_release(sid);
        log_line(STR("CHUNKHOLD released ship=") + std::to_wstring(sid));
    }

    // WHAT: forced rock passes. 1 s and 3 s after a set the rock list is refreshed at once, so the rocks of the new cube are in the list
    //       before the belt counters start. Called every mine_step tick.
    void chunk_hold_tick(int sid)
    {
        const auto it = g_ch_local.find(sid);
        if (it == g_ch_local.end()) return;
        ChLocal& r = it->second;
        const ULONGLONG age = GetTickCount64() - r.set_ms;
        for (int i = 0; i < 2; ++i)
        {
            if (r.forced[i] || age < CH_FORCE_MS[i]) continue;
            r.forced[i] = true;
            rock_fallback_request();
            g_rock_fb_pass_ms = 0;   // the next rock_list_ensure runs the scan pass now
            rock_list_ensure();
        }
    }

    // WHAT: true while the ship's cube was set less than CH_SETTLE_MS ago. The belt counters (CHECK empty_t, SEEK rng_t) do not count then.
    bool chunk_hold_settling(int sid)
    {
        const auto it = g_ch_local.find(sid);
        return it != g_ch_local.end() && GetTickCount64() - it->second.set_ms < CH_SETTLE_MS;
    }

    // WHAT: releases the holds of every ship not in sids (bench list changed).
    void chunk_hold_keep_only(const std::vector<int>& sids)
    {
        std::vector<int> drop;
        for (const auto& p : g_ch_local) if (std::find(sids.begin(), sids.end(), p.first) == sids.end()) drop.push_back(p.first);
        for (int sid : drop) chunk_hold_release(sid);
    }

    // WHAT: releases every hold. Dev Menu "Release all chunk holds", game exit, mod unload. Safe without MSCore (the holds are only forgotten then).
    void chunk_hold_release_all()
    {
        std::vector<int> sids;
        for (const auto& p : g_ch_local) sids.push_back(p.first);
        for (int sid : sids) chunk_hold_release(sid);
        log_line(STR("CHUNKHOLD release_all ships=") + std::to_wstring(sids.size()));
    }
}
