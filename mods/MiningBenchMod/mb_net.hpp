// FILE: mods/MiningBenchMod/mb_net.hpp
// WHAT: dock network glue: the shared tables (g_net_docks, g_net_ships, g_net_ticket) kept in step with the game once a second (net_tick),
//       the reservation helpers used by job_start, and the status files NetDocks.txt, NetShips.txt, DockBusy.txt and DockPos.txt (dock positions cache).
// WHY separate: the rules (net_pick, net_grant_tick, net_reconcile, texts) are pure and live in mb_logic.hpp so the tests run them.
//       This file only reads the game and writes files.
// DEPENDS ON: mb_common.hpp (alive, g_now, get_dll_dir, write_atomic), mb_ue.hpp (find_by_id, obj_id, get_int_prop, get_obj_prop, FindAllOf),
//             mb_config.hpp (eff_cfg), mb_cmdstate.hpp (g_jobs, DockJob), mb_logic.hpp (NetDock, NetShip, net_reconcile, net_grant_tick).
//             Forward (defined in mb_mining.hpp, same as mine_status_x_write in mb_dockjob.hpp): net_holder_working, net_hold_s.
// USED BY: mb_dockjob.hpp (net_pick, net_unreserve), mb_mining.hpp (net_tick once a second, net_holder_working, net_hold_s), dllmain.cpp (include only).
// NOTE: a dock is GONE when find_by_id("Dock", id) finds no live object. Docks whose object is not loaded would look gone: check in game (scenario c).
#pragma once

namespace
{
    bool net_holder_working(int sid);   // mb_mining.hpp: ship has a dock job or its mining loop runs
    double net_hold_s(int sid);         // mb_mining.hpp: seconds docked in the current hold (0 when not holding)

    std::map<int, NetDock> g_net_docks;   // dock id -> reservation, occupant, queue
    std::map<int, NetShip> g_net_ships;   // ship id -> intent, wait state (rows exist while a ship has a net_pick)
    long g_net_ticket = 0;                // global FIFO ticket counter
    std::map<int, std::string> g_net_dock_names;   // dock id -> in-game name (refreshed in net_tick; missing = not readable)
    std::map<int, std::string> g_net_ship_names;   // ship id -> display name (refreshed in net_tick; missing = not readable)
    std::map<int, DockPos> g_net_dockpos;          // dock id -> where the dock is (learned from a docked ship, cached in DockPos.txt)

    NetReq net_req_of(int sid, Purpose purpose);   // mb_mining.hpp: cargo, fuel, hull and dock ore of ship sid (queue score input)

    // WHAT: text cleaned for the '|' separated files and one-line logs.
    std::string net_clean(std::string s)
    {
        for (char& c : s) if (c == '|' || c == '\r' || c == '\n') c = ' ';
        return s;
    }

    // WHAT: ship sid holds dock d (reserved for it, or docked there). Its own dock job at d is never "busy" (dock_busy is only a fallback).
    bool net_owns(int d, int sid)
    {
        const auto it = g_net_docks.find(d);
        return it != g_net_docks.end() && (it->second.resv == sid || it->second.occ == sid);
    }

    // WHAT: name for a file column: the name, or "" when it is not readable.
    std::string net_name(const std::map<int, std::string>& names, int id)
    {
        const auto it = names.find(id);
        return it == names.end() ? std::string() : it->second;
    }

    // WHAT: name for a status / log text: the name, else the id (only when the name can't be read).
    std::string net_dock_label(int id) { const std::string n = net_name(g_net_dock_names, id); return n.empty() ? std::to_string(id) : n; }
    std::string net_ship_label(int sid) { const std::string n = net_name(g_net_ship_names, sid); return n.empty() ? std::to_string(sid) : n; }
    std::set<int> g_flee_pick;            // ships whose dock trip is a hull flee (purpose P_FLEE until the job starts)
    std::map<int, std::string> g_dock_after_undock;   // ship -> dock command to start when its undock (first step) ends ok

    // WHAT: dock id the ship is docked at, 0 = not docked. Table occupancy first (net_tick: DockID or Dock object), then the ship's own DockID / Dock.
    // WHY: ship_docked() needs both props and read false for docked ships (live log 16:03): one source is not enough.
    int net_docked_at(int sid, UObject* ship)
    {
        for (const auto& kv : g_net_docks) if (kv.second.occ == sid) return kv.first;
        if (!alive(ship)) return 0;
        int32_t did = 0;
        get_int_prop(ship, STR("DockID"), did);
        if (did > 0) return did;
        UObject* d = get_obj_prop(ship, STR("Dock"));
        return alive(d) ? obj_id(d) : 0;
    }
    constexpr double NET_FULL_S = 300.0;  // a FULL mark clears after this long (s), or earlier when the ore cargo shrinks there

