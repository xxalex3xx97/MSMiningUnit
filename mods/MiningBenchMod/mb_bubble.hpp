// FILE: mods/MiningBenchMod/mb_bubble.hpp
// WHAT: bubble reflex for approaching ships (state SEEK). One ship per 0.25 s tick, round robin. The registry is asked for the first rock
//       on the nose within the look-ahead L (max(100 m, speed x 4 s)); a rock counts when it lies inside the bubble
//       (ship CollisionRadius + rock radius + BUBBLE_CLEARANCE_M). The target is left out while the ship is in its stand-off phase.
//       Ladder (mb_logic.hpp bubble_step): BRAKE = Throttle 0 until speed < BUBBLE_BRAKE_V; then REPLAN = path_guard against the target with the
//       blocker in the path skip; after BUBBLE_MAX_REPLAN replans RETARGET = the target is given up (path_give_up).
// WHY: the path check runs before an approach and every PATH_WATCH_S; a rock that comes into view between checks was hit at full speed.
// STATE: g_bubble (mb_steer.hpp), one BubbleState per ship. move_aim_step holds Throttle 0 while braking (bubble_braking).
// BUDGET: one ship pass <= BUBBLE_SLOW_MS (0.2 ms). Over it: 'BUBBLE slow' (once per 30 s) and that ship is not checked for 10 s
//         (never while it is braking: its pending action must run).
// OUTPUT: log 'BUBBLE brake ship= rock= dist= rad= speed= -> replan|retarget', 'BUBBLE slow ship= ms='.
//         Dev: bubble_status:<ship> and bubble_sim:<ship>:<rock_id> (read-only), both write MiningBubble.txt (mb_devcmd.hpp dispatch).
// DEPENDS ON (exact, include order): mb_registry.hpp (reg_st, RockReg, reg_eff_rad), mb_mining.hpp (MineSt, g_mine, g_bench_ids, standoff_of,
//          path_guard, path_give_up, rock_actor_by_id, item_c), mb_steer.hpp (spd_of, g_bubble, g_raw_per_m), mb_logic.hpp (bubble_*).
// USED BY: mb_testcmd.hpp (cmd_tick, bubble_tick), mb_devcmd.hpp (bubble_dev).

namespace
{
    // WHAT: a ship whose bubble pass was slow is not checked this long (ms); the slow log line is written at most this often (ms).
    constexpr ULONGLONG BUBBLE_OFF_MS = 10000;
    constexpr ULONGLONG BUBBLE_SLOW_LOG_MS = 30000;

    std::map<int, ULONGLONG> g_bubble_off;   // sid -> GetTickCount64 until which the ship is not checked (slow pass)
    ULONGLONG g_bubble_slow_ms = 0;          // GetTickCount64 of the last BUBBLE slow line (0 = none yet)
    size_t g_bubble_rr = 0;                  // round robin position over g_bench_ids

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

    // WHAT: the stand-off phase of ship sid: its target rock (rock > 0) is excluded from the bubble when the ship is within APPROACH_RAMP_M of the
    //       target stand-off point. Returns the id to leave out (0 = none).
    int bubble_skip_id(int sid, const double p[3], int rock)
    {
        if (rock <= 0) return 0;
        double so[3], tc[3], tr = 0.0;
        if (!standoff_of(sid, rock, p, so, tc, tr)) return 0;
        return bub_dist(p, so) <= APPROACH_RAMP_M ? rock : 0;
    }

    // WHAT: the nearest rock on the nose of ship sid within the bubble for look-ahead speed (registry, first_blocker). rock = the ship target.
    //       Returns the blocker id, 0 = clear. out_d = blocker distance to the nose line (m); out_r = its radius (m).
    int bubble_query(int sid, UObject* ship, const ActFrame& f, double speed, int rock, double& out_d, double& out_r)
    {
        const double p[3] = {f.loc[0] / MV_UU_PER_M, f.loc[1] / MV_UU_PER_M, f.loc[2] / MV_UU_PER_M};
        const double L = bubble_look_ahead(speed);
        const double b[3] = {p[0] + f.fw[0] * L, p[1] + f.fw[1] * L, p[2] + f.fw[2] * L};
        const RockReg& t = reg_st().tab;
        const size_t i = t.first_blocker(sys_id(ship), p, b, bubble_clear_m(bubble_ship_r(sid)), nullptr, bubble_skip_id(sid, p, rock), nullptr, 0.0, out_d);
        if (i == RockReg::npos) return 0;
        out_r = reg_eff_rad(t.rocks[i]);
        return t.rocks[i].id;
    }

