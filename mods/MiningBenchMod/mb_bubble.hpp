// FILE: mods/MiningBenchMod/mb_bubble.hpp
// WHAT: bubble reflex for every manual movement of a ship (our steering, not the game autopilot). Each 0.25 s tick, every bench ship in
//       SEEK / MINE / COLLECT / SWEEP is checked (one registry first_blocker per ship). Activation and phase: mb_logic.hpp (bubble_why, bubble_phase).
//       Look-ahead L = max(100 m, 4 s x speed) along the nose; a rock blocks when inside the bubble (ship CollisionRadius + rock radius + 30 m).
//       The target rock is left out only in the stand-off phase of a rock approach or mining (SEEK / MINE).
//       Ladder (bubble_step): BRAKE = Throttle 0 (rotation kept) until speed < 3 m/s; then the fix of the phase (bubble_fix):
//         SEEK: replan = path_guard (blocker in the path skip); skip = path_give_up after 2 replans.
//         WAYPOINT / ORE: waypoint beside the blocker (max BUBBLE_MAX_WP per flight), the flight is flown again after it; else skip
//                         (SWEEP leg: sw_nofly + SW_RETURN; ore: SwOre marked done).
//         MINE (drift): brake only; nudge BUBBLE_NUDGE_M away from the rock when too near it, else resume.
// WHY: the path check runs before an approach and every PATH_WATCH_S; a rock that comes into view between checks was hit at full speed.
// STATE: g_bubble (mb_steer.hpp), one BubbleState per ship. move_aim_step holds Throttle 0 while braking (bubble_braking).
// BUDGET: one ship pass <= BUBBLE_SLOW_MS (0.2 ms). Over it: 'BUBBLE slow' (once per 30 s) and that ship is not checked for 10 s
//         (never while it is braking: its pending fix must run).
// ORDER: bubble_tick runs in cmd_tick BEFORE mining_tick, so a finished waypoint is flown again before the state machine reads the job.
// OUTPUT: log 'BUBBLE brake ship= phase= rock= dist= rad= speed= -> replan|waypoint|skip|nudge|resume', 'BUBBLE slow ship= ms='.
//         Dev: bubble_status:<ship> and bubble_sim:<ship>:<rock_id> (read-only), both write MiningBubble.txt (mb_devcmd.hpp dispatch).
// DEPENDS ON (exact, include order): mb_registry.hpp (reg_st, RockReg, reg_eff_rad, reg_abs_m), mb_mining.hpp (MineSt, g_mine, g_bench_ids, standoff_of,
//          path_guard, path_give_up, rock_actor_by_id, item_c, dock_add_m, sw_phase, SwOre), mb_steer.hpp (spd_of, g_bubble, g_mvs, move_start, mv_cancel),
//          mb_logic.hpp (bubble_*).
// USED BY: mb_testcmd.hpp (cmd_tick, bubble_tick), mb_devcmd.hpp (bubble_dev).

namespace
{
    // WHAT: a ship whose bubble pass was slow is not checked this long (ms); the slow log line is written at most this often (ms).
    constexpr ULONGLONG BUBBLE_OFF_MS = 10000;
    constexpr ULONGLONG BUBBLE_SLOW_LOG_MS = 30000;
    constexpr double BUBBLE_ARRIVE_M = 30.0;   // a resumed flight is done when the ship is this close to its destination (m)

    std::map<int, ULONGLONG> g_bubble_off;   // sid -> GetTickCount64 until which the ship is not checked (slow pass)
    ULONGLONG g_bubble_slow_ms = 0;          // GetTickCount64 of the last BUBBLE slow line (0 = none yet)

    // WHAT: Euclidean distance between two points (m).
    double bub_dist(const double a[3], const double b[3])
    {
        const double d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    }

    // WHAT: collision radius (m) of ship sid, read once (CollisionRadius, raw per m). BUBBLE_SHIP_R_M until it is read.
    double bubble_ship_r(int sid)
    {
        static std::map<int, double> cache;
        auto it = cache.find(sid);
        if (it != cache.end()) return it->second;
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        int64_t raw = 0;
        if (!alive(ship) || !pnum_prop(ship, STR("CollisionRadius"), raw) || raw <= 0) return BUBBLE_SHIP_R_M;
        const double r = static_cast<double>(raw) / g_raw_per_m;
        cache[sid] = r;
        return r;
    }

