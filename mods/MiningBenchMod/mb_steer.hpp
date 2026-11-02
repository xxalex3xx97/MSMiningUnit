// FILE: mods/MiningBenchMod/mb_steer.hpp
// WHAT: steering for the move job (face_rock / approach_rock): rotation through the PilotConsole aim offset, the Throttle
//       profile for the approach, the speed safety monitor, move job start / tick / finish, and zero_all (stop throttle and rotation).
// WHY separate: this is the proven product steering code (confirmed working in game, brief Update 28). It is kept apart from the
//       dock job state machine (mb_dockjob.hpp) and from the Test tab (mb_testcmd.hpp).
// DEPENDS ON: mb_common.hpp, mb_config.hpp (eff_cfg, parse_distance, g_cfg, g_raw_per_m), mb_ue.hpp, mb_math.hpp (dist3,
//             quat_angle_deg, MV_PI), mb_orders.hpp (add_input), mb_cmdstate.hpp (g_cmd, g_jobs),
//             mb_rocks.hpp (actor_ok, act_frame, act_aim, pick_rock, rock_id_of, rock_kind, rock_radius_raw, ActFrame, ActAim).
// USED BY: mb_dockjob.hpp (zero_all, spd_of, MV_UNDOCK_*, MV_SKIP_*), mb_testcmd.hpp (move_start, move_tick, spd_update, g_mvs,
//          mv_active, mv_cancel, MoveJob, view_zero, zero_all, spd_of, g_last_err, MV_SPEED_ABORT), dllmain.cpp (include only).
// PER SHIP: move jobs (g_mvs) and speed monitors (g_spds) are keyed by ship id, so each ship steers on its own.
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
    // ---------------- steering / undock / throttle / move jobs (spec 2026-10-27) ----------------
    // Constants below are grouped by job. Units are in each comment.

    // WHAT: abort threshold, speed / speed limit (ratio). Above it: throttle and rotation stop, the move job is cancelled.
    // WHY: user safety rule: never exceed what the ship can do (no speed override).
    constexpr double MV_SPEED_ABORT = 1.1;

    // WHAT: rate sample period (s) for g_mv.rate. It only feeds the aim trace line.
    constexpr double MV_SAMPLE_S = 0.25;

    // WHAT: speed / limit ratio above which a sample is treated as a position glitch (ratio). The sample is dropped, no abort.
    // WHY: position jumps (floating origin, chunk change) gave fake speeds (v/lim = 217 seen with the save-object position).
    constexpr double MV_SPEED_GLITCH = 10.0;

    // WHAT: face_rock is done when the heading error is below this (deg).
    // WHY: tolerance chosen in code; unknown why 2 deg (ported as is).
    constexpr double MV_FACE_DONE_DEG = 2.0;

    // WHAT: face_rock fails after this many seconds in phase 1 (s). WHY: unknown (ported as is).
    constexpr double MV_FACE_TIMEOUT_S = 30.0;

    // WHAT: approach_rock enters phase 2 (approach, Throttle) when the heading error is below this (deg).
    // WHY: the ship only thrusts when it already points roughly at the rock. Value unknown (ported as is).
    constexpr double MV_APPROACH_START_DEG = 10.0;

    // WHAT: heading error above this (deg) forces the approach Throttle target to 0.
    // WHY: no thrust while the ship points far from the rock. Value unknown (ported as is).
    constexpr double MV_APPROACH_HEAD_DEG = 10.0;

    // WHAT: approach_rock fails after this many seconds in total (s). WHY: unknown (ported as is).
    constexpr double MV_APPROACH_TIMEOUT_S = 300.0;

    // approach Throttle profile (see move_aim_step phase 2).
    // WHAT: remaining distance per Throttle step (m): target step = floor(remaining / this), clamped to 1..MV_APPROACH_MAX_THR.
    // WHY: the ship slows down step by step as it nears the stop distance. Value unknown (ported as is).
    constexpr double MV_APPROACH_M_PER_STEP = 25.0;

    // WHAT: top forward Throttle step used by the approach (steps). The game has steps -10..+10.
    // WHY: fixed step 1 made the ship crawl at about 2 m/s (observed), so the approach uses steps up to this.
    constexpr int MV_APPROACH_MAX_THR = 10;

    // WHAT: the applied Throttle step rises by 1 at most once per this many seconds (s). It falls straight to the target.
    // WHY: gradual acceleration, so the 1.1x speed abort is not hit far from the rock.
    constexpr double MV_THR_RAMP_S = 0.5;

    // WHAT: overshoot brake: reverse Throttle step (steps) used while the ship still moves past the stop distance.
    constexpr int MV_BRAKE_THR = -2;

    // WHAT: brake until speed is below this (m/s). The name says MS, it means metres per second, not milliseconds.
    // WHY: then Throttle goes to 0 and the approach job ends.
    constexpr double MV_BRAKE_DONE_MS = 1.0;

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

    // WHAT: forward declaration of view_send (defined further down in this file, see raw helpers).
    // WHY: view_zero and mv_finish call it before its definition.
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

    // WHAT: the speed monitor state per ship (sid -> state). spd_of creates it on first use.
    std::map<int, SpeedMon> g_spds;
    SpeedMon& spd_of(int sid) { return g_spds[sid]; }

    // WHAT: one move job (face_rock or approach_rock). Only one runs at a time.
    //       kind = "" idle | "face_rock" | "approach_rock". sid = ship id, seq = command seq (result is reported under it).
    //       steps = Throttle steps of the ship (GetThrottleSteps). phase: 1 = aim (rotate), 2 = approach (Throttle).
    //       t / pt / samp / log_acc / view_t / thr_t = timers in s (job total, phase, rate sample, log, offset resend, Throttle ramp).
    //       qs = quaternion at the last rate sample. rate = turn rate over the last sample (deg/s, trace only).
    //       err = heading error (deg). cid = PilotConsoleObject id for view_send (resolved once at start).
    //       aligned = (0,0) already sent for the current aligned spell. thr = applied Throttle step (ramped).
    //       rock_name = ore name of the chosen rock. rock_act = rock actor, re-checked with actor_ok() every tick.
    struct MoveJob
    {
        std::string kind;               // "" idle | face_rock | approach_rock
        int sid = 0, seq = 0, rock_id = 0, steps = 0;
        // phase: face/approach: 1 aim, 2 approach
        int phase = 0;
        double t = 0.0, pt = 0.0, samp = 0.0, log_acc = 0.0;
        double qs[4] = {1, 0, 0, 0};    // quat at last rate sample
        double rate = 0.0;              // deg/s over the last sample
        double err = -1.0;
        int cid = 0;                    // PilotConsoleObject id for view_send (face/approach), resolved once at job start
        double view_t = 0.0;            // time since last view offset send
        bool aligned = false;           // (0,0) already sent for the current aligned spell
        int thr = 0;                    // approach: Throttle step currently applied (ramped, see phase 2)
        double thr_t = 0.0;             // approach: time since the last +1 ramp step
        std::wstring rock_name;         // kind of the chosen rock (start log / status)
        UObject* rock_act = nullptr;    // rock ACTOR for face/approach (game list); alive-checked each tick, 'rock lost' when gone
    };

    // WHAT: move jobs per ship (sid -> job). Empty kind = no job for that ship. Each ship can steer on its own.
    std::map<int, MoveJob> g_mvs;
    bool mv_active(int sid) { auto it = g_mvs.find(sid); return it != g_mvs.end() && !it->second.kind.empty(); }

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

    // WHAT: stop the ship's drive: Throttle = 0 and RotationImpulseInput xyz = 0 (rot_zero). Does nothing when the ship is not alive.
    // WHY: one stop used by every job end and by stop_all, so every path stops the ship the same way.
    //      ImpulseInput is never touched here.
    void zero_all(UObject* ship)
    {
        if (!alive(ship)) return;
        set_int_prop(ship, STR("Throttle"), 0);
        rot_zero(ship);
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

    // WHAT: send the aim offset (0,0) to console cid, which stops the rotation. Does nothing when cid is 0 (nothing was sent).
    // WHY: shared by mv_finish and stop_all, so both stop the rotation the same way. A failure is only logged,
    //      because the caller is ending anyway.
    void view_zero(int cid)
    {
        if (cid <= 0) return;
        std::wstring e;
        if (!view_send(cid, 0, 0, e)) tlog(STR("view_zero failed: ") + e);
    }

    // WHAT: drop the move job of ship sid without a result: stop its aim offset (view_zero) and erase it. No-op when none.
    // WHY: stop_all and mining stop cancel one ship's job; other ships keep theirs.
    void mv_cancel(int sid)
    {
        auto it = g_mvs.find(sid);
        if (it == g_mvs.end()) return;
        view_zero(it->second.cid);
        g_mvs.erase(it);
    }

    // ---------------- move job end and speed monitor ----------------

    // WHAT: end the move job of ship sid: erase it, stop the aim offset (view_zero), zero_all, log the result, and set the command
    //       result when this job is still the last command (seq check). Callers must return at once (the job is erased).
    // WHY: one exit path for every way a move job ends (ok, error, abort), so the ship is always stopped.
    void mv_finish(int sid, const std::wstring& res)
    {
        const MoveJob m = g_mvs[sid];
        g_mvs.erase(sid);
        g_mv_res[m.sid] = res;
        view_zero(m.cid);   // stop the aim offset (face/approach only; cid 0 = nothing sent)
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), m.sid);
        if (alive(ship)) zero_all(ship);
        tlog(STR("seq=") + std::to_wstring(m.seq) + STR(" ") + widen(m.kind) + STR(" ship=") + std::to_wstring(m.sid) + STR(" result: ") + res);
        if (g_cmd.seq == m.seq) g_cmd.result = res;
    }

    // WHAT: speed monitor sample for ship sid, run on every poll step (CMD_POLL_S = 0.25 s).
    //       Steps: 1 find the ship (gone -> reset its monitor). 2 read the ship ACTOR position, in raw units.
    //       3 speed v = position change / dt. 4 limit lim = GetModifiedMaxSpeed (else GetMaxSpeed) * steer_probe_speed_k.
    //       5 ratio > MV_SPEED_GLITCH: glitch, sample dropped. 6 ratio > MV_SPEED_ABORT: zero_all, and the move job of sid is ended once.
    // WHY: the user safety rule (no speed override): the ship must stay within its own speed limit. Position comes from the ship
    //      ACTOR: the save-object Location is chunk-relative and a chunk crossing caused a false abort (v/lim = 217).
    void spd_sample(int sid, double dt)
    {
        UObject* ship = sid > 0 ? find_by_id(STR("DMSPlayerShipObject"), sid) : nullptr;
        SpeedMon& sp = g_spds[sid];
        if (!alive(ship)) { sp.started = false; sp.have = false; sp.sid = 0; return; }
        if (sp.sid != sid) { sp = SpeedMon(); sp.sid = sid; }
        // Position for the speed estimate = the ship ACTOR's world position (game space, uu -> raw units). The save object's Location is
        // chunk-relative: crossing a chunk boundary made it jump and the speed safety aborted a healthy approach (v/lim=217).
        double p[3] = {0, 0, 0};
        ActFrame af;
        if (act_frame(ship, af)) { for (int i = 0; i < 3; ++i) p[i] = af.loc[i] / 100.0 * g_raw_per_m; }
        else if (!get_loc(ship, p)) return;
        if (sp.started && dt > 0.0) { sp.v = dist3(p, sp.p) / dt; sp.have = true; }
        std::copy(p, p + 3, sp.p);
        sp.started = true;
        int64_t mmx = 0, mx = 0;
        const bool ok_m = call_pnum(ship, STR("GetModifiedMaxSpeed"), mmx);
        const bool ok_x = call_pnum(ship, STR("GetMaxSpeed"), mx);
        const int64_t src = (ok_m && mmx > 0) ? mmx : (ok_x ? mx : 0);
        sp.lim = static_cast<double>(src) * g_cfg.steer_probe_speed_k;
        if (!sp.have || sp.lim <= 0.0) return;
        const double ratio = sp.v / sp.lim;
        // A reading this far over the limit is a position glitch (floating-origin shift etc.), not real speed: drop the sample
        // instead of aborting the job.
        if (ratio > MV_SPEED_GLITCH) { sp.have = false; return; }
        if (ratio < 1.0) sp.aborted = false;
        if (ratio <= MV_SPEED_ABORT) return;
        zero_all(ship);
        if (!sp.aborted)
        {
            sp.aborted = true;
            tlog(STR("ABORT speed ship=") + std::to_wstring(sid) + STR(" v/lim=") + std::to_wstring(ratio));
            if (mv_active(sid)) mv_finish(sid, STR("error: ABORT speed v/lim=") + std::to_wstring(ratio));
        }
    }

    // WHAT: speed monitor for every ship that needs it: the command ship, every ship with a move job, every ship with a dock job.
    //       Monitor entries of other ships are erased (a stale sample would give a false speed).
    void spd_update(double dt)
    {
        std::set<int> ids;
        if (g_cmd.ship > 0) ids.insert(g_cmd.ship);
        for (const auto& kv : g_mvs) ids.insert(kv.first);
        for (const DockJob& j : g_jobs) ids.insert(j.sid);
        for (int sid : ids) spd_sample(sid, dt);
        for (auto it = g_spds.begin(); it != g_spds.end();) it = ids.count(it->first) ? std::next(it) : g_spds.erase(it);
    }

    // ---------------- aim and rotation ----------------

    // WHAT: aim numbers for face/approach from the ship actor frame and the rock actor (act_aim).
    //       Returns err_deg = heading error (deg), a_body = rotation axis in ship axes, dist_raw = centre distance (raw),
    //       radius = rock radius (raw). false when the rock or the ship frame is unreadable.
    //       Steps: 1 check the rock is alive and read the ship frame. 2 act_aim: angle and axis to the rock.
    //       3 copy the axis out, convert the distance from m to raw (dist_m * g_raw_per_m), read the rock radius (raw).
    // WHY: the aim uses the game's own actor math (uu / 100 = m), so it matches the HUD distances.
    bool aim_calc(UObject* ship, UObject* rock, double& err_deg, double a_body[3], double& dist_raw, double& radius)
    {
        // 1. + 2. rock alive, ship frame readable, angle and rotation axis from the rock's actor location
        ActFrame f;
        ActAim r;
        if (!alive(rock) || !act_frame(ship, f) || !act_aim(f, rock, r)) return false;
        // 3. outputs: heading error, axis, distance in raw units, radius in raw units
        err_deg = r.err_deg;
        std::copy(r.a_body, r.a_body + 3, a_body);
        dist_raw = r.dist_m * g_raw_per_m;
        radius = rock_radius_raw(rock);
        return true;
    }

    // WHAT: face/approach rotation: send the aim offset (view_send) every MV_VIEW_PERIOD_S, from the heading error and the rotation axis.
    //       Steps: 1 accumulate the time since the last send; return until MV_VIEW_PERIOD_S has passed.
    //       2 error < MV_FACE_DONE_DEG: send (0,0) once (aligned), then nothing until the error grows again.
    //       3 otherwise: magnitude mag = min(err + MV_VIEW_DEADZONE_DEG, MV_VIEW_MAX_DEG) (dead zone plus remaining error, clamped).
    //       4 split mag by the unit rotation axis: yaw_deg = mag * (axis . g_view_up), pit_deg = mag * (axis . g_view_pitch).
    //       5 convert to raw (x 65536) with the signs MV_VIEW_*_SIGN. 6 every 10th send (about 1 s): trace line.
    //       7 view_send. A failure ends the job (mv_finish) and returns false.
    // WHY: the offset is a target for the aim cursor relative to the nose (proven route). Projecting on the body axes is what turns
    //      the nose toward the rock. Sending (0,0) when aligned stops the turn.
    // Returns false when the job ended (view_send failed).
    bool view_aim_drive(MoveJob& mv, const double a[3], double err, double dt)
    {
        // 1. resend period
        mv.view_t += dt;
        if (mv.view_t < MV_VIEW_PERIOD_S) return true;
        mv.view_t = 0.0;
        std::wstring e;
        // 2. aligned: (0,0) once, then stay quiet
        if (err < MV_FACE_DONE_DEG)
        {
            if (mv.aligned) return true;
            mv.aligned = true;
            if (!view_send(mv.cid, 0, 0, e)) { mv_finish(mv.sid, STR("error: view_send ") + e); return false; }
            return true;
        }
        mv.aligned = false;
        // 3. magnitude: dead zone + remaining error, clamped
        const double n = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const double mag = std::min(err + MV_VIEW_DEADZONE_DEG, MV_VIEW_MAX_DEG);   // past the dead zone, growing with the error
        // 4. split the magnitude onto the yaw and pitch body axes (projection of the unit rotation axis)
        const double yaw_deg = n > 1e-9 ? mag * dot3(a, g_view_up) / n : 0.0;      // yaw axis component
        const double pit_deg = n > 1e-9 ? mag * dot3(a, g_view_pitch) / n : 0.0;   // pitch axis component
        // 5. deg -> raw, with the fixed signs
        const int64_t yaw_raw = static_cast<int64_t>(std::llround(MV_VIEW_YAW_SIGN * yaw_deg * VIEW_RAW_PER_DEG));
        const int64_t pit_raw = static_cast<int64_t>(std::llround(MV_VIEW_PITCH_SIGN * pit_deg * VIEW_RAW_PER_DEG));
        // 6. trace once per second (every 10th send): shows whether small offsets still turn the ship (dead zone?) and how fast
        static int trace_n = 0;
        if (++trace_n % 10 == 0)
            tlog(STR("aim err=") + std::to_wstring(err) + STR(" offset yaw=") + std::to_wstring(yaw_deg) + STR(" pitch=") + std::to_wstring(pit_deg) +
                 STR(" rate_deg_s=") + std::to_wstring(mv.rate));
        // 7. send
        if (!view_send(mv.cid, yaw_raw, pit_raw, e)) { mv_finish(mv.sid, STR("error: view_send ") + e); return false; }
        return true;
    }

    // WHAT: one step of a move job: phase 1 = aim (rotate until aligned), phase 2 = approach (Throttle profile).
    //       Steps: 1 re-check the rock and get the aim numbers; an unreadable rock ends the job ("rock lost").
    //       2 store err in g_mv.err and g_last_err. 3 phase 1 ends: face_rock when err < MV_FACE_DONE_DEG (ok) or after
    //       MV_FACE_TIMEOUT_S (error); approach_rock enters phase 2 when err < MV_APPROACH_START_DEG. 4 rotation (view_aim_drive)
    //       every step. 5 phase 2 only: timeout MV_APPROACH_TIMEOUT_S. 6 surface distance (m) and stop distance mining_distance (m).
    //       7 remaining = surface - stop distance (m). 8 remaining <= 0: brake (reverse step while speed >= 1 m/s), then Throttle 0 and the job ends ok.
    //       9 remaining > 0: target step = 0 if heading error > 10 deg, else floor(remaining / 25 m) clamped to 1..min(10, steps).
    //         The applied step rises by 1 at most every 0.5 s and falls straight to the target. 10 write Throttle; log every ~1 s.
    // WHY: the ship only approaches when it is roughly aligned; the Throttle slows down step by step, so the stop is smooth and the speed
    //      safety (spd_update) is not triggered. Stop on SURFACE distance (user choice, Update 28).
    //      Caveat (ported as is): when no speed sample exists (spd_of(sid).have false) the speed is taken as 0, so a brake ends at once.
    void move_aim_step(MoveJob& mv, UObject* ship, double dt)
    {
        const int sid = mv.sid;
        // 1. rock still valid, aim numbers
        double a[3] = {0, 0, 0}, dist = 0.0, rad = 0.0, err = 0.0;
        if (!actor_ok(mv.rock_act) || !aim_calc(ship, mv.rock_act, err, a, dist, rad)) { mv_finish(sid, STR("error: rock lost")); return; }
        // 2. remember the heading error (job state and status; the status shows the command ship only)
        mv.err = err;
        if (sid == g_cmd.ship) g_last_err = err;
        // WHY no sign-flip here any more: view offsets are targets with a fixed sign (MV_VIEW_*_SIGN), the old rate-route flip is gone
        const bool approach = mv.kind == "approach_rock";
        // 3. phase 1 (aim) ends
        if (mv.phase == 1)
        {
            if (!approach && err < MV_FACE_DONE_DEG) { mv_finish(sid, STR("ok heading_error=") + std::to_wstring(err) + STR("deg")); return; }
            if (!approach && mv.pt >= MV_FACE_TIMEOUT_S) { mv_finish(sid, STR("error: face timeout err=") + std::to_wstring(err)); return; }
            if (approach && err < MV_APPROACH_START_DEG) { mv.phase = 2; mv.pt = 0.0; tlog(STR("approach phase, err=") + std::to_wstring(err)); }
        }
        // 4. rotation runs in both phases
        if (!view_aim_drive(mv, a, err, dt)) return;   // rotation via view_send
        if (mv.phase != 2) return;
        // 5. approach timeout
        if (mv.t >= MV_APPROACH_TIMEOUT_S) { mv_finish(sid, STR("error: approach timeout")); return; }
        // 6. surface distance (m) and the per-ship stop distance (m)
        const double surface = (dist - rad) / g_raw_per_m;
        const double md = parse_distance(eff_cfg(sid).mining_distance);
        if (md < 0.0) { mv_finish(sid, STR("error: mining_distance unset")); return; }
        // Approach Throttle profile. WHY: remaining = surface - mining distance (m). Target step = floor(remaining / 25 m) clamped to
        // 1..min(10, steps): full speed above 250 m, linear slow-down to step 1 near the stop distance. Heading error > MV_APPROACH_HEAD_DEG -> 0.
        // Applied step rises by 1 at most every MV_THR_RAMP_S (gradual accel, keeps the 1.1x speed abort far away); it falls straight to target.
        // Past the stop distance (overshoot): reverse step MV_BRAKE_THR while speed >= MV_BRAKE_DONE_MS, then 0 and the job ends.
        // 7. remaining distance (m) and speed (m/s)
        const double remaining = surface - md;
        const SpeedMon& sp = spd_of(sid);
        const double v_ms = sp.have ? sp.v / g_raw_per_m : 0.0;
        int target = 0;
        // 8. at or past the stop distance: brake until nearly stopped, then finish
        if (remaining <= 0.0)
        {
            if (v_ms < MV_BRAKE_DONE_MS)
            {
                set_int_prop(ship, STR("Throttle"), 0);
                mv_finish(sid, STR("ok surface_m=") + std::to_wstring(surface) + STR(" speed=") + std::to_wstring(v_ms) + STR(" m/s"));
                return;
            }
            target = MV_BRAKE_THR;
            mv.thr = MV_BRAKE_THR;
            mv.thr_t = 0.0;
        }
        else
        {
            // 9. still before the stop distance: ramped forward step toward the target
            if (mv.thr < 0) mv.thr = 0;   // only if pushed back out after a brake: restart forward from 0
            const int cap = std::min(MV_APPROACH_MAX_THR, mv.steps);
            target = err > MV_APPROACH_HEAD_DEG ? 0 : std::clamp(static_cast<int>(std::floor(remaining / MV_APPROACH_M_PER_STEP)), 1, cap);
            if (target > mv.thr)
            {
                mv.thr_t += dt;
                if (mv.thr_t >= MV_THR_RAMP_S) { mv.thr_t = 0.0; ++mv.thr; }
            }
            else { mv.thr = target; mv.thr_t = 0.0; }
        }
        // 10. write the Throttle step; log about once per second
        set_int_prop(ship, STR("Throttle"), mv.thr);
        if (mv.log_acc >= 1.0)
        {
            mv.log_acc = 0.0;
            tlog(STR("approach surface_m=") + std::to_wstring(surface) + STR(" md=") + std::to_wstring(md) + STR(" remaining_m=") + std::to_wstring(remaining) +
                 STR(" err=") + std::to_wstring(err) + STR(" target=") + std::to_wstring(target) + STR(" thr=") + std::to_wstring(mv.thr) +
                 STR(" speed_m_s=") + std::to_wstring(v_ms));
        }
    }

    // WHAT: move job tick, every engine tick while a job runs: re-find the ship by id, read the quaternion (turn rate every 0.25 s),
    //       advance the job timers, then call move_aim_step.
    // WHY: the ship and the rock are re-resolved on every tick and never kept, so a freed object is never used (crash lesson).
    void move_tick(double dt)
    {
        std::vector<int> ids;
        for (const auto& kv : g_mvs) ids.push_back(kv.first);   // copy: a finish erases its entry
        for (int sid : ids)
        {
            auto it = g_mvs.find(sid);
            if (it == g_mvs.end() || it->second.kind.empty()) continue;
            MoveJob& mv = it->second;
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(ship)) { mv_finish(sid, STR("error: ship not found during move")); continue; }
            double q[4] = {1, 0, 0, 0};
            if (!quat4(ship, q)) { mv_finish(sid, STR("error: Quat unreadable")); continue; }
            mv.t += dt;
            mv.pt += dt;
            mv.samp += dt;
            mv.log_acc += dt;
            if (mv.samp >= MV_SAMPLE_S)
            {
                mv.rate = quat_angle_deg(mv.qs, q) / mv.samp;
                std::copy(q, q + 4, mv.qs);
                mv.samp = 0.0;
            }
            move_aim_step(mv, ship, dt);
        }
    }

    // WHAT: start a face_rock or approach_rock job. Returns "waiting" when started, else the error text. Only one move job at a time.
    //       Steps: 1 refuse if a move job runs or a dock job runs for this ship. 2 find the ship. 3 read the Throttle step count
    //       (GetThrottleSteps). 4 find the PilotConsoleObject and cache its id. 5 pick the rock (rock_sel, else nearest of INI resource).
    //       6 approach_rock needs mining_distance set. 7 record the start quaternion, set phase 1 (aim), log the start.
    // WHY: all preconditions are checked here before the first write, so a failed start changes nothing on the ship.
    std::wstring move_start(int sid, const std::string& cmd, int seq)
    {
        // 1. one move job at a time, and no dock job for the same ship
        if (mv_active(sid)) return STR("error: move job already running");
        for (const DockJob& j : g_jobs) if (j.sid == sid) return STR("error: dock job already running for this ship");
        // 2. ship
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) return STR("error: ship not found");
        MoveJob m;
        m.kind = cmd;
        m.sid = sid;
        m.seq = seq;
        std::wstring pick_rule;   // which pick_rock rule chose the rock (start log)
        // 3. throttle steps
        call_int(ship, STR("GetThrottleSteps"), m.steps);
        // 4. console id (rotation = view_send; cached for the whole job)
        std::wstring how;
        size_t pcount = 0;
        UObject* console = pilot_console_of(sid, how, pcount);
        if (!alive(console)) return STR("error: PilotConsoleObject not found");
        m.cid = obj_id(console);
        if (m.steps < 1) return STR("error: Throttle steps unreadable");
        // 5. rock
        std::wstring why;
        UObject* rock = pick_rock(sid, ship, why, pick_rule);   // rock_sel > nearest rock actor of the INI item (game distance)
        if (!alive(rock)) return STR("error: ") + (why.empty() ? STR("no rock") : why);
        m.rock_act = rock;
        m.rock_id = rock_id_of(rock);
        m.rock_name = rock_kind(rock);
        // 6. approach needs the stop distance
        if (cmd == "approach_rock" && parse_distance(eff_cfg(sid).mining_distance) < 0.0) return STR("error: mining_distance unset");
        // 7. start: quaternion for the rate sample, phase 1 (aim)
        quat4(ship, m.qs);
        // WHY: aiming starts at once (no forward-axis test phase before it)
        m.phase = 1;
        if (sid == g_cmd.ship) g_last_err = -1.0;
        g_mvs[sid] = m;
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
