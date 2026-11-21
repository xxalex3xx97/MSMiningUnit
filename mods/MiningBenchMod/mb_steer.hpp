// FILE: mods/MiningBenchMod/mb_steer.hpp
// WHAT: steering for the move job (face_rock / approach_rock): rotation through the PilotConsole aim offset, the Throttle
//       profile for the approach, the speed safety monitor, move job start / tick / finish, and zero_all (stop throttle and rotation).
// WHY separate: this is the proven product steering code (confirmed working in game, brief Update 28). It is kept apart from the
//       dock job state machine (mb_dockjob.hpp) and from the Test tab (mb_testcmd.hpp).
// DEPENDS ON: mb_common.hpp, mb_config.hpp (eff_cfg, parse_distance, g_cfg, g_raw_per_m), mb_ue.hpp, mb_math.hpp (dist3,
//             quat_angle_deg, MV_PI), mb_orders.hpp (add_input), mb_cmdstate.hpp (g_cmd, g_jobs),
//             mb_rocks.hpp (actor_ok, act_frame, act_aim, pick_rock, rock_id_of, rock_kind, rock_radius_raw, ActFrame, ActAim).
// USED BY: mb_dockjob.hpp (zero_all_dual, MV_UNDOCK_*, MV_SKIP_*), mb_testcmd.hpp (move_start, move_tick, spd_update_sc, g_mvs,
//          mv_active, mv_cancel, MoveJob, zero_all, g_last_err, MV_SPEED_ABORT), dllmain.cpp (include only).
// PER SHIP: move jobs (g_mvs) are keyed by ship id, so each ship steers on its own (ShipControl runs each one). SC6-D2: legacy steering removed.
// GAME FACTS (fact = confirmed in game, see brief Update 28 unless noted; ASSUMPTION = taken from code/notes; UNVERIFIED = not confirmed):
//   - fact: steering = AddInputCommand(PilotConsoleObject id, ViewInput{ControlMode=SHIP(1), ViewRotation}), the ViewInput built
//     with ViewInput::CreateInputView. ViewRotation is an AIM OFFSET relative to the ship nose: the ship turns at a rate that grows
//     with the offset, and (0,0) stops the turn.
//   - fact (user estimate): the game ignores aim offsets inside a dead zone of about 7 deg. Smaller offsets did not turn the ship.
//     So the offset sent = 7 deg + remaining error, clamped to 20 deg.
//   - fact: body axes: nose = +X. Yaw = rotation about +Z (the ship also banks into a turn, but roll does not move the nose).
//     Pitch = rotation about -Y.
//   - ASSUMPTION: ViewRotation raw scale = 65536 raw per deg (VIEW_RAW_PER_DEG). The working steering uses it; not re-measured.
//   - Throttle steps run -10..+10 (code note on MV_APPROACH_MAX_THR). fact (brief Update 22): step 1 gave about 5.7 m/s, 5% of the ship limit.
//   - fact: speed limit = GetModifiedMaxSpeed (else GetMaxSpeed), raw per s. Ship speed is measured from the ship ACTOR position.
//   - fact: game distance = K2_GetActorLocation difference / 100 (uu -> m). Save-object Location is chunk-relative: not used here.
//   - Stop distance = SURFACE distance (centre distance - rock radius) vs the per-ship mining_distance setting (m).
//     The HUD shows the centre distance (user chose: stop on surface, Update 28).
//   - UNITS used below: m = metres, s = seconds, deg = degrees, raw = PNum raw (65536 per m), uu = Unreal units (100 uu = 1 m).

#pragma once

namespace
{
    // forward declarations (mb_warp.hpp is included after this file)
    bool get_chunk(UObject* o, int64_t c[3]);
    int sys_id(UObject* o);

    // ---------------- steering / undock / throttle / move jobs (spec 2026-10-27) ----------------
    // Constants below are grouped by job. Units are in each comment.