    // WHAT: ship sid leaves the dock network (stop, halt, new trip, ship gone): queue place, ticket and reservation go. Logs RELEASE.
    void net_leave(int sid)
    {
        const auto si = g_net_ships.find(sid);
        bool had = si != g_net_ships.end() && si->second.waits_dock != 0;
        for (const auto& kv : g_net_docks) if (kv.second.resv == sid) had = true;
        if (had) log_line(widen("CONTROL ship=" + std::to_string(sid) + " (" + net_ship_label(sid) + ") called off"));
        if (si != g_net_ships.end() && si->second.waits_dock != 0)
            log_line(STR("NET dock=") + std::to_wstring(si->second.waits_dock) + STR(" RELEASE by=") + std::to_wstring(sid));
        for (const auto& kv : g_net_docks)
            if (kv.second.resv == sid) log_line(STR("NET dock=") + std::to_wstring(kv.first) + STR(" RELEASE by=") + std::to_wstring(sid));
        net_release_ship(g_net_docks, g_net_ships, sid);
    }

    // WHAT: a dock reservation of ship sid is dropped (failed start). The ship keeps its queue place (use net_release_ship to leave the queue).
    void net_unreserve(int sid)
    {
        for (auto& kv : g_net_docks)
        {
            NetDock& d = kv.second;
            if (d.resv == sid) { d.resv = 0; d.why = P_NONE; d.since = 0; }
        }
    }

    // WHAT: seconds since t (0 when t is unset).
    long long net_age(double t)
    {
        return t > 0 ? static_cast<long long>(g_now - t) : 0;
    }

    const char* net_purpose_text(Purpose p)
    {
        switch (p)
        {
        case P_UNLOAD: return "unload";
        case P_REFUEL: return "refuel";
        case P_UNDOCK_TARGET: return "undock";
        case P_PICK: return "pick";
        case P_FLEE: return "flee";
        default: return "none";
        }
    }

    // WHAT: plain word of a purpose for the Control log lines (Unload, Refuel, ...).
    const char* net_purpose_word(Purpose p)
    {
        switch (p)
        {
        case P_UNLOAD: return "Unload";
        case P_REFUEL: return "Refuel";
        case P_UNDOCK_TARGET: return "Undock";
        case P_PICK: return "Pick";
        case P_FLEE: return "Flee";
        default: return "Dock";
        }
    }

    // WHAT: holding phase text for NetShips.txt (NetShip.phase: 0 none, 1 flying, 2 holding, 3 cleared).
    const char* net_phase_text(int p)
    {
        switch (p)
        {
        case 1: return "flying";
        case 2: return "holding";
        case 3: return "cleared";
        default: return "none";
        }
    }

    // WHAT: DockPos.txt -> g_net_dockpos. Line: dock|sys|ch0|ch1|ch2|loc0|loc1|loc2. Bad lines are skipped.
    void net_load_dockpos()
    {
        std::ifstream f(get_dll_dir() / "DockPos.txt");
        std::string line;
        while (std::getline(f, line))
        {
            std::vector<std::string> c;
            size_t from = 0;
            for (size_t bar = line.find('|'); ; bar = line.find('|', from))
            {
                c.push_back(line.substr(from, bar == std::string::npos ? std::string::npos : bar - from));
                if (bar == std::string::npos) break;
                from = bar + 1;
            }
            if (c.size() != 8) continue;
            try
            {
                DockPos p;
                p.known = true;
                const int d = std::stoi(c[0]);
                p.sys = std::stoi(c[1]);
                for (int i = 0; i < 3; ++i) { p.ch[i] = std::stoll(c[2 + i]); p.loc[i] = std::stod(c[5 + i]); }
                g_net_dockpos[d] = p;
            }
            catch (...) {}
        }
    }

    // WHAT: dock d position from docked ship sh (sys, chunk, raw loc). A docked ship sits at its dock, so its position stands in for the dock (UNVERIFIED in game).
    void net_learn_dockpos(int d, UObject* sh)
    {
        const int sys = sys_id(sh);
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        if (sys <= 0 || !get_chunk(sh, ch) || !get_loc(sh, loc)) return;
        DockPos& p = g_net_dockpos[d];
        p.known = true; p.sys = sys;
        for (int i = 0; i < 3; ++i) { p.ch[i] = ch[i]; p.loc[i] = loc[i]; }
    }