    // WHAT: one bubble pass for SEEK ship sid (m = its MineSt): query, ladder step, and the action of the step (brake, replan or retarget).
    void bubble_one(int sid, MineSt& m)
    {
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        ActFrame f;
        if (!alive(ship) || !act_frame(ship, f)) return;
        BubbleState& bs = g_bubble[sid];
        const SpeedMon& sp = spd_of(sid);
        const double v = sp.have ? sp.v / g_raw_per_m : 0.0;
        double d = 0.0, rad = 0.0;
        const int bid = bubble_query(sid, ship, f, v, m.rock, d, rad);
        if (bid) { bs.blk = bid; bs.blk_d = d; }
        const BubbleAct act = bubble_step(bs, bid != 0, v, m.rock);
        if (act == BBA_BRAKE)
        {
            set_int_prop(ship, STR("Throttle"), 0);
            log_line(STR("BUBBLE brake ship=") + std::to_wstring(sid) + STR(" rock=") + std::to_wstring(bid) + STR(" dist=") +
                     std::to_wstring(static_cast<long long>(std::llround(d))) + STR(" rad=") + std::to_wstring(static_cast<long long>(std::llround(rad))) +
                     STR(" speed=") + std::to_wstring(v) + STR(" -> ") + (bs.pending == BBA_REPLAN ? STR("replan") : STR("retarget")));
        }
        else if (act == BBA_REPLAN)
        {
            if (bs.blk > 0) g_path_skip[sid].insert(bs.blk);   // the blocker is never picked as a target for this visit
            Config c = eff_cfg(sid);
            std::wstring why;
            UObject* item = item_c(c.resource, why);
            UObject* tg = m.rock > 0 ? rock_actor_by_id(m.rock) : nullptr;
            if (tg && m.st == MS_SEEK) path_guard(ship, sid, m, c, tg, item);
        }
        else if (act == BBA_RETARGET && m.rock > 0)
        {
            path_give_up(ship, sid, m, m.rock, bs.blk, bs.blk_d);
        }
    }

    // WHAT: bubble tick (cmd_tick 0.25 s block): at most one SEEK ship per tick, round robin over g_bench_ids. Other states drop their ladder.
    void bubble_tick(double dt)
    {
        (void)dt;
        const size_t n = g_bench_ids.size();
        if (n == 0) return;
        const ULONGLONG now = GetTickCount64();
        for (size_t k = 0; k < n; ++k)
        {
            const int sid = g_bench_ids[(g_bubble_rr + k) % n];
            auto it = g_mine.find(sid);
            if (it == g_mine.end()) continue;
            if (it->second.st != MS_SEEK) { g_bubble.erase(sid); continue; }
            auto bi = g_bubble.find(sid);
            const bool braking = bi != g_bubble.end() && bi->second.braking;
            auto mj = g_mvs.find(sid);
            const bool approach = mj != g_mvs.end() && mj->second.kind == "approach_rock" && it->second.rock > 0;   // idle SEEK (no target): nothing to protect
            if (!braking && !approach) continue;
            auto off = g_bubble_off.find(sid);
            if (!braking && off != g_bubble_off.end() && now < off->second) continue;   // slow pass: paused (a braking ship always finishes)
            g_bubble_rr = (g_bubble_rr + k + 1) % n;
            LARGE_INTEGER t0;
            QueryPerformanceCounter(&t0);
            bubble_one(sid, it->second);
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
            return;
        }
    }

    // WHAT: bubble_status:<ship> and bubble_sim:<ship>:<rock_id> (Dev cmds). Read-only: no game write. Writes MiningBubble.txt; returns one line.
    //       status: ship pose and speed, look-ahead, the 10 nearest rocks (distance, surface, radius, bearing from the nose, ahead, in bubble), the blocker now.
    //       sim: the rocks that would block a straight flight from the ship to rock_id (target excluded), in order, and the first ladder step.
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
        char buf[320];
        std::string out;
        if (!sim)
        {
            int32_t thr = 0;
            get_int_prop(ship, STR("Throttle"), thr);
            auto mi = g_mine.find(sid);
            const int trock = mi != g_mine.end() ? mi->second.rock : 0;
            std::snprintf(buf, sizeof(buf), "ship=%d sys=%d pos=(%.1f,%.1f,%.1f) m speed=%.2f m/s throttle=%d ship_r=%.1f m look_ahead=%.1f m clear=%.1f m\r\n",
                          sid, sys, p[0], p[1], p[2], v, static_cast<int>(thr), cr, L, bubble_clear_m(cr));
            out += buf;
            std::snprintf(buf, sizeof(buf), "nose=(%.3f,%.3f,%.3f) target=%d state=%s\r\n", f.fw[0], f.fw[1], f.fw[2], trock,
                          mi != g_mine.end() ? u8(mine_state_name(mi->second.st)).c_str() : "none");
            out += buf;
            const auto bs = g_bubble.find(sid);
            std::snprintf(buf, sizeof(buf), "ladder braking=%d replans=%d blk=%d\r\n", bs != g_bubble.end() && bs->second.braking ? 1 : 0,
                          bs != g_bubble.end() ? bs->second.replans : 0, bs != g_bubble.end() ? bs->second.blk : 0);
            out += buf;
            double d = 0.0, rad = 0.0;
            const int bid = bubble_query(sid, ship, f, v, trock, d, rad);
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
                const double rr = reg_eff_rad(rk) + bubble_clear_m(cr);
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
                const double rr = reg_eff_rad(rk) + bubble_clear_m(cr);
                double pd = 0.0;
                if (!seg_sphere_blocks(p, bt, rk.c, rr, pd)) return;
                const double w[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                const double dd[3] = {bt[0] - p[0], bt[1] - p[1], bt[2] - p[2]};
                hits.push_back({(w[0] * dd[0] + w[1] * dd[1] + w[2] * dd[2]) / ll, rk.id, pd, reg_eff_rad(rk)});
            });
            std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.t < y.t; });
            std::snprintf(buf, sizeof(buf), "sim ship=%d rock=%d speed=%.2f m/s blockers=%zu\r\n", sid, rock, v, hits.size());
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