    // WHAT: abort threshold, speed / speed limit (ratio). Above it: throttle and rotation stop, the move job is cancelled.
    // WHY: user safety rule: never exceed what the ship can do (no speed override).
    constexpr double MV_SPEED_ABORT = 1.1;

    // WHAT: face_rock is done when the heading error is below this (deg).
    // WHY: tolerance chosen in code; unknown why 2 deg (ported as is).
    constexpr double MV_FACE_DONE_DEG = 2.0;

    // WHAT: undock job fails after this many seconds (s). WHY: unknown (ported as is).
    constexpr double MV_UNDOCK_TIMEOUT_S = 120.0;

    // WHAT: undock is done when the ship is this far from its start point (m).
    // WHY: the autopilot only has to get the ship clear of the dock. Why 50 m: unknown (ported as is).
    constexpr double MV_UNDOCK_CLEAR_M = 50.0;

    // WHAT: dock leg: first SkipToOrder this many seconds (s) after the leg starts (SetShipAutopilot(1)).
    // WHY: the player flow does SetShipAutopilot then SkipToOrder (brief Update 24). The wait value is unknown (ported as is).
    constexpr double MV_SKIP_WAIT_S = 1.0;

    // WHAT: dock leg: if the ship moved less than 1 m this many seconds (s) after SkipToOrder, SkipToOrder is sent once more.
    // WHY: fallback for a leg that did not start. Value unknown (ported as is).
    constexpr double MV_SKIP_FB_S = 3.0;

    // face/approach rotation actuator = view_send (InputView.ViewRotation aim offset, proven in game).
    // WHAT: max aim offset magnitude sent (deg). WHY: clamp on the offset; value unknown (ported as is).
    constexpr double MV_VIEW_MAX_DEG = 20.0;

    // WHAT: game dead zone around the aim cursor (deg). User estimate about 7 deg.
    // WHY: offsets below this did not turn the ship (face_rock stalled at 5.76 deg error), so the offset sent = dead zone + error.
    constexpr double MV_VIEW_DEADZONE_DEG = 7.0;

    // WHAT: resend period (s) of the aim offset. Not every engine tick.
    // WHY: fewer CreateInputView + AddInputCommand calls. Exact value unknown (ported as is).
    constexpr double MV_VIEW_PERIOD_S = 0.1;

    // Sign of the offsets (view_aim_drive). The offset axes are measured body axes (g_view_up / g_view_pitch below).
    // WHAT: sign applied to the yaw offset. +1 is the sign that works in game (steering confirmed, brief Update 28).
    // WHY: if the ship ever turned away from the rock, the sign would flip to -1.
    constexpr double MV_VIEW_YAW_SIGN = 1.0;

    // WHAT: sign applied to the pitch offset. +1 is the sign that works in game (steering confirmed, brief Update 28).
    constexpr double MV_VIEW_PITCH_SIGN = 1.0;

    // WHAT: forward declaration of view_send (defined further down in this file, wraps MSCore_view_send).
    // WHY: mv_finish calls it before its definition.
    bool view_send(int cid, int64_t yaw_raw, int64_t pitch_raw, std::wstring& err);

    // WHAT: speed monitor state. Sampled every poll step (CMD_POLL_S) for g_cmd.ship by spd_update.
    //       p[3] = ship position (raw). v = speed (raw per s) from the last poll step. lim = speed limit (raw per s).
    //       started / have / aborted = flags (first sample, valid v, abort already reported). sid = ship id of the samples.
    // WHY: the abort and the status line both read this one state.
    struct SpeedMon
    {
        double p[3] = {0, 0, 0};
        double v = 0.0;                 // position change / poll dt (raw per s)
        double lim = 0.0;               // (GetModifiedMaxSpeed, else GetMaxSpeed) * steer_probe_speed_k (raw per s)
        bool started = false, have = false, aborted = false;
        int sid = 0;
    };

