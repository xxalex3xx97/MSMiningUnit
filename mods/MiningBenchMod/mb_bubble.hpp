// FILE: mods/MiningBenchMod/mb_bubble.hpp
// WHAT: mining's side of the bubble reflex. ShipControl runs the reflex (SC3): every ship with a move job or a bubble state is checked each
//       tick (sc_bubble_run.hpp), the throttle is held on a hit (BRAKE), and the fix (replan, waypoint, skip, nudge, resume) is picked from
//       sc_logic_bubble.hpp. Mining answers ShipControl's host callbacks (sc_host_* below: first_blocker, bubble_profile, near_rock, on_bubble),
//       reads the ladder status (bub_ladder) and runs the bubble_status / bubble_sim dev commands.
// WHY: one reflex for every move (ShipControl). Mining keeps only the mining facts: the rock registry, the job state and the target rock.
// STATE: ShipControl holds one BubbleState per ship (sc_bubble.hpp). Mining holds no bubble state.
// SHIP READS: ship flight state (flags, Throttle, CollisionRadius) through ms_flight_read (MSCore API 10, sc_flight_abi.h).
// BUDGET (ShipControl, sc_bubble_run.hpp): one ship pass <= BUBBLE_SLOW_MS (0.2 ms). Over it: 'BUBBLE slow' (once per 30 s) and that ship
//         is not checked for 10 s (never while it is braking: its pending fix must run).
// ORDER: bubble_tick runs in cmd_tick BEFORE mining_tick, so a finished waypoint is flown again before the state machine reads the job.
// OUTPUT (ShipControl): log 'BUBBLE brake ship= phase= rock= ... -> replan|waypoint|skip|nudge|resume', 'BUBBLE slow ship= ms='.
//         Dev (mining): bubble_status:<ship> and bubble_sim:<ship>:<rock_id> (read-only), both write MiningBubble.txt (mb_devcmd.hpp dispatch).
// DEPENDS ON (exact, include order): mb_registry.hpp (reg_state, reg_get, reg_box, reg_nearest_n, reg_eff_rad, reg_abs_m), mb_mining.hpp (MineSt, g_mine, g_bench_ids, standoff_of,
//          path_guard, path_give_up, rock_actor_by_id, item_c, dock_add_m, sw_phase, SwOre), mb_steer.hpp (g_mvs, move_start, mv_cancel),
//          mb_logic.hpp (bubble_*).
// USED BY: mb_testcmd.hpp (cmd_tick, bubble_tick), mb_devcmd.hpp (bubble_dev).

