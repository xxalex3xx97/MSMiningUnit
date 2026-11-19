// FILE: mods/ShipControl/sc_bubble.hpp
// WHAT: bubble ladder state of ShipControl (SC3 A2): one BubbleState per ship (g_bub), braking query for the move job, drop on release,
//       and the status copy for SC_bubble_status.
// WHY: the state lives in ShipControl so the brake and the replan/skip/waypoint decisions are made from one snapshot per ship (no mining copy).
// DEPENDS ON (exact): sc_api.h (ScBubbleStatus), sc_logic_bubble.hpp (BubbleState), <map>
// USED BY: sc_move.hpp (step_job brake), sc_bubble_run.hpp (engine), dllmain.cpp (SC_release, SC_release_all, SC_bubble_status), tests/test_shipcontrol.cpp
// GAME FACTS: none. Keys are ship ids chosen by the caller.
#pragma once

#include "sc_api.h"
#include "sc_logic_bubble.hpp"
#include <map>

namespace
{
    // WHAT: ladder state per ship id. Absent = no state (the ship has no bubble activity).
    std::map<int, BubbleState> g_bub;

    // WHAT: true when the bubble ladder of ship sid holds the throttle at 0 (braking). Absent = false.
    // WHY: step_job (sc_move.hpp) stops its own thrust while a bubble brake holds.
    bool bub_braking(int sid)
    {
        auto it = g_bub.find(sid);
        return it != g_bub.end() && it->second.braking;
    }

    // WHAT: drops the ladder state of ship sid (SC_release, SC_release_all). No callback.
    void bub_drop(int sid)
    {
        g_bub.erase(sid);
    }

    // WHAT: fills o (size already set by the caller) from the ladder state of ship sid. have = 0 when no state (all other fields 0).
    // WHY: SC_bubble_status and the mining status line read the same numbers.
    void bub_status_fill(int sid, ScBubbleStatus& o)
    {
        const int32_t sz = o.size;
        o = ScBubbleStatus{};
        o.size = sz;
        auto it = g_bub.find(sid);
        if (it == g_bub.end()) return;
        const BubbleState& s = it->second;
        o.have = 1;
        o.braking = s.braking ? 1 : 0;
        o.replans = s.replans;
        o.pending = s.pending;
        o.wp = s.wp;
        o.resume = s.resume ? 1 : 0;
        o.blk = s.blk;
        o.phase = s.phase;
        o.why = s.why;
        o.rock = s.rock;
        o.blk_d = s.blk_d;
    }
}