    // WHAT: fixed world point for a point move (no actor): system, chunk and raw location (same units as the ship save object).
    struct PtTarget { int sys = 0; int64_t ch[3] = {0, 0, 0}; double loc[3] = {0, 0, 0}; };

    // WHAT: one move job (face_rock or approach_rock). Only one runs at a time.
    //       kind = "" idle | "face_rock" | "approach_rock". sid = ship id, seq = command seq (result is reported under it).
    //       steps = Throttle steps of the ship (GetThrottleSteps). phase: 1 = aim (rotate), 2 = approach (Throttle).
    //       t = job total time (s). err = heading error (deg). cid = PilotConsoleObject id (from ShipControl at start).
    //       thr = applied Throttle step (ShipControl status).
    //       rock_name = ore name of the chosen rock. rock_act = rock actor, re-checked with actor_ok() every tick.
    struct MoveJob
    {
        std::string kind;               // "" idle | face_rock | approach_rock
        int sid = 0, seq = 0, rock_id = 0, steps = 0;
        // phase: face/approach: 1 aim, 2 approach
        int phase = 0;
        double t = 0.0;                 // job total time (s)
        double err = -1.0;
        int cid = 0;                    // PilotConsoleObject id (ShipControl, resolved at job start)
        bool ore_on = false;            // rock_act is a floating ore actor (it drifts): fly to ore_loc
        double ore_loc[3] = {0, 0, 0};  // ore: world point (uu) to fly to = the sweep scan position
        int thr = 0;                    // approach: Throttle step currently applied (ShipControl status)
        std::wstring rock_name;         // kind of the chosen rock (start log / status)
        UObject* rock_act = nullptr;    // rock ACTOR for face/approach (game list); alive-checked each tick, 'rock lost' when gone
        bool pt_on = false;             // point move: target is ptg (no actor). Named pt_on: pt is the phase timer.
        PtTarget ptg;
        bool warp_wait = false;         // far-trip warp sent for this flight: wait until it has landed (warp_settle_done), no thrust, no aim
        bool sc_on = false;             // ShipControl runs this job (SC2b): this record is a shadow, refreshed by sc_mv_sync; the legacy fields stay unused
    };

    // WHAT: move jobs per ship (sid -> job). Empty kind = no job for that ship. Each ship can steer on its own.
    std::map<int, MoveJob> g_mvs;
    // WHAT: far-trip warp of an approach flight (mb_mining.hpp): sends the warp when the destination is far, sets mv.warp_wait. true = warp sent.
    bool move_warp_begin(int sid, UObject* ship, MoveJob& mv);
    bool mv_active(int sid) { auto it = g_mvs.find(sid); return it != g_mvs.end() && !it->second.kind.empty(); }
    // WHAT: true when the move job of sid runs in ShipControl (sc_on). false when none or legacy.
    bool mv_active_sc(int sid) { auto it = g_mvs.find(sid); return it != g_mvs.end() && it->second.sc_on; }

    // Body-frame axes for the aim offset. Measured earlier in game (old code, removed); the working steering confirms them.
    // WHAT: unit body-frame axis of the YAW offset = +Z (unitless). WHY: a yaw offset turns the nose about +Z.
    //       The ship's roll (banking) is not part of this axis; roll does not move the nose.
    double g_view_up[3] = {0, 0, 1};

    // WHAT: unit body-frame axis of the PITCH offset = -Y (unitless). WHY: a pitch offset turns the nose about -Y (nose up).
    double g_view_pitch[3] = {0, -1, 0};

    // WHAT: heading error (deg) of the last aim step, -1 = none yet. WHY:
    double g_last_err = -1.0;

    // WHAT: raw units per degree of ViewRotation: 65536 (fixed point, same scale as raw_per_meter).
    // WHY: scale used by the working steering. ASSUMPTION: not re-measured in this pass.
    constexpr double VIEW_RAW_PER_DEG = 65536.0;