    // WHAT: game flags of a ship: autopilot, docking autopilot, travelling (warp). Read-only.
    struct BubFlags { bool ap = false, dap = false, trav = false; };
    BubFlags bubble_flags(UObject* ship)
    {
        BubFlags f;
        get_bool_prop(ship, STR("bAutopilot"), f.ap);
        get_bool_prop(ship, STR("bDockingAutopilot"), f.dap);
        get_bool_prop(ship, STR("bTravelling"), f.trav);
        return f;
    }

    // WHAT: the rock to leave out of the query: the ship target in its stand-off phase (SEEK / MINE only). 0 = none.
    int bubble_skip_id(int sid, BubblePhase ph, const double p[3], int rock)
    {
        if ((ph != BPH_SEEK && ph != BPH_MINE) || rock <= 0) return 0;
        double so[3], tc[3], tr = 0.0;
        if (!standoff_of(sid, rock, p, so, tc, tr)) return 0;
        return bub_dist(p, so) <= APPROACH_RAMP_M ? rock : 0;
    }

    // WHAT: the first blocker on the nose of ship sid for look-ahead speed (registry first_blocker). skip_id is left out (0 = none).
    //       Returns the blocker id, 0 = clear. out_d = blocker distance to the nose line (m); out_r = its radius (m).
    int bubble_query(int sid, UObject* ship, const ActFrame& f, double speed, int skip_id, double& out_d, double& out_r)
    {
        const double p[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
        const double L = bubble_look_ahead(speed);
        const double b[3] = {p[0] + f.fw[0] * L, p[1] + f.fw[1] * L, p[2] + f.fw[2] * L};
        reg_area_seeded(sys_id(ship), p);   // unseeded area: the look-ahead below sees the seeded rocks only (logged once per pass)
        double cc[3] = {0, 0, 0};
        return seg_blocker(sys_id(ship), p, b, skip_id, bubble_clear_m(bubble_ship_r(sid), g_cfg.bubble_clearance_m), false, out_d, cc, out_r);
    }

    // WHAT: destination (m) of the current flight of a move job: point target or floating ore. false when the job has neither.
    bool flight_dst(const MoveJob& mv, double out[3])
    {
        if (mv.pt_on) { reg_abs_m(mv.ptg.ch, mv.ptg.loc, g_raw_per_m, out); return true; }
        if (mv.ore_on) { for (int i = 0; i < 3; ++i) out[i] = mv.ore_loc[i] / MV_UU_PER_M; return true; }
        return false;
    }

    // WHAT: flies ship sid to wp (m) with a point move job (the approach move kind, as path_guard does). false when the job did not start.
    bool bub_move_to(int sid, UObject* ship, const double wp[3])
    {
        ActFrame f;
        if (!act_frame(ship, f)) return false;
        const int sys = sys_id(ship);
        int64_t ch[3] = {0, 0, 0};
        double raw[3] = {0, 0, 0};
        if (sys <= 0 || !get_chunk(ship, ch) || !get_loc(ship, raw)) return false;
        const double ps[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
        const double off[3] = {wp[0] - ps[0], wp[1] - ps[1], wp[2] - ps[2]};
        PtTarget pt;
        pt.sys = sys;
        dock_add_m(ch, raw, off, pt.ch, pt.loc);
        mv_cancel(sid); zero_all(ship);
        return move_start(sid, "approach_rock", -1, nullptr, &pt) == STR("waiting");
    }

    // WHAT: nearest rock of ship (registry, surface). near = its centre closer than bubble_too_near. c / r = its centre (m) and radius (m).
    bool bub_near_rock(UObject* ship, const double p[3], double cr, double c[3], double& r)
    {
        double surf = 0.0;
        const int id = nearest_rock_any_id(ship, surf, 0);
        if (id <= 0 || !reg_geom(id, c, r)) return false;
        return bubble_too_near(bub_dist(p, c), cr, r);
    }

    // WHAT: carries out the fix of a ladder event (SEEK replan / skip, waypoint, skip, nudge, or brake only). ph = bs.phase of the hit.
    void bub_exec(int sid, UObject* ship, MineSt& m, BubbleState& bs, int act, const double p[3], double cr)
    {
        double nc[3] = {0, 0, 0}, nr = 0.0;
        const bool nrock = bub_near_rock(ship, p, cr, nc, nr);
        const BubbleFix fix = bubble_fix(static_cast<BubblePhase>(bs.phase), act, bs.wp, nrock);
        const auto mj = g_mvs.find(sid);
        const MoveJob cur = mj != g_mvs.end() ? mj->second : MoveJob();
        if (fix == BF_REPLAN)
        {
            if (bs.blk > 0) g_path_skip[sid].insert(bs.blk);   // the blocker is never picked as a target for this visit
            Config c = eff_cfg(sid);
            std::wstring why;
            UObject* item = item_c(c.resource, why);
            UObject* tg = m.rock > 0 ? rock_actor_by_id(m.rock) : nullptr;
            if (tg && m.st == MS_SEEK) path_guard(ship, sid, m, c, tg, item);
            return;
        }
        if (fix == BF_WAYPOINT)
        {
            double dst[3] = {0, 0, 0}, bc[3] = {0, 0, 0}, br = 0.0, wp[3] = {0, 0, 0};
            if (flight_dst(cur, dst) && reg_geom(bs.blk, bc, br))
            {
                path_waypoint(p, dst, bc, block_r_m(br, bubble_clear_m(cr, g_cfg.bubble_clearance_m)), wp);
                if (bub_move_to(sid, ship, wp))
                {
                    ++bs.wp;
                    bs.resume = true;
                    std::copy(dst, dst + 3, bs.dst);
                    return;
                }
            }
            bs.resume = false;   // no waypoint possible: skip below
        }
        if (fix == BF_NUDGE)
        {
            double away[3] = {p[0] - nc[0], p[1] - nc[1], p[2] - nc[2]};
            const double n = std::max(1e-9, bub_dist(p, nc));
            double wp[3];
            for (int i = 0; i < 3; ++i) wp[i] = p[i] + away[i] / n * BUBBLE_NUDGE_M;
            bub_move_to(sid, ship, wp);
            bs.resume = false;
            return;
        }
        if (fix == BF_RESUME || fix == BF_NONE) { bs.resume = false; return; }
        // SKIP (SEEK: give the target up; waypoint / ore: the flight is dropped)
        if (bs.phase == BPH_SEEK && m.rock > 0) { path_give_up(ship, sid, m, m.rock, bs.blk, bs.blk_d); bs.resume = false; return; }
        if (bs.phase == BPH_ORE && cur.ore_on)
            for (SwOre& o : m.sw_list) if (o.a == cur.rock_act) o.done = true;   // SWEEP: the ore is unreachable for this run
        bs.resume = false;
        mv_cancel(sid); zero_all(ship);
        if (m.st == MS_SWEEP && (bs.phase == BPH_WAYPOINT || bs.phase == BPH_ORE))
        {
            m.sw_nofly = true;   // SW_RETURN: back by warp instead of this leg
            sw_phase(ship, sid, m, SW_RETURN);
        }
    }

    // WHAT: one bubble pass for ship sid in phase ph: query, ladder step, and the fix of the step.
    void bubble_one(int sid, MineSt& m, BubblePhase ph, UObject* ship)
    {
        ActFrame f;
        if (!act_frame(ship, f)) return;
        BubbleState& bs = g_bubble[sid];
        const SpeedMon& sp = spd_of(sid);
        const double v = sp.have ? sp.v / g_raw_per_m : 0.0;
        const double cr = bubble_ship_r(sid);
        const double p[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
        double d = 0.0, rad = 0.0;
        const int bid = bubble_query(sid, ship, f, v, bubble_skip_id(sid, ph, p, m.rock), d, rad);
        if (bid) { bs.blk = bid; bs.blk_d = d; if (!bs.braking) bs.phase = ph; }
        if (bs.rock != m.rock) bs.wp = 0;   // bubble_step resets the count on a new rock too
        const BubbleAct act = bubble_step(bs, bid != 0, v, m.rock);
        if (act == BBA_BRAKE)
        {
            set_int_prop(ship, STR("Throttle"), 0);
            double nc[3] = {0, 0, 0}, nr = 0.0;
            const bool nrock = bub_near_rock(ship, p, cr, nc, nr);
            const BubbleFix fix = bubble_fix(static_cast<BubblePhase>(bs.phase), bs.pending, bs.wp, nrock);
            log_line(STR("BUBBLE brake ship=") + std::to_wstring(sid) + STR(" phase=") + widen(bubble_phase_name(static_cast<BubblePhase>(bs.phase))) +
                     STR(" rock=") + std::to_wstring(bid) + STR(" dist=") + std::to_wstring(static_cast<long long>(std::llround(d))) +
                     STR(" rad=") + std::to_wstring(static_cast<long long>(std::llround(rad))) + STR(" speed=") + std::to_wstring(v) +
                     STR(" -> ") + widen(bubble_fix_name(fix)));
        }
        else if (act == BBA_REPLAN || act == BBA_RETARGET) bub_exec(sid, ship, m, bs, act, p, cr);
    }

    // WHAT: bubble tick (cmd_tick 0.25 s block, before mining_tick). Every bench ship in SEEK / MINE / COLLECT / SWEEP gets one check per tick
    //       when it moves by our steering or faster than BUBBLE_BRAKE_V. Game autopilot, docking autopilot and warp travel are never touched.
    void bubble_tick(double dt)
    {
        (void)dt;
        const ULONGLONG now = GetTickCount64();
        for (int sid : g_bench_ids)
        {
            auto it = g_mine.find(sid);
            if (it == g_mine.end()) continue;
            MineSt& m = it->second;
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            const bool state_ok = m.st == MS_SEEK || m.st == MS_MINE || m.st == MS_COLLECT || m.st == MS_SWEEP;
            if (!state_ok || !alive(ship) || (m.st == MS_SWEEP && m.sw_ph == SW_WARP)) { g_bubble.erase(sid); continue; }
            BubbleState& bs = g_bubble[sid];
            // a flown waypoint ends: fly again to the flight destination, unless the ship is there
            if (bs.resume && !mv_active(sid))
            {
                double ps[3] = {0, 0, 0};
                ActFrame f;
                if (act_frame(ship, f)) for (int i = 0; i < 3; ++i) ps[i] = f.loc[i] / MV_UU_PER_M;
                if (bub_dist(ps, bs.dst) <= BUBBLE_ARRIVE_M || !bub_move_to(sid, ship, bs.dst)) bs.resume = false;
            }
            const BubFlags fl = bubble_flags(ship);
            auto mj = g_mvs.find(sid);
            const MoveJob* mv = (mj != g_mvs.end() && !mj->second.kind.empty()) ? &mj->second : nullptr;
            const bool thrust = mv && mv->kind == "approach_rock";
            const SpeedMon& sp = spd_of(sid);
            const double v = sp.have ? sp.v / g_raw_per_m : 0.0;
            if (mv && mv->warp_wait) continue;   // far-trip warp in progress: the teleport is not ours to brake
            const BubbleWhy why = bubble_why(fl.ap, fl.dap, fl.trav, thrust, v);
            bs.why = why;
            if (why == BW_AUTOPILOT || why == BW_DOCK_AUTOPILOT || why == BW_TRAVEL) { g_bubble.erase(sid); continue; }
            if (why == BW_IDLE && !bs.braking) { if (!bs.resume) bs.wp = 0; continue; }
            if (!bs.braking && !bs.resume && !mv) bs.wp = 0;
            auto off = g_bubble_off.find(sid);
            if (!bs.braking && off != g_bubble_off.end() && now < off->second) continue;   // slow pass: paused (a braking ship always finishes)
            const BubblePhase ph = bs.braking ? static_cast<BubblePhase>(bs.phase)
                                              : bubble_phase(thrust, mv && mv->pt_on, mv && mv->ore_on, !thrust);
            LARGE_INTEGER t0;
            QueryPerformanceCounter(&t0);
            bubble_one(sid, m, ph, ship);
            const double ms = perf_ms_since(t0);
            if (ms > BUBBLE_SLOW_MS)
            {
                g_bubble_off[sid] = now + BUBBLE_OFF_MS;
                if (g_bubble_slow_ms == 0 || now - g_bubble_slow_ms >= BUBBLE_SLOW_LOG_MS)
                {
                    g_bubble_slow_ms = now;
                    log_line(STR("BUBBLE slow ship=") + std::to_wstring(sid) + STR(" ms=") + std::to_wstring(ms));
                }
            }
        }
    }

    // WHAT: bubble_status:<ship> and bubble_sim:<ship>:<rock_id> (Dev cmds). Read-only: no game write. Writes MiningBubble.txt; returns one line.
    //       status: activation (why), phase, pose, speed, look-ahead, ladder state, blocker now, the 10 nearest rocks.
    //       sim: the rocks that would block a straight SEEK flight from the ship to rock_id (target excluded), in order, and the first ladder step.
    std::wstring bubble_dev(const std::string& cmd)
    {
        const bool sim = cmd.rfind("bubble_sim:", 0) == 0;
        const std::string rest = cmd.substr(sim ? 11 : 14);
        const int sid = std::atoi(rest.c_str());
        int rock = 0;
        if (sim) { const size_t c = rest.find(':'); if (c != std::string::npos) rock = std::atoi(rest.c_str() + c + 1); }
        if (sid <= 0) return STR("error: bubble ship id");
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        ActFrame f;
        if (!alive(ship) || !act_frame(ship, f)) return STR("error: ship not found");
        const RockReg& t = reg_st().tab;
        const double p[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
        const double cr = bubble_ship_r(sid);
        const SpeedMon& sp = spd_of(sid);
        const double v = sp.have ? sp.v / g_raw_per_m : 0.0;
        const int sys = sys_id(ship);
        const double L = bubble_look_ahead(v);
        const double b[3] = {p[0] + f.fw[0] * L, p[1] + f.fw[1] * L, p[2] + f.fw[2] * L};
        char buf[400];
        std::string out;
        if (!sim)
        {
            int32_t thr = 0;
            get_int_prop(ship, STR("Throttle"), thr);
            auto mi = g_mine.find(sid);
            const int trock = mi != g_mine.end() ? mi->second.rock : 0;
            auto mj = g_mvs.find(sid);
            const MoveJob* mv = (mj != g_mvs.end() && !mj->second.kind.empty()) ? &mj->second : nullptr;
            const bool thrust = mv && mv->kind == "approach_rock";
            const BubFlags fl = bubble_flags(ship);
            const BubbleWhy why = bubble_why(fl.ap, fl.dap, fl.trav, thrust, v);
            const BubblePhase ph = bubble_phase(thrust, mv && mv->pt_on, mv && mv->ore_on, !thrust);
            std::snprintf(buf, sizeof(buf), "ship=%d sys=%d pos=(%.1f,%.1f,%.1f) m speed=%.2f m/s throttle=%d ship_r=%.1f m look_ahead=%.1f m clear=%.1f m\r\n",
                          sid, sys, p[0], p[1], p[2], v, static_cast<int>(thr), cr, L, bubble_clear_m(cr, g_cfg.bubble_clearance_m));
            out += buf;
            std::snprintf(buf, sizeof(buf), "active=%s\r\nwhy=%s\r\nphase=%s state=%s move_job=%s pt=%d ore=%d target=%d\r\n",
                          why == BW_ACTIVE ? "1" : "0", bubble_why_name(why), bubble_phase_name(ph),
                          mi != g_mine.end() ? u8(mine_state_name(mi->second.st)).c_str() : "none",
                          mv ? mv->kind.c_str() : "none", mv && mv->pt_on ? 1 : 0, mv && mv->ore_on ? 1 : 0, trock);
            out += buf;
            std::snprintf(buf, sizeof(buf), "nose=(%.3f,%.3f,%.3f)\r\n", f.fw[0], f.fw[1], f.fw[2]);
            out += buf;
            const auto bs = g_bubble.find(sid);
            std::snprintf(buf, sizeof(buf), "ladder braking=%d replans=%d wp=%d resume=%d blk=%d\r\n", bs != g_bubble.end() && bs->second.braking ? 1 : 0,
                          bs != g_bubble.end() ? bs->second.replans : 0, bs != g_bubble.end() ? bs->second.wp : 0,
                          bs != g_bubble.end() && bs->second.resume ? 1 : 0, bs != g_bubble.end() ? bs->second.blk : 0);
            out += buf;
            double d = 0.0, rad = 0.0;
            const int bid = bubble_query(sid, ship, f, v, bubble_skip_id(sid, ph, p, trock), d, rad);
            std::snprintf(buf, sizeof(buf), "blocker now: %s\r\n",
                          bid ? (std::to_string(bid) + " dist=" + std::to_string(d) + " rad=" + std::to_string(rad)).c_str() : "none");
            out += buf;
            std::vector<std::pair<double, size_t>> cands;
            t.nearest_n(sys, nullptr, p, nullptr, nullptr, 20000.0, 10, cands);
            for (const auto& pr : cands)
            {
                const RegRock& rk = t.rocks[pr.second];
                const double dv[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                const double cd = std::max(1e-9, pr.first);
                const double cosb = (f.fw[0] * dv[0] + f.fw[1] * dv[1] + f.fw[2] * dv[2]) / cd;
                const double bearing = std::acos(std::clamp(cosb, -1.0, 1.0)) * 180.0 / 3.14159265358979;
                double pd = 0.0;
                const double rr = block_r_m(reg_eff_rad(rk), bubble_clear_m(cr, g_cfg.bubble_clearance_m));
                const bool in_bubble = seg_sphere_blocks(p, b, rk.c, rr, pd);
                std::snprintf(buf, sizeof(buf), "rock=%d dist=%.1f surf=%.1f rad=%.1f bearing=%.1f ahead=%d in_bubble=%d\r\n", rk.id, cd,
                              cd - reg_eff_rad(rk), reg_eff_rad(rk), bearing, cosb > 0.0 ? 1 : 0, in_bubble ? 1 : 0);
                out += buf;
            }
        }
        else
        {
            const RegRock* tr = t.get(rock);
            if (!tr) return STR("error: rock not in registry");
            const double bt[3] = {tr->c[0], tr->c[1], tr->c[2]};
            struct Hit { double t; int id; double d; double rad; };
            std::vector<Hit> hits;
            const double lo[3] = {std::min(p[0], bt[0]) - PATH_RMAX_M, std::min(p[1], bt[1]) - PATH_RMAX_M, std::min(p[2], bt[2]) - PATH_RMAX_M};
            const double hi[3] = {std::max(p[0], bt[0]) + PATH_RMAX_M, std::max(p[1], bt[1]) + PATH_RMAX_M, std::max(p[2], bt[2]) + PATH_RMAX_M};
            const double ll = std::max(1e-12, bub_dist(p, bt) * bub_dist(p, bt));
            t.for_box(sys, lo, hi, [&](size_t i)
            {
                const RegRock& rk = t.rocks[i];
                if (!rk.alive || rk.sys != sys || rk.id == rock) return;
                const double rr = block_r_m(reg_eff_rad(rk), bubble_clear_m(cr, g_cfg.bubble_clearance_m));
                double pd = 0.0;
                if (!seg_sphere_blocks(p, bt, rk.c, rr, pd)) return;
                const double w[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                const double dd[3] = {bt[0] - p[0], bt[1] - p[1], bt[2] - p[2]};
                hits.push_back({(w[0] * dd[0] + w[1] * dd[1] + w[2] * dd[2]) / ll, rk.id, pd, reg_eff_rad(rk)});
            });
            std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.t < y.t; });
            std::snprintf(buf, sizeof(buf), "sim ship=%d rock=%d speed=%.2f m/s phase=seek (straight flight to the rock) blockers=%zu\r\n", sid, rock, v, hits.size());
            out += buf;
            for (const Hit& h : hits)
            {
                std::snprintf(buf, sizeof(buf), "blocker=%d t=%.3f dist=%.1f rad=%.1f\r\n", h.id, h.t, h.d, h.rad);
                out += buf;
            }
            std::snprintf(buf, sizeof(buf), "ladder: %s\r\n",
                          hits.empty() ? "clear, the flight would not be stopped" : ("first blocker " + std::to_string(hits[0].id) + ": brake, then replan (0 of 2)").c_str());
            out += buf;
        }
        try { write_atomic(get_dll_dir() / "MiningBubble.txt", out); } catch (...) {}
        return (sim ? STR("ok bubble_sim ship=") : STR("ok bubble_status ship=")) + std::to_wstring(sid) + STR(" (see MiningBubble.txt)");
    }
}
