// FILE: mods/MiningBenchMod/mb_filters.hpp
// WHAT: ship Logistic Filters. Reads ship.LogisticFilters, sends LogisticFilterInput commands (remove a row, add a row, set item/amount/action) and checks the result. pf_start plans the rows for the enabled fuels, pf_tick sends one command per step.
// WHY separate: the filter rows are a separate ship list from the orders. The refuel job sets them once and leaves them on the ship (brief 2026-10-27 real-orders).
// DEPENDS ON: mb_common (alive, log_line, widen), mb_ue (find_by_id, first_of, put, obj_id, obj_display), mb_docks (find_item), mb_orders (add_input).
// USED BY: mb_dockjob (pf_start, pf_tick, pf_state, pf_result), mb_testcmd (stop_all erases g_pf).
// GAME FACTS:
//   ASSUMPTION (probe, brief PROBE RESULTS): LogisticFilterInput.CreateInputLogisticFilter(InIndex, InItemStack{Item,Amount}, InAction, bInRemove), receiver = ship id.
//     UI row flow: index N with empty item = new row; then item with amount 0; then amount; then action (LIMIT=0, BUFFER=1). bInRemove=1 removes the row at index.
//   ASSUMPTION: ship.LogisticFilters is a TArray of struct LogisticFilter{ItemStack{Item,Amount}, Action}. Read by reflection, not checked in game.
//   UNVERIFIED: the server may apply rows late, so pf_tick waits PF_SETTLE_S and reads back before it compares.
#pragma once

namespace
{
    constexpr double PF_SETTLE_S = 0.5;    // wait after each send before the next one
    constexpr double PF_TIMEOUT_S = 5.0;   // max wait for a row to appear or a remove to apply

    // action: LIMIT=0 BUFFER=1 (same as the game enum)
    struct PfRow { UObject* item = nullptr; int amount = 0; int action = 0; };

    // WHAT: one filter job per ship. want = the rows to end with. phase 0 remove stale rows, 1 wait for a remove, 2 add rows (k = row, sub = step inside the row), 3 verify.
    // WHY: one send per tick and a read-back before each step, so the game applies each change before the next one (the UI does the same).
    struct PfJob { std::vector<PfRow> want; int phase = 0; size_t k = 0; int sub = 0; double t = 0.0; int before = 0; int state = 1; /*1 running,2 ok,3 failed*/ std::wstring result; };
    std::map<int, PfJob> g_pf;

    // WHAT: reads ship.LogisticFilters into out, walking the raw TArray by the struct stride. false when a property or layout is missing.
    // WHY: no header for the struct, so the layout comes from reflection (same way as inv_total).
    bool pf_read(UObject* ship, std::vector<PfRow>& out)
    {
        out.clear();
        if (!alive(ship)) return false;
        std::wstring err;
        if (!ms_load(err)) return false;
        const int n = g_ms.filters_read(ship, nullptr, 0);   // count first, then the rows
        if (n < 0) return false;
        std::vector<MsFilterRow> rows(static_cast<size_t>(n));
        if (n > 0 && g_ms.filters_read(ship, rows.data(), n) < 0) return false;
        for (const MsFilterRow& r : rows)
        {
            PfRow p;
            p.item = static_cast<UObject*>(r.item);
            p.amount = r.amount;
            p.action = r.action;
            out.push_back(p);
        }
        return true;
    }

    // WHAT: sends one LogisticFilterInput to the ship through MSCore. remove=true removes the row at idx. item may be nullptr (new empty row).
    // WHY: the command build (InItemStack struct write) moved to MSCore with the other order inputs.
    bool pf_send(int sid, int idx, UObject* item, int amount, int action, bool remove, std::wstring& err)
    {
        if (!ms_load(err)) return false;
        wchar_t e[256]{};
        return ms_err(g_ms.filter_send(sid, idx, item, amount, action, remove ? 1 : 0, e, 256), e, err);
    }

    // WHAT: filter job state for a ship: 0 none, 1 running, 2 ok, 3 failed.
    // WHY: the dock job polls this (stage 3) to know when the filters are set.
    int pf_state(int sid)
    {
        auto it = g_pf.find(sid);
        return it == g_pf.end() ? 0 : it->second.state;
    }

    // WHAT: the failure message of a filter job (empty when none).
    std::wstring pf_result(int sid)
    {
        auto it = g_pf.find(sid);
        return it == g_pf.end() ? std::wstring() : it->second.result;
    }