namespace
{
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
        MsFlight f;
        const bool ok = ms_flight_read(sid, FL_STATE, f) == 1 && (f.flags & FF_COLL) != 0;
        const double r = ship_r_from_flight(ok, f.coll_radius_raw);
        if (ok) cache[sid] = r;
        return r;
    }

    // WHAT: game flags of a ship: autopilot, docking autopilot, travelling (warp). Read-only, one FL_STATE flight read.
    struct BubFlags { bool ap = false, dap = false, trav = false; };
    BubFlags bubble_flags(int sid)
    {
        BubFlags b;
        MsFlight f;
        if (ms_flight_read(sid, FL_STATE, f) != 1) return b;
        b.ap = (f.flags & FF_AUTOPILOT) != 0;
        b.dap = (f.flags & FF_DOCK_AP) != 0;
        b.trav = (f.flags & FF_TRAVELLING) != 0;
        return b;
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
        return seg_blocker(sys_id(ship), p, b, skip_id, bubble_clear_m(bubble_ship_r(sid), g_cfg.bubble_clearance_m), false, out_d, cc, out_r, false, sid);
    }

    // WHAT: destination (m) of the current flight of a move job: point target or floating ore. false when the job has neither.
    bool flight_dst(const MoveJob& mv, double out[3])
    {
        if (mv.pt_on) { reg_abs_m(mv.ptg.ch, mv.ptg.loc, g_raw_per_m, out); return true; }
        if (mv.ore_on) { for (int i = 0; i < 3; ++i) out[i] = mv.ore_loc[i] / MV_UU_PER_M; return true; }
        return false;
    }

    // WHAT: nearest rock of ship (registry, surface). near = its centre closer than bubble_too_near. c / r = its centre (m) and radius (m).
    bool bub_near_rock(UObject* ship, const double p[3], double cr, double c[3], double& r)
    {
        double surf = 0.0;
        const int id = nearest_rock_any_id(ship, surf, 0);
        if (id <= 0 || !reg_geom(id, c, r)) return false;
        return bubble_too_near(bub_dist(p, c), cr, r);
    }

    // WHAT: replan fix (SEEK): the blocker is left out of the target choice for this visit, then path_guard against the target with it skipped.
    //       Legacy bub_exec and the ShipControl callback (kind REPLAN) both call it.
    void bub_do_replan(int sid, UObject* ship, MineSt& m, int blk)
    {
        if (blk > 0) g_path_skip[sid].insert(blk);   // the blocker is never picked as a target for this visit
        Config c = eff_cfg(sid);
        std::wstring why;
        UObject* item = item_c(c.resource, why);
        UObject* tg = m.rock > 0 ? rock_actor_by_id(m.rock) : nullptr;
        if (tg && m.st == MS_SEEK) path_guard(ship, sid, m, c, tg, item);
    }

    // WHAT: skip fix (SEEK: give the target up; waypoint / ore: the flight is dropped). Legacy bub_exec and the ShipControl callback (kind SKIP) both call it.
    //       cur (ore flight) is read from g_mvs here, before the cancel.
    void bub_do_skip(int sid, UObject* ship, MineSt& m, int phase, int blk, double blk_d)
    {
        if (phase == BPH_SEEK && m.rock > 0) { path_give_up(ship, sid, m, m.rock, blk, blk_d); return; }
        const auto mj = g_mvs.find(sid);
        const MoveJob cur = mj != g_mvs.end() ? mj->second : MoveJob();
        if (phase == BPH_ORE && cur.ore_on)
            for (SwOre& o : m.sw_list) if (o.a == cur.rock_act) o.done = true;   // SWEEP: the ore is unreachable for this run
        mv_cancel(sid); zero_all_dual(ship);
        if (m.st == MS_SWEEP && (phase == BPH_WAYPOINT || phase == BPH_ORE))
        {
            // back by warp only when a return point was saved (sw_back); else the next ore is picked. A return with no saved point warped to system 0.
            if (m.sw_back) { m.sw_nofly = true; sw_phase(ship, sid, m, SW_RETURN); }
            else if (m.sw_ph != SW_PICK) sw_phase(ship, sid, m, SW_PICK);
        }
    }

    // WHAT: one mining ship's laser this tick: its system and the beam from the ship (a, m) to its rock centre (b, m).
    struct Beam { int sys = 0; double a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; };
    std::map<int, Beam> g_beams;           // sid -> beam of every bench ship in MINE with a readable ship and rock (rebuilt each tick)
    std::map<int, double> g_beam_clear_t;  // sid -> seconds its beam has been clear while its laser is held (s)
    std::set<int> g_beam_held;             // sids whose laser is held off because another bench ship is in their beam
    std::map<int, ULONGLONG> g_fire_t;     // sid -> GetTickCount64 when it entered MS_MINE (tiebreak: first to fire keeps firing)
    std::map<int, double> g_beam_hold_t;   // sid -> seconds its laser has been held (s); BEAM_YIELD_S gives the rock up

    // WHAT: beam pass (cmd_tick 0.25 s block, after bubble_tick). A firing ship's laser is held off while another bench ship is inside its beam
    //       (ship_in_beam) and resumed after BEAM_CLEAR_S clear (beam_hold_next). Log: 'BEAM hold' / 'BEAM resume'.
    // WHY: the laser damages every ship in the beam, not only the rock (hull hit with no attacker).
    // WHAT: a held ship whose beam cannot be built any more (ship or rock unreadable) is released: laser back on, hold state gone.
    // WHY: otherwise it is skipped by beam_tick for good: laser off, no resume, no yield (code review of PR 54).
    void beam_release(int sid)
    {
        if (!g_beam_held.erase(sid)) return;
        g_beam_clear_t.erase(sid); g_beam_hold_t.erase(sid);
        run_cmd(-1, sid, "laser_on");
        log_line(STR("BEAM resume ship=") + std::to_wstring(sid) + STR(" (beam unreadable)"));
    }

    void beam_tick(double dt)
    {
        const std::wstring scls = ms_ship_cls();
        if (scls.empty())   // MSCore too old: no ship lookups, release any held laser
        {
            for (int sid : std::set<int>(g_beam_held)) beam_release(sid);
            g_ship_blk.clear();
            g_beams.clear();
            return;
        }
        std::set<int> prev_blk;
        for (const auto& kv : g_ship_blk) prev_blk.insert(kv.first);
        g_ship_blk.clear();
        g_beams.clear();
        for (int sid : g_bench_ids)
        {
            auto it = g_mine.find(sid);
            if (it == g_mine.end()) continue;
            MineSt& m = it->second;
            if (m.st != MS_MINE) { g_beam_held.erase(sid); g_beam_clear_t.erase(sid); g_fire_t.erase(sid); g_beam_hold_t.erase(sid); continue; }
            UObject* ship = find_by_id(scls.c_str(), sid);
            ActFrame f;
            if (!alive(ship) || !act_frame(ship, f)) { beam_release(sid); continue; }
            ShipBlk& sb = g_ship_blk[sid];   // this ship is a no-go sphere for the other ships (path checks)
            sb.sys = sys_id(ship);
            for (int i = 0; i < 3; ++i) sb.c[i] = f.loc[i] / MV_UU_PER_M;
            g_fire_t.emplace(sid, GetTickCount64());
            double rc[3] = {0, 0, 0}, rr = 0.0;
            if (m.rock <= 0 || !reg_geom(m.rock, rc, rr)) { beam_release(sid); continue; }
            Beam bm;
            bm.sys = sys_id(ship);
            for (int i = 0; i < 3; ++i) { bm.a[i] = f.loc[i] / MV_UU_PER_M; bm.b[i] = rc[i]; }
            g_beams[sid] = bm;
        }
        for (int sid : prev_blk)
            if (!g_ship_blk.count(sid)) log_line(STR("SHIPBLOCK remove ship=") + std::to_wstring(sid));
        for (const auto& kv : g_ship_blk)
            if (!prev_blk.count(kv.first)) log_line(STR("SHIPBLOCK add ship=") + std::to_wstring(kv.first) + STR(" r=") + std::to_wstring(static_cast<long long>(SHIP_ZONE_M)));
        for (const auto& kv : g_beams)
        {
            const int sid = kv.first;
            const Beam& bm = kv.second;
            int victim = 0;
            for (int vid : g_bench_ids)
            {
                if (vid == sid) continue;
                UObject* vs = find_by_id(scls.c_str(), vid);
                ActFrame vf;
                if (!alive(vs) || !act_frame(vs, vf) || sys_id(vs) != bm.sys) continue;
                const double vp[3] = {vf.loc[0] / MV_UU_PER_M, vf.loc[1] / MV_UU_PER_M, vf.loc[2] / MV_UU_PER_M};
                bool hit = ship_in_beam(bm.a, bm.b, vp, bubble_ship_r(vid));
                if (!hit && in_ship_zone(bm.a, vp))   // inside the 300 m zone: the earlier firer keeps firing (zone_hold_for)
                {
                    const auto ft = g_fire_t.find(vid);
                    hit = zone_hold_for(g_ship_blk.count(vid) > 0, ft != g_fire_t.end() ? ft->second : 0, vid, g_fire_t[sid], sid);
                }
                if (hit) { victim = vid; break; }
            }
            const bool held = g_beam_held.count(sid) > 0;
            const bool now_held = beam_hold_next(held, victim != 0, g_beam_clear_t[sid], dt);
            double& ht = g_beam_hold_t[sid];
            ht = now_held ? ht + dt : 0.0;
            if (now_held && !held)
            {
                g_beam_held.insert(sid);
                run_cmd(-1, sid, "laser_off");
                log_line(STR("BEAM hold ship=") + std::to_wstring(sid) + STR(" victim=") + std::to_wstring(victim));
            }
            else if (!now_held && held)
            {
                g_beam_held.erase(sid);
                run_cmd(-1, sid, "laser_on");
                log_line(STR("BEAM resume ship=") + std::to_wstring(sid));
            }
            if (now_held && beam_yield_due(g_beam_hold_t[sid]))
            {   // held too long in a shared zone: this ship gives its rock up and seeks the next one
                UObject* ys = find_by_id(scls.c_str(), sid);
                MineSt& ym = g_mine[sid];
                log_line(STR("BEAM yield ship=") + std::to_wstring(sid) + STR(" rock=") + std::to_wstring(ym.rock) +
                         STR(" held=") + std::to_wstring(static_cast<long long>(g_beam_hold_t[sid])) + STR(" s"));
                g_path_skip[sid].insert(ym.rock);
                g_beam_held.erase(sid); g_beam_hold_t.erase(sid); g_beam_clear_t.erase(sid);
                if (alive(ys)) begin_collect(ys, sid, ym, 0, STR("shared zone, seeking next rock"));
                continue;
            }
            if (now_held) g_mine[sid].idle_t = 0.0;   // a held laser is not a depleted rock
        }
    }

    // WHAT: bubble tick (cmd_tick 0.25 s block, before mining_tick). The bubble ladder runs in ShipControl (SC3); the mining side only answers
    //       its callbacks (sc_host_*). The legacy mining ladder was removed in SC6-D2 (mining refuses to run without ShipControl).
    void bubble_tick(double dt)
    {
        if (!sc_bubble_block() && g_sc_speed_on) sc_tick_bubble(dt);
    }

    // WHAT: ladder state of ship sid for the status line: ShipControl's status (SC3). Zeros when ShipControl does not run the ship.
    void bub_ladder(int sid, int& braking, int& replans, int& wp, int& resume, int& blk)
    {
        braking = replans = wp = resume = blk = 0;
        if (!sc_bub_on_ship(sid)) return;
        ScBubbleStatus s{};
        if (!sc_bubble_status(sid, s) || !s.have) return;
        braking = s.braking; replans = s.replans; wp = s.wp; resume = s.resume; blk = s.blk;
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
        const double p[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
        const double cr = bubble_ship_r(sid);
        const SpeedMon sp = spd_get(sid);
        const double v = sp.have ? sp.v / g_raw_per_m : 0.0;
        const int sys = sys_id(ship);
        const double L = bubble_look_ahead(v);
        const double b[3] = {p[0] + f.fw[0] * L, p[1] + f.fw[1] * L, p[2] + f.fw[2] * L};
        char buf[400];
        std::string out;
        if (!sim)
        {
            MsFlight sfl;
            const int sfr = ms_flight_read(sid, FL_STATE, sfl);
            const int thr = (sfr == 1 && (sfl.flags & FF_THROTTLE)) ? sfl.throttle : 0;
            auto mi = g_mine.find(sid);
            const int trock = mi != g_mine.end() ? mi->second.rock : 0;
            auto mj = g_mvs.find(sid);
            const MoveJob* mv = (mj != g_mvs.end() && !mj->second.kind.empty()) ? &mj->second : nullptr;
            const bool thrust = mv && mv->kind == "approach_rock";
            const BubFlags fl = bubble_flags(sid);
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
            int lbr = 0, lrep = 0, lwp = 0, lres = 0, lblk = 0;
            bub_ladder(sid, lbr, lrep, lwp, lres, lblk);
            std::snprintf(buf, sizeof(buf), "ladder braking=%d replans=%d wp=%d resume=%d blk=%d\r\n", lbr, lrep, lwp, lres, lblk);
            out += buf;
            double d = 0.0, rad = 0.0;
            const int bid = bubble_query(sid, ship, f, v, bubble_skip_id(sid, ph, p, trock), d, rad);
            std::snprintf(buf, sizeof(buf), "blocker now: %s\r\n",
                          bid ? (std::to_string(bid) + " dist=" + std::to_string(d) + " rad=" + std::to_string(rad)).c_str() : "none");
            out += buf;
            std::vector<std::pair<double, RegRock>> cands;
            reg_nearest_n(sys, nullptr, p, nullptr, 20000.0, 10, cands);
            for (const auto& pr : cands)
            {
                const RegRock& rk = pr.second;
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
            RegRock tr;
            if (!reg_get(rock, tr)) return STR("error: rock not in registry");
            const double bt[3] = {tr.c[0], tr.c[1], tr.c[2]};
            struct Hit { double t; int id; double d; double rad; };
            std::vector<Hit> hits;
            const double lo[3] = {std::min(p[0], bt[0]) - PATH_RMAX_M, std::min(p[1], bt[1]) - PATH_RMAX_M, std::min(p[2], bt[2]) - PATH_RMAX_M};
            const double hi[3] = {std::max(p[0], bt[0]) + PATH_RMAX_M, std::max(p[1], bt[1]) + PATH_RMAX_M, std::max(p[2], bt[2]) + PATH_RMAX_M};
            const double ll = std::max(1e-12, bub_dist(p, bt) * bub_dist(p, bt));
            std::vector<RegRock> box;
            reg_box(sys, lo, hi, box);
            for (const RegRock& rk : box)
            {
                if (!rk.alive || rk.sys != sys || rk.id == rock) continue;
                const double rr = block_r_m(reg_eff_rad(rk), bubble_clear_m(cr, g_cfg.bubble_clearance_m));
                double pd = 0.0;
                if (!seg_sphere_blocks(p, bt, rk.c, rr, pd)) continue;
                const double w[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                const double dd[3] = {bt[0] - p[0], bt[1] - p[1], bt[2] - p[2]};
                hits.push_back({(w[0] * dd[0] + w[1] * dd[1] + w[2] * dd[2]) / ll, rk.id, pd, reg_eff_rad(rk)});
            }
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

    // ---------- ShipControl bubble callbacks (SC3-C, ScHost level 4) ----------
    // WHAT: ScHost.first_blocker: first blocker on p..b for ship sid (phase, target rock). Fills out: distance, radius, centre, geom_ok and rock_r
    //       (reg_geom of the blocker; geom_ok 0 and rock_r 0 when the geometry is unknown). Returns the id (0 = clear, < 0 = ship sphere).
    // WHY: the registry query stays in mining (ship spheres, skip rule); ShipControl only decides the ladder from the hit.
    int sc_host_first_blocker(int sid, int sys, int phase, int rock, const double p[3], const double b[3], double clear_m, ScBlockHit* out)
    {
        try
        {
            if (!out) return 0;
            reg_area_seeded(sys, p);   // unseeded area: the query sees the seeded rocks only (logged once per pass)
            const int skip = bubble_skip_id(sid, static_cast<BubblePhase>(phase), p, rock);
            double d = 0.0, r = 0.0, cc[3] = {0, 0, 0};
            const int id = seg_blocker(sys, p, b, skip, clear_m, false, d, cc, r, false, sid);
            out->d = d;
            out->block_r = r;
            for (int i = 0; i < 3; ++i) out->c[i] = cc[i];
            out->geom_ok = 0;
            out->rock_r = 0.0;
            double gc[3] = {0, 0, 0}, gr = 0.0;
            if (id != 0 && reg_geom(id, gc, gr)) { out->geom_ok = 1; out->rock_r = gr; }
            return id;
        }
        catch (...) { return 0; }
    }

    // WHAT: ScHost.bubble_profile: mining's side of one ship's bubble pass (active, job flags, target rock, flight destination).
    //       0 = not a ship ShipControl runs (SC erases the ship's state). No ship lookup: ShipControl checks the ship alive.
    int sc_host_bubble_profile(int sid, ScBubbleProfile* out)
    {
        try
        {
            if (!out || !sc_bub_on_ship(sid)) return 0;
            const auto mi = g_mine.find(sid);
            const MineSt* m = mi != g_mine.end() ? &mi->second : nullptr;
            const bool state_ok = m && (m->st == MS_SEEK || m->st == MS_MINE || m->st == MS_COLLECT || m->st == MS_SWEEP);
            const auto mj = g_mvs.find(sid);
            const MoveJob* mv = (mj != g_mvs.end() && !mj->second.kind.empty()) ? &mj->second : nullptr;
            out->active = (state_ok && !(m->st == MS_SWEEP && m->sw_ph == SW_WARP)) ? 1 : 0;
            out->thrust = mv && mv->kind == "approach_rock" ? 1 : 0;
            out->pt_on = mv && mv->pt_on ? 1 : 0;
            out->ore_on = mv && mv->ore_on ? 1 : 0;
            out->job_active = mv ? 1 : 0;
            out->warp_wait = mv && mv->warp_wait ? 1 : 0;
            out->rock = m ? m->rock : 0;
            double dst[3] = {0, 0, 0};
            out->dst_ok = mv && flight_dst(*mv, dst) ? 1 : 0;
            for (int i = 0; i < 3; ++i) out->dst_m[i] = dst[i];
            out->clearance_m = static_cast<double>(g_cfg.bubble_clearance_m);
            return 1;
        }
        catch (...) { return 0; }
    }

    // WHAT: ScHost.near_rock: 1 = ship sid (centre p, radius ship_r_m) is too near its nearest rock. out_c3 / out_r = that rock's centre (m) and radius (m).
    int sc_host_near_rock(int sid, const double p[3], double ship_r_m, double* out_c3, double* out_r)
    {
        try
        {
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(ship) || !out_c3 || !out_r) return 0;
            double c[3] = {0, 0, 0}, r = 0.0;
            const bool too_near = bub_near_rock(ship, p, ship_r_m, c, r);
            for (int i = 0; i < 3; ++i) out_c3[i] = c[i];
            *out_r = r;
            return too_near ? 1 : 0;
        }
        catch (...) { return 0; }
    }

    // WHAT: ScHost.on_bubble: one ladder event. kind 1 REPLAN and 2 SKIP run their fix here; kind 3 FLY starts the point flight (approach_rock job to ev->loc / ev->ch).
    //       1 = done (FLY: the flight started), 0 = not.
    int sc_host_on_bubble(int sid, const ScBubbleEvent* ev)
    {
        try
        {
            if (!ev) return 0;
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(ship)) return 0;
            if (ev->kind == 3)
            {
                PtTarget pt;
                pt.sys = ev->sys;
                for (int i = 0; i < 3; ++i) { pt.ch[i] = ev->ch[i]; pt.loc[i] = ev->loc[i]; }
                mv_cancel(sid); zero_all_dual(ship);
                return move_start(sid, "approach_rock", -1, nullptr, &pt) == STR("waiting") ? 1 : 0;
            }
            const auto mi = g_mine.find(sid);
            if (mi == g_mine.end()) return 0;
            if (ev->kind == 1) { bub_do_replan(sid, ship, mi->second, ev->blk); return 1; }
            if (ev->kind == 2) { bub_do_skip(sid, ship, mi->second, ev->phase, ev->blk, ev->blk_d); return 1; }
            return 0;
        }
        catch (...) { return 0; }
    }
}
