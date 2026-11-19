// FILE: mods/ShipControl/sc_frame.hpp
// WHAT: ship frame and aim numbers of the ShipControl move job: the frame from one MsFlight snapshot, the aim of a fixed point,
//       a point's location in the ship frame, and the object-to-actor axis calibration (learned once from a ship with an actor).
// WHY: ported 1:1 from MiningBenchMod mb_rocks.hpp (act_aim_pt, act_calib_learn, act_frame_obj, act_frame) and mb_steer.hpp (pt_rloc).
//      The mining copies stay until SC2b slice C. Pure: no game calls, the snapshot comes from MSCore (sc_mscore.hpp).
// DEPENDS ON (exact): sc_mscore.hpp (MsFlight, FF_*), sc_math.hpp (norm3, dot3, cross3, qcal_pick, qcal_apply, SC_PI), sc_logic_steer.hpp (SM_* units),
//                     <algorithm>, <cmath>, <cstdint>, <string>
// USED BY: sc_speed.hpp (sc_calib_learn in speed_sample), sc_move.hpp (SC2b A2), tests/test_shipcontrol.cpp
// GAME FACTS: actor location is uu (100 per m). Ship object location = chunk (2048 m) + raw local loc (65536 per m). Quat from MsFlight is already / 65536.
#pragma once

#include "sc_mscore.hpp"
#include "sc_math.hpp"
#include "sc_logic_steer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
    // WHAT: ship frame in world uu. loc = position (uu). fw / R / U = unit forward, right, up axes.
    struct ScFrame { double loc[3] = {0, 0, 0}, fw[3] = {1, 0, 0}, R[3] = {0, 1, 0}, U[3] = {0, 0, 1}; };

    // WHAT: aim numbers of one point against a frame. dist_m = centre distance (m); err_deg = angle nose to point (deg);
    //       a_body = rotation axis in ship axes (a = cross(fw, dir)); rloc = the point (uu).
    struct ScAim { double dist_m = 0.0, err_deg = 0.0, a_body[3] = {0, 0, 0}, rloc[3] = {0, 0, 0}; };

    // WHAT: object-to-actor axis map. ok = learned from one ship with an actor frame and object pose. qmode / ax as in qcal_pick. Default ax = +X, +Y, +Z.
    struct ScCalib { bool ok = false; int qmode = 0; int ax[3] = {0, 2, 4}; };
    ScCalib g_calib;

    // WHAT: aim numbers of a fixed point rloc (uu, world) against frame f. false when the point is at the ship (degenerate).
    // WHY: 1:1 port of act_aim_pt (mb_rocks.hpp).
    bool aim_pt(const ScFrame& f, const double rloc[3], ScAim& r)
    {
        std::copy(rloc, rloc + 3, r.rloc);
        double d[3] = {r.rloc[0] - f.loc[0], r.rloc[1] - f.loc[1], r.rloc[2] - f.loc[2]};
        const double n = norm3(d);
        if (n < 1e-9) return false;
        for (double& x : d) x /= n;
        r.dist_m = n / SM_UU_PER_M;
        r.err_deg = std::acos(std::clamp(dot3(f.fw, d), -1.0, 1.0)) * 180.0 / SC_PI;
        double a[3];
        cross3(f.fw, d, a);
        r.a_body[0] = dot3(a, f.fw);
        r.a_body[1] = dot3(a, f.R);
        r.a_body[2] = dot3(a, f.U);
        return true;
    }

    // WHAT: rock-free location of a point in the ship frame (uu). Point chunk / raw loc vs ship chunk / raw loc, same system only.
    //       rloc = f.loc + (dr / 65536) * 100, dr = (tch - sch) * 134217728 + (tloc - sloc) (raw).
    // WHY: 1:1 port of pt_rloc (mb_steer.hpp). The ship chunk and raw loc come from the snapshot (MsFlight ch / loc_raw), not from the game.
    bool pt_rloc_calc(const ScFrame& f, int ship_sys, const int64_t sch[3], const double sloc[3], int tsys, const int64_t tch[3], const double tloc[3], double rloc[3])
    {
        if (ship_sys != tsys) return false;
        for (int i = 0; i < 3; ++i)
        {
            const double dr = static_cast<double>(tch[i] - sch[i]) * SM_CHUNK_RAW + (tloc[i] - sloc[i]);
            rloc[i] = f.loc[i] + dr / SM_RAW_PER_M * SM_UU_PER_M;
        }
        return true;
    }

    // WHAT: learns the object-to-actor axis map from one snapshot that has the actor frame and the object pose. Only while not learned yet.
    //       true = learned now (the caller logs it once). false = already learned, or the snapshot cannot teach it.
    // WHY: 1:1 port of the calibration in act_calib_learn (mb_rocks.hpp). Called from speed_sample (sc_speed.hpp).
    bool sc_calib_learn(const MsFlight& f)
    {
        if (g_calib.ok || !(f.flags & FF_ACTOR_FRAME) || !(f.flags & FF_OBJ_POSE)) return false;
        int qm = 0;
        int ax[3] = {0, 2, 4};
        if (!qcal_pick(f.quat, f.fw, f.R, f.U, qm, ax)) return false;
        g_calib.ok = true;
        g_calib.qmode = qm;
        std::copy(ax, ax + 3, g_calib.ax);
        return true;
    }

    // WHAT: fills out from one snapshot. Actor frame (FF_ACTOR_FRAME) is used as read. Else object pose (FF_OBJ_POSE) with the calibration
    //       learned: loc = (ch * 2048 + loc_raw / 65536) * 100 (uu), axes from quat through the calibrated map. Else why = 'ship frame unreadable'.
    // WHY: 1:1 port of act_frame / act_frame_obj (mb_rocks.hpp) without the game reads.
    bool frame_from_flight(const MsFlight& f, ScFrame& out, std::wstring& why)
    {
        if (f.flags & FF_ACTOR_FRAME)
        {
            std::copy(f.act_loc, f.act_loc + 3, out.loc);
            std::copy(f.fw, f.fw + 3, out.fw);
            std::copy(f.R, f.R + 3, out.R);
            std::copy(f.U, f.U + 3, out.U);
            why.clear();
            return true;
        }
        if ((f.flags & FF_OBJ_POSE) && g_calib.ok)
        {
            for (int i = 0; i < 3; ++i) out.loc[i] = (static_cast<double>(f.ch[i]) * SM_CHUNK_M + f.loc_raw[i] / SM_RAW_PER_M) * SM_UU_PER_M;
            qcal_apply(f.quat, g_calib.qmode, g_calib.ax, out.fw, out.R, out.U);
            why.clear();
            return true;
        }
        why = L"ship frame unreadable";
        return false;
    }
}