    const char* net_reason_text(NetReason r)
    {
        switch (r)
        {
        case NR_WAIT_BUSY: return "wait_busy";
        case NR_WAIT_QUEUE: return "wait_queue";
        case NR_WAIT_PARKED: return "wait_parked";
        case NR_WAIT_FULL: return "wait_full";
        case NR_WAIT_UNMANAGED: return "wait_unmanaged";
        case NR_NOT_CONFIGURED: return "not_configured";
        case NR_DOCK_GONE: return "dock_gone";
        case NR_NOTHING_TO_UNLOAD: return "nothing_to_unload";
        default: return "ok";
        }
    }

    std::string net_join(const std::vector<int>& v)
    {
        std::string s;
        for (int x : v) s += (s.empty() ? "" : ",") + std::to_string(x);
        return s;
    }

    // WHAT: 1-based place of the ship in the queue of the dock it waits for (0 = not queued).
    int net_queue_pos(const NetShip& s)
    {
        const auto it = g_net_docks.find(s.waits_dock);
        if (it == g_net_docks.end()) return 0;
        const auto& q = it->second.queue;
        const auto f = std::find(q.begin(), q.end(), s.sid);
        return f == q.end() ? 0 : static_cast<int>(f - q.begin()) + 1;
    }

    // WHAT: dock status for NetDocks.txt: GONE, FULL, WORKING / PARKED (occupant working or parked), RESERVED, FREE.
    const char* net_status_text(const NetDock& d)
    {
        if (!d.exists) return "GONE";
        if (d.full) return "FULL";
        if (d.occ != 0) return net_holder_working(d.occ) ? "WORKING" : "PARKED";
        if (d.resv != 0) return "RESERVED";
        return "FREE";
    }

    // WHAT: NetDocks.txt, NetShips.txt and DockBusy.txt (dock|occupant: the docked or claiming ship, else the reserving ship). Each written only when changed.
    void net_write()
    {
        std::string dk, sh, busy, dp;
        for (const auto& kv : g_net_docks)
        {
            const NetDock& d = kv.second;
            const auto pos = g_net_dockpos.find(d.id);
            const bool line_ok = pos != g_net_dockpos.end() && pos->second.known;
            dk += std::to_string(d.id) + "|" + net_status_text(d) + "|" + std::to_string(d.occ) + "|" + net_purpose_text(d.why) + "|" +
                  std::to_string(net_age(d.since)) + "|" + net_join(d.queue) + "|" + (d.full ? "1" : "0") + "|" + net_name(g_net_dock_names, d.id) + "|" +
                  std::to_string(d.resv) + "|" + (line_ok ? "1" : "0") + "\n";
            const int who = d.occ != 0 ? d.occ : d.resv;
            if (who != 0) busy += std::to_string(d.id) + "|" + std::to_string(who) + "\n";
        }
        for (const auto& kv : g_net_ships)
        {
            const NetShip& s = kv.second;
            sh += std::to_string(s.sid) + "|" + net_purpose_text(s.intent) + "|" + net_join(s.wanted) + "|" + std::to_string(s.waits_dock) + "|" +
                  std::to_string(s.blocked_by) + "|" + std::to_string(net_age(s.wait_since)) + "|" + net_reason_text(s.reason) + "|" +
                  std::to_string(static_cast<long long>(net_hold_s(s.sid))) + "|" + std::to_string(eff_cfg(s.sid).dock_hold_limit) + "|" +
                  std::to_string(net_queue_pos(s)) + "|" + net_name(g_net_ship_names, s.sid) + "|" + net_name(g_net_dock_names, s.waits_dock) + "|" +
                  net_name(g_net_ship_names, s.blocked_by) + "|" + std::to_string(s.slot) + "|" + std::to_string(static_cast<long long>(s.prio)) + "|" +
                  net_clean(s.prio_why) + "|" + net_phase_text(s.phase) + "\n";
        }
        for (const auto& kv : g_net_dockpos)
        {
            const DockPos& p = kv.second;
            if (!p.known) continue;
            dp += std::to_string(kv.first) + "|" + std::to_string(p.sys) + "|" + std::to_string(p.ch[0]) + "|" + std::to_string(p.ch[1]) + "|" + std::to_string(p.ch[2]) + "|" +
                  std::to_string(p.loc[0]) + "|" + std::to_string(p.loc[1]) + "|" + std::to_string(p.loc[2]) + "\n";
        }
        static std::string last_dk, last_sh, last_busy, last_dp;
        try
        {
            if (dk != last_dk) { write_atomic(get_dll_dir() / "NetDocks.txt", dk); last_dk = dk; }
            if (sh != last_sh) { write_atomic(get_dll_dir() / "NetShips.txt", sh); last_sh = sh; }
            if (busy != last_busy) { write_atomic(get_dll_dir() / "DockBusy.txt", busy); last_busy = busy; }
            if (dp != last_dp) { write_atomic(get_dll_dir() / "DockPos.txt", dp); last_dp = dp; }
        }
        catch (...) {}
    }