    // ---------------- stop and console helpers ----------------

    // WHAT: stop the ship's drive: MSCore_ship_zero (Throttle = 0, rotation = 0). When that export is missing or fails, the legacy writes:
    //       Throttle = 0 and RotationImpulseInput xyz = 0 (rot_zero). Does nothing when the ship is not alive.
    // WHY: one stop used by every job end and by stop_all, so every path stops the ship the same way. The fallback keeps Stop working
    //      when MSCore is older. ImpulseInput is never touched here.
    void zero_all(UObject* ship)
    {
        if (!alive(ship)) return;
        if (ms_zero_ship(obj_id(ship))) return;
        set_int_prop(ship, STR("Throttle"), 0);
        rot_zero(ship);
    }

    // WHAT: zero_all through ShipControl (SC_zero, owner "mining") while ShipControl does the speed monitor. Refused or failed = zero_all.
    // WHY: the same stop, one owner: the ship id is the object ID.
    void zero_all_dual(UObject* ship)
    {
        if (g_sc_speed_on && alive(ship) && sc_speed_zero(obj_id(ship))) return;
        zero_all(ship);
    }

    // WHAT: find the PilotConsoleObject of ship sid. Matches ShipID first, then ParentID. nullptr when none.
    //       how = which field matched ("ShipID" / "ParentID" / "none"). count = number of PilotConsoleObject instances seen.
    // WHY: view_send needs the console id. It is resolved once per move job and cached in MoveJob::cid.
    //      count is only stored by move_start (not used further).
    UObject* pilot_console_of(int sid, std::wstring& how, size_t& count)
    {
        std::vector<UObject*> pcs;
        UObjectGlobals::FindAllOf(STR("PilotConsoleObject"), pcs);
        count = pcs.size();
        UObject* console = nullptr;
        how = STR("none");
        for (int pass = 0; pass < 2 && !alive(console); ++pass)
        {
            const TCHAR* pname = pass == 0 ? STR("ShipID") : STR("ParentID");
            for (UObject* o : pcs)
            {
                int32_t v = -1;
                if (alive(o) && get_int_prop(o, pname, v) && v == sid) { console = o; how = pname; break; }
            }
        }
        return console;
    }

    // WHAT: drop the move job of ship sid without a result: stop its aim offset (ShipControl) and erase it. No-op when none.
    // WHY: stop_all and mining stop cancel one ship's job; other ships keep theirs.
    void mv_cancel(int sid)
    {
        auto it = g_mvs.find(sid);
        if (it == g_mvs.end()) return;
        sc_move_cancel(sid);   // ShipControl: aim offset stopped, job erased (no zero, no callback)
        g_mvs.erase(it);
    }

    // ---------------- move job end and speed monitor ----------------