    // WHAT: true when both row lists have the same item, amount and action in the same order.
    bool pf_rows_equal(const std::vector<PfRow>& a, const std::vector<PfRow>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t k = 0; k < a.size(); ++k)
            if (a[k].item != b[k].item || a[k].amount != b[k].amount || a[k].action != b[k].action) return false;
        return true;
    }

    // WHAT: plans the filter rows for the fuels (name, amount): per fuel a Buffer row then a Limit row, both at amount. Starts the job unless the ship already has these rows (then state 2 at once).
    // WHY: fuels are passed in by the caller (config decides which are on), so this file has no policy. Returns false with err set when a fuel item or the ship is missing.
    bool pf_start(int sid, const std::vector<std::pair<std::string, int>>& fuels, std::wstring& err)
    {
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        if (!alive(ship)) { err = STR("ship not found"); return false; }
        PfJob j;
        for (const auto& [name, amount] : fuels)
        {
            std::wstring why;
            UObject* it = find_item(name, why);
            if (!it) { err = why; return false; }
            j.want.push_back({it, amount, 1});   // BUFFER
            j.want.push_back({it, amount, 0});   // LIMIT
        }
        log_line(STR("FILTERS start ship=") + std::to_wstring(sid) + STR(" rows=") + std::to_wstring(j.want.size()));
        std::vector<PfRow> cur;
        if (!pf_read(ship, cur)) { err = STR("LogisticFilters unreadable"); return false; }
        if (pf_rows_equal(cur, j.want)) { j.state = 2; log_line(STR("FILTERS already set")); }
        g_pf[sid] = j;
        return true;
    }

    // WHAT: advances every running filter job by one step (at most one send per job per call). dt in seconds.
    // WHY: runs from jobs_tick, so the game is never blocked. Each step reads back the ship before it acts.
    void pf_tick(double dt)
    {
        for (auto& entry : g_pf)
        {
            const int sid = entry.first;
            PfJob& j = entry.second;
            if (j.state != 1) continue;
            j.t += dt;
            auto fail = [&](const std::wstring& msg) { j.state = 3; j.result = msg; log_line(STR("FILTERS FAIL ") + msg); };
            UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
            if (!alive(ship)) { fail(STR("ship gone")); continue; }
            std::vector<PfRow> cur;
            if (!pf_read(ship, cur)) { fail(STR("LogisticFilters unreadable")); continue; }
            std::wstring err;

            if (j.phase == 0)   // remove stale rows, one per send
            {
                if (cur.empty()) { j.phase = j.want.empty() ? 3 : 2; j.k = 0; j.sub = 0; j.t = 0.0; continue; }
                if (!pf_send(sid, 0, nullptr, 0, 0, true, err)) { fail(err); continue; }
                j.before = static_cast<int>(cur.size());
                j.phase = 1; j.t = 0.0;
                continue;
            }
            if (j.phase == 1)   // wait for the remove to show, then check again
            {
                if (static_cast<int>(cur.size()) < j.before) { j.phase = 0; j.t = 0.0; continue; }
                if (j.t >= PF_TIMEOUT_S) fail(STR("remove timeout"));
                continue;
            }
            if (j.phase == 2)   // add rows. per row: sub 0 new empty row, 1 wait for it then item, 2 settle then amount, 3 settle then action, 4 settle then next row
            {
                const PfRow& w = j.want[j.k];
                const int idx = static_cast<int>(j.k);
                if (j.sub == 0)
                {
                    if (!pf_send(sid, idx, nullptr, 0, 0, false, err)) { fail(err); continue; }
                    j.sub = 1; j.t = 0.0;
                }
                else if (j.sub == 1)
                {
                    if (cur.size() > j.k)
                    {
                        if (!pf_send(sid, idx, w.item, 0, 0, false, err)) { fail(err); continue; }
                        j.sub = 2; j.t = 0.0;
                    }
                    else if (j.t >= PF_TIMEOUT_S) fail(STR("row create timeout"));
                }
                else if (j.t >= PF_SETTLE_S)
                {
                    if (j.sub == 2)
                    {
                        if (!pf_send(sid, idx, w.item, w.amount, 0, false, err)) { fail(err); continue; }
                        j.sub = 3; j.t = 0.0;
                    }
                    else if (j.sub == 3)
                    {
                        if (!pf_send(sid, idx, w.item, w.amount, w.action, false, err)) { fail(err); continue; }
                        j.sub = 4; j.t = 0.0;
                    }
                    else
                    {
                        log_line(STR("FILTERS row ") + std::to_wstring(idx) + STR(" item=") + obj_display(w.item) +
                                 STR(" amount=") + std::to_wstring(w.amount) + STR(" action=") + std::to_wstring(w.action));
                        j.sub = 0; j.t = 0.0;
                        if (++j.k >= j.want.size()) j.phase = 3;
                    }
                }
                continue;
            }
            if (j.phase == 3)   // verify after settle
            {
                if (j.t < PF_SETTLE_S) continue;
                if (pf_rows_equal(cur, j.want)) { j.state = 2; log_line(STR("FILTERS ok rows=") + std::to_wstring(j.want.size())); }
                else fail(STR("filters mismatch"));
            }
        }
    }
}