    // WHAT: once a second: read the game (dock occupants, job targets, live ships, dock existence), reconcile the tables, grant free docks, write the files.
    // WHY: the tables must match the game before the rules run. Each object is checked with alive() first.
    void net_tick()
    {
        static bool dockpos_loaded = false;
        if (!dockpos_loaded) { dockpos_loaded = true; net_load_dockpos(); }
        std::map<int, int> occupant, heading;   // occupant: dock -> ship (docked, or a dock job heading there); heading: live ship -> dock of its job (0 none)
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* sh : ships)
        {
            if (!alive(sh)) continue;
            const int sid = obj_id(sh);
            heading[sid] = 0;
            const std::string sname = net_clean(u8(obj_display(sh)));
            if (sname.empty()) g_net_ship_names.erase(sid); else g_net_ship_names[sid] = sname;
            int32_t did = 0;
            get_int_prop(sh, STR("DockID"), did);
            UObject* d = get_obj_prop(sh, STR("Dock"));
            if (did <= 0 && alive(d)) did = obj_id(d);
            if (did > 0) { occupant[did] = sid; net_learn_dockpos(did, sh); }
        }
        for (const DockJob& j : g_jobs)
        {
            if (j.dock <= 0) continue;
            heading[j.sid] = j.dock;
            if (j.cmd != "undock") occupant[j.dock] = j.sid;   // a dock job claims its dock; an undock only leaves one
        }
        std::set<int> known, existing;
        for (const auto& kv : g_net_docks) known.insert(kv.first);
        for (const auto& kv : occupant) known.insert(kv.first);
        for (int d : known)
        {
            UObject* dobj = find_by_id(STR("Dock"), d);
            if (!alive(dobj)) continue;
            existing.insert(d);
            const std::string dname = net_clean(u8(dock_name(dobj)));
            if (dname.empty()) g_net_dock_names.erase(d); else g_net_dock_names[d] = dname;
        }
        for (auto& kv : g_net_docks)
            if (kv.second.full && g_now - kv.second.full_since >= NET_FULL_S) kv.second.full = false;
        net_reconcile(g_net_docks, g_net_ships, occupant, heading, existing);
        // dock arrival time: set when the occupant of a dock changes (dwell rule in net_bad_holder)
        static std::map<int, int> occ_prev;
        for (auto& kv : g_net_docks)
        {
            NetDock& d = kv.second;
            const auto p = occ_prev.find(kv.first);
            if (d.occ == 0) d.occ_since = 0;
            else if (p == occ_prev.end() || p->second != d.occ) d.occ_since = g_now;
            occ_prev[kv.first] = d.occ;
        }
        net_grant_tick(g_net_docks, g_net_ships, g_now);
        // server log (throttled like the Log tab): GRANT once per grant; WAIT once when the dock/holder/reason changes, then a reminder every 60 s
        static std::set<int> granted_seen;
        static std::map<int, LogGate> wait_gate;
        std::set<int> granted_now, waiting_now;
        for (const auto& kv : g_net_ships)
        {
            const NetShip& s = kv.second;
            const std::string sn = net_ship_label(kv.first);
            if (s.granted)
            {
                granted_now.insert(kv.first);
                if (granted_seen.count(kv.first) == 0)
                {
                    for (const auto& dk : g_net_docks)
                        if (dk.second.resv == kv.first)
                            log_line(widen("CONTROL ship=" + std::to_string(kv.first) + " (" + sn + ") cleared for " + net_purpose_word(s.intent) + " " + net_dock_label(dk.first)));
                }
            }
            if (s.waits_dock == 0) continue;
            waiting_now.insert(kv.first);
            const std::string key = std::to_string(s.waits_dock) + "|" + std::to_string(s.blocked_by) + "|" + net_reason_text(s.reason);
            const GateAct act = log_gate(wait_gate[kv.first], key, g_now, 0.0, 60.0);
            if (act != GATE_NONE)
                log_line(widen("NET ship=" + std::to_string(kv.first) + " (" + sn + ") " + (act == GATE_REMIND ? "STILL WAIT" : "WAIT") +
                               " dock=" + std::to_string(s.waits_dock) + " (" + net_dock_label(s.waits_dock) + ") holder=" + std::to_string(s.blocked_by) +
                               " (" + net_ship_label(s.blocked_by) + ") why=" + net_reason_text(s.reason) + " t=" + std::to_string(net_age(s.wait_since)) + "s"));
        }
        granted_seen = granted_now;
        for (auto it = wait_gate.begin(); it != wait_gate.end();) it = waiting_now.count(it->first) ? std::next(it) : wait_gate.erase(it);
        net_write();
    }
}