    // WHAT: end the move job of ship sid: erase it, stop the aim offset and zero_all (ShipControl), log the result, and set the command
    //       result when this job is still the last command (seq check). Callers must return at once (the job is erased).
    // WHY: one exit path for every way a move job ends (ok, error, abort), so the ship is always stopped.
    void mv_finish(int sid, const std::wstring& res)
    {
        auto it = g_mvs.find(sid);
        if (it == g_mvs.end()) return;
        const MoveJob m = it->second;
        g_mvs.erase(it);
        g_mv_res[m.sid] = res;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), m.sid);
        sc_move_cancel(sid);   // no-op when ShipControl already ended the job (on_move_done)
        if (alive(ship)) zero_all_dual(ship);
        tlog(STR("seq=") + std::to_wstring(m.seq) + STR(" ") + widen(m.kind) + STR(" ship=") + std::to_wstring(m.sid) + STR(" result: ") + res);
        if (g_cmd.seq == m.seq) g_cmd.result = res;
    }

    // ---------------- ShipControl move jobs (SC2b): host callbacks and the shadow record ----------------

    // WHAT: the job move_start is starting in ShipControl (set for the SC_move_start call only). sc_host_warp_begin uses it. nullptr otherwise.
    MoveJob* g_sc_start_mv = nullptr;

    // WHAT: host tlog: every move-job line of ShipControl goes to the mining tlog (same text as the legacy line). Never throws.
    void sc_host_tlog(const wchar_t* text)
    {
        try { tlog(text ? std::wstring(text) : std::wstring()); }
        catch (...) {}
    }

    // WHAT: host move-done: ShipControl ended the job of sid (job erased and ship zeroed there). Same exit as a legacy job end. Never throws.
    void sc_host_move_done(int sid, int seq, const wchar_t* result)
    {
        (void)seq;
        try { mv_finish(sid, result ? std::wstring(result) : std::wstring()); }
        catch (...) {}
    }

    // WHAT: host warp_begin: the far-trip warp of the approach job being started (g_sc_start_mv, move_warp_begin). 1 = warp sent. Never throws.
    int sc_host_warp_begin(int sid, const ScMoveReq* req)
    {
        (void)req;
        try
        {
            if (!g_sc_start_mv) return 0;
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            return move_warp_begin(sid, ship, *g_sc_start_mv) ? 1 : 0;
        }
        catch (...) { return 0; }
    }

    // WHAT: host remain_m: surface distance (m) from ship_m to the registry rock of the job of sid (warp settle). 1 = ok, 0 = no such rock. Never throws.
    int sc_host_remain(int sid, const double ship_m[3], double* out_m)
    {
        try
        {
            auto it = g_mvs.find(sid);
            if (it == g_mvs.end() || !out_m) return 0;
            RegRock r;
            if (!reg_get(it->second.rock_id, r)) return 0;
            *out_m = remain_dist_m(ship_m, r.c, reg_eff_rad(r));
            return 1;
        }
        catch (...) { return 0; }
    }

    // WHAT: host target_ok: the rock actor of a job is still valid. Never throws.
    int sc_host_target_ok(int sid, void* handle)
    {
        (void)sid;
        try { return actor_ok(static_cast<UObject*>(handle)) ? 1 : 0; }
        catch (...) { return 0; }
    }

    // WHAT: ShipControl host callback ore_loc: location (uu, K2_GetActorLocation) of a floating ore actor for the ore aim. 1 = ok. Never throws.
    int sc_host_ore_loc(int sid, void* handle, double* out3_uu)
    {
        (void)sid;
        try
        {
            UObject* o = static_cast<UObject*>(handle);
            std::wstring why;
            if (!out3_uu || !ore_ok(o) || !actor_vec_call(o, STR("K2_GetActorLocation"), out3_uu, why)) return 0;
            return 1;
        }
        catch (...) { return 0; }
    }

    // WHAT: refreshes the shadow record of the ShipControl job of sid from SC_move_status: phase, warp wait, heading error, Throttle step, ore point.
    //       The command ship's heading error goes to g_last_err (status). Legacy jobs and missing jobs: nothing.
    void sc_mv_sync(int sid)
    {
        auto it = g_mvs.find(sid);
        if (it == g_mvs.end() || !it->second.sc_on) return;
        ScMoveStatus st;
        if (!sc_move_status(sid, st) || !st.active) return;
        MoveJob& m = it->second;
        m.phase = st.phase;
        m.warp_wait = st.warp_wait != 0;
        m.err = st.err_deg;
        m.thr = st.thr;
        std::copy(st.ore_loc, st.ore_loc + 3, m.ore_loc);
        if (sid == g_cmd.ship) g_last_err = st.err_deg;
    }

    // WHAT: speed monitor for every ship that needs it: the command ship, every ship with a move job, every ship with a dock job.
    //       Monitor entries of other ships are erased (a stale sample would give a false speed).
    std::set<int> spd_ids()
    {
        std::set<int> ids;
        if (g_cmd.ship > 0) ids.insert(g_cmd.ship);
        for (const auto& kv : g_mvs) ids.insert(kv.first);
        for (const DockJob& j : g_jobs) ids.insert(j.sid);
        return ids;
    }

    // WHAT: speed monitor, run every poll step (CMD_POLL_S). ShipControl usable (sc_speed_block == nullptr): it watches the ships of spd_ids
    //       (owner "mining"), SC_tick_speed samples and zeroes them, and its fresh aborts (host callback -> g_sc_abort) run the mining abort text here.
    //       ShipControl not usable or SC_tick_speed -1: no ship is watched, readers get a zeroed SpeedMon (spd_get). Mode change logged once.
    // WHY: one speed guard per ship, ShipControl is the only sampler (SC6-D2: the legacy sampler is gone).
    void spd_update_sc(double dt)
    {
        const std::set<int> ids = spd_ids();
        const wchar_t* block = sc_speed_block();
        // watch set: the ids while ShipControl is usable, else none
        for (auto it = g_sc_watch.begin(); it != g_sc_watch.end();)
        {
            if (block || !ids.count(*it)) { sc_watch_ship(*it, false); it = g_sc_watch.erase(it); }
            else ++it;
        }
        if (!block)
        {
            sc_speed_tune();
            for (int sid : ids) if (!g_sc_watch.count(sid) && sc_watch_ship(sid, true)) g_sc_watch.insert(sid);
        }
        g_sc_last_tick = block ? 0 : sc_tick_speed(dt);
        g_sc_speed_on = !block && g_sc_last_tick != -1;
        // fresh aborts of this step (ShipControl already zeroed the ship): same text as before
        for (const auto& ab : g_sc_abort)
        {
            tlog(STR("ABORT speed ship=") + std::to_wstring(ab.first) + STR(" v/lim=") + std::to_wstring(ab.second));
            if (mv_active(ab.first)) mv_finish(ab.first, STR("error: ABORT speed v/lim=") + std::to_wstring(ab.second));
        }
        g_sc_abort.clear();
        const wchar_t* why = block ? block : STR("MSCore not usable (SC_tick_speed -1)");
        const std::wstring mode = g_sc_speed_on ? std::wstring(STR("using ShipControl")) : std::wstring(STR("off (")) + why + STR(")");
        if (mode != g_sc_mode_note) { g_sc_mode_note = mode; log_line(STR("SHIPCTL speed: ") + mode); }
    }

    // WHAT: speed reading of ship sid for the readers (bubble, dock job, move brake, dev probes): ShipControl's reading (SC_speed) when it
    //       watches sid. Otherwise a zeroed SpeedMon (have = false). Returned by value: readers only read the fields.
    SpeedMon spd_get(int sid)
    {
        SpeedMon m;
        m.sid = sid;
        if (!g_sc_speed_on || !g_sc_watch.count(sid)) return m;
        ScSpeed s = {};
        s.size = static_cast<int32_t>(sizeof(ScSpeed));
        if (sc_speed_read(sid, s) && s.have) { m.have = true; m.v = s.v_raw; m.lim = s.lim_raw; }
        m.aborted = s.aborted != 0;
        return m;
    }

    // ---------------- low power (ShipControl rule: sc_logic_steer.hpp power_dead_from) ----------------
    // WHAT: power state of a ship from one FL_POWER flight read (MSCore API 10). ok = grid numbers read (FF_GRID).
    //       lp_known / lp_on = low-power flag readable (FF_LP_KNOWN) / on (FF_LP_ON). All false when the read failed.
    struct PowerGridRead { bool ok = false; double prod = 0.0, cons = 0.0, eff = 0.0; bool lp_known = false, lp_on = false; };
    PowerGridRead power_grid(UObject* ship)
    {
        PowerGridRead g;
        if (!alive(ship)) return g;
        MsFlight f;
        if (ms_flight_read(obj_id(ship), FL_POWER, f) != 1) return g;
        g.ok = (f.flags & FF_GRID) != 0;
        g.prod = f.grid_prod;
        g.cons = f.grid_cons;
        g.eff = f.grid_eff;
        g.lp_known = (f.flags & FF_LP_KNOWN) != 0;
        g.lp_on = (f.flags & FF_LP_ON) != 0;
        return g;
    }

    // WHAT: reactors of ship (ship.Reactors): one object property or an array of objects. Alive objects only, in order.
    std::vector<UObject*> reactors_of(UObject* ship)
    {
        std::vector<UObject*> out;
        FProperty* p = ship ? ship->GetPropertyByNameInChain(STR("Reactors")) : nullptr;
        if (!p) return out;
        if (CastField<FObjectProperty>(p))
        {
            UObject* r = get_obj_prop(ship, STR("Reactors"));
            if (alive(r)) out.push_back(r);
            return out;
        }
        auto* ap = CastField<FArrayProperty>(p);
        auto* arr = ap && CastField<FObjectProperty>(ap->GetInner()) ? ship->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Reactors")) : nullptr;
        if (!arr) return out;
        for (int32_t i = 0; i < arr->Num(); ++i) if (alive((*arr)[i])) out.push_back((*arr)[i]);
        return out;
    }

    // WHAT: no power now: power_dead_from on the flight power read (grid when readable, else the low-power flag). known = a source was readable.
    bool power_dead(const PowerGridRead& g, bool& known)
    {
        return power_dead_from(g.ok, g.prod, g.cons, g.eff, g.lp_known, g.lp_on, known);
    }

    // WHAT: move job tick, every engine tick while a job runs: ShipControl's jobs (sc_on) only. One tick of all of them (sc_tick_frame), then the
    //       shadow record of each is synced from SC_move_status. -1 = ShipControl killed or MSCore unusable: every job is ended.
    void move_tick(double dt)
    {
        std::vector<int> sc_ids;
        for (const auto& kv : g_mvs) if (kv.second.sc_on) sc_ids.push_back(kv.first);
        if (sc_ids.empty()) return;
        if (sc_tick_frame(dt) == -1) { for (int sid : sc_ids) mv_finish(sid, STR("error: MSCore flight unavailable")); return; }
        for (int sid : sc_ids) sc_mv_sync(sid);
    }

    // WHAT: start a face_rock or approach_rock job. Returns "waiting" when started, else the error text. Only one move job at a time.
    //       Steps: 1 refuse if a move job runs or a dock job runs for this ship. 2 find the ship, claim it (policy). 3 pick the rock (rock_sel,
    //       else nearest of INI resource). 4 approach_rock needs mining_distance set. 5 ShipControl starts the job (steps and console read there), log.
    // WHY: all preconditions are checked here before the first write, so a failed start changes nothing on the ship.
    // WHAT: ore_pt = scan point (uu) of a floating ore target (sweep): the start point of the ShipControl job.
    std::wstring move_start(int sid, const std::string& cmd, int seq, UObject* target = nullptr, const PtTarget* pt = nullptr, const double* ore_pt = nullptr)
    {
        // 1. one move job at a time, and no dock job for the same ship
        if (mv_active(sid)) return STR("error: move job already running");
        for (const DockJob& j : g_jobs) if (j.sid == sid) return STR("error: dock job already running for this ship");
        // 2. ship
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        // SC6 policy: the ship must be claimed by mining (dev commands claim it here too); refused = error, no legacy path
        if (!g_sc_held.count(sid))
        {
            std::wstring w;
            if (!sc_policy_ok()) return std::wstring(STR("error: ")) + SC_NEED_TEXT;
            if (!sc_claim_ship(sid, &w)) return STR("error: ") + w;
        }
        // ShipControl reads the steps and the console itself; not usable for this ship = error
        if (!sc_move_ready(sid)) return std::wstring(STR("error: ")) + SC_NEED_TEXT;
        MoveJob m;
        m.kind = cmd;
        m.sid = sid;
        m.seq = seq;
        std::wstring pick_rule;   // which pick_rock rule chose the rock (start log)
        // 5. rock
        std::wstring why;
        if (pt)
        {
            m.pt_on = true;
            m.ptg = *pt;
            m.rock_id = 0;
            m.rock_name = STR("point");
        }
        else if (ore_ok(target))
        {
            m.rock_act = target;
            m.ore_on = true;
            m.rock_id = 0;
            m.rock_name = STR("floating ore");
        }
        else
        {
            UObject* rock = pick_rock(sid, ship, why, pick_rule);   // rock_sel > nearest rock actor of the INI item (game distance)
            if (!alive(rock)) return STR("error: ") + (why.empty() ? STR("no rock") : why);
            m.rock_act = rock;
            m.rock_id = rock_id_of(rock);
            m.rock_name = rock_kind(rock);
        }
        // 6. approach needs the stop distance
        if (cmd == "approach_rock" && !pt && parse_distance(eff_cfg(sid).mining_distance) < 0.0) return STR("error: mining_distance unset");
        // 7. start. ShipControl builds the job (mode 0 rock, 1 point, 2 ore); its warp comes from sc_host_warp_begin.
        ScMoveReq req = {};
        req.size = static_cast<int32_t>(sizeof(ScMoveReq));
        req.kind = cmd == "approach_rock" ? 1 : 0;
        req.seq = seq;
        req.tag = m.rock_id;
        req.stop_m = parse_distance(eff_cfg(sid).mining_distance) >= 0.0 ? parse_distance(eff_cfg(sid).mining_distance) : 0.0;
        if (pt)
        {
            req.mode = 1;
            req.sys = pt->sys;
            std::copy(pt->ch, pt->ch + 3, req.ch);
            std::copy(pt->loc, pt->loc + 3, req.loc);
        }
        else if (m.ore_on)
        {
            req.mode = 2;
            req.handle = target;
            if (ore_pt) std::copy(ore_pt, ore_pt + 3, req.pt_uu);
        }
        else
        {
            req.mode = 0;
            if (!rock_loc_uu(m.rock_act, req.pt_uu)) return STR("error: rock lost");
            req.handle = m.rock_act;
            req.radius_m = rock_radius_raw(m.rock_act) / g_raw_per_m;
        }
        std::wstring serr;
        MoveJob* prev = g_sc_start_mv;
        g_sc_start_mv = &m;   // warp_begin (approach) reads this job
        const bool started = sc_move_start(req, sid, serr);
        g_sc_start_mv = prev;
        if (!started) return serr;
        m.sc_on = true;
        m.cid = req.out_cid;
        m.steps = req.out_steps;
        m.phase = 1;
        g_mvs[sid] = m;
        sc_mv_sync(sid);
        tlog(STR("start ") + widen(cmd) + STR(" ship=") + std::to_wstring(sid) + STR(" seq=") + std::to_wstring(seq) +
             STR(" cid=") + std::to_wstring(m.cid) +
             STR(" rock=") + std::to_wstring(m.rock_id) + (m.rock_id > 0 ? STR(" kind=") + m.rock_name + STR(" pick=") + pick_rule : std::wstring()));
        return STR("waiting");
    }

    // WHAT: send one InputView command to console cid, carrying the aim offset (yaw_raw, pitch_raw in raw units).
    // WHY: the InputView copy, ControlMode = SHIP and ViewRotation writes now live in MSCore (MSCore_view_send).
    bool view_send(int cid, int64_t yaw_raw, int64_t pitch_raw, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        int ok = g_ms.view_send(cid, yaw_raw, pitch_raw, e, 256);
        if (!ok) err = e;
        return ok != 0;
    }
}
