// FILE: mods/MiningBenchMod/mb_cmdstate.hpp
// WHAT: state of the test-command poller: timing constants, the DockJob record, the command state, and the MiningCmd.txt line reader and parser.
// WHY separate: this state is shared by the command, job and test-tab code, so it lives in one place.
// DEPENDS ON: mb_common (trim, to_int, get_dll_dir, STR and standard types).
// USED BY: mb_dockjob, mb_testcmd, mb_steer, mb_rocks.
// GAME FACTS: none. The command channel is a file: the Lua Test tab writes MiningCmd.txt (brief Update 23 mechanism).
//   Timing values below come from the logs and brief where noted; the rest are tuning values (see each constant).
#pragma once

namespace
{
    // ---------------- test command poller (always on, no INI key) ----------------
    // Lua writes MiningCmd.txt (next to the dll): "<seq>|<ship_id>|<cmd>". A line runs once when seq > last executed seq.
    // WHY a file: the Lua side has no direct call into the mod, so the Test tab writes a command line and the mod polls for it (brief Update 23).
    // Two Lua writers keep separate seq counters: a command runs when its seq differs from the last one run (cmd_poll), so a wrap never blocks it.

    // WHAT: seconds between reads of MiningCmd.txt (0.25 s). WHY: tuning value, not from the logs (ported as is).
    constexpr double CMD_POLL_S = 0.25;


    // WHAT: seconds to wait after UpdateShipOrderInput before reading DockID back (3.0 s). WHY: the DockID readback window of the dock job (log, Update 23 coder note).
    constexpr double CMD_DOCK_APPLY_S = 3.0;   // UpdateShipOrderInput DockID readback window

    // WHAT: seconds to wait for the new DeliverOrder to show up in ship.OrderIDs (5.0 s). WHY: the dock job's order-growth wait (log, Update 23 coder note: "poll OrderIDs 5 s").
    constexpr double CMD_ORDER_TIMEOUT_S = 5.0;

    // WHAT: longest time a dock job watches the autopilot before it stops (180 s). WHY: the watch window of the dock job (log, test-tab dock run: "watch to target dock, 180 s").
    constexpr double CMD_WATCH_S = 180.0;

    // WHAT: seconds between watch log lines during a dock job (1.0 s). WHY: unknown (ported as is); it only sets how often the watch is logged.
    constexpr double CMD_WATCH_LOG_S = 1.0;

    // ---------------- dock job record ----------------

    // WHAT: one dock job: sends one ship to its mining (unload) or refuel dock. The job runs over many ticks: wait for the new order, apply the dock, then watch the autopilot.
    // WHY: a multi-tick state machine needs its state stored between ticks. There is at most one job per ship, and a job never blocks other commands.
    struct DockJob
    {
        int sid = 0, dock = 0, seq = 0;   // ship id; target dock id; seq of the command that started the job
        std::string cmd;               // dock_mining | dock_refuel | undock (dock_auto is resolved to one of these)
        std::string kind;              // command as sent: dock_mining | dock_refuel | dock_auto | undock (MiningStatusX job column)
        int stage = 0;                 // 0 = wait OrderIDs growth, 1 = apply dock (UpdateShipOrderInput), 2 = watch autopilot, 3 = order build, 4 = far trip warp settle, then the dock order (see mb_dockjob.hpp)
        std::vector<int> unl;          // dock_mining: ore indices unloaded at dock (set at job start, used when the order is issued)
        int base = 0, new_order = 0, new_index = 0;   // base = OrderIDs count before the new order; new_order = id of the new DeliverOrder; new_index = its index in Orders (SkipToOrder target)
        double t = 0.0, log_acc = 0.0;   // t = seconds since the job started (summed from the tick step); log_acc = seconds since the last watch log line
        double skip_wait = -1.0;       // stage 2: SkipToOrder due at this t (-1 = none)
        double skip_t = -1.0;          // t of the SkipToOrder call (fallback check)
        bool fb_done = false;          // fallback step already done (its exact reason: WHY unknown beyond the name, ported as is)
        double p0[3] = {0, 0, 0};      // position at job start (undock clear check)
        double p_skip[3] = {0, 0, 0};  // position at the SkipToOrder call
        double stall_t = -1.0;         // dock leg: seconds the ship moved < 1 m (-1 = not started); at 10 s the leg is retried
        double p_stall[3] = {0, 0, 0}; // position the stall timer measures from
        int retries = 0;               // stall retries done (max DOCK_STALL_MAX)
        double prog_best = -1.0, prog_t = 0.0;   // timeout clock: distance at last progress reset, job t of that reset
    };

    // WHAT: the active dock jobs, one entry per ship that has a job running. WHY: the job state machine lives here, and jobs_tick walks the list each tick.
    std::vector<DockJob> g_jobs;
    // WHAT: dock picked by the Dock picker (dock_to:<id>), per ship id; overrides the ini dock for that ship's next dock job. WHY: one ship docks at a chosen dock without changing its ini.
    std::map<int, int> g_dock_pick;
    // WHAT: ore indices (ORE_KEY order) this ship unloads on its current dock trip, per ship id. WHY: one dock takes only the ores whose list holds it; the other ores stay in cargo for the next trip, and MS_HOLD waits only for these.
    std::map<int, std::vector<int>> g_unl_ores;
    // WHAT: last result text of the dock/undock job and of the move job, per ship id. WHY: the mining loop (mb_mining.hpp) reads how a job ended.
    std::map<int, std::wstring> g_job_res, g_mv_res;

    // WHAT: state of the command poller: the last seq run, the last ship and result, and the text status. WHY: one instance, because the poller is global; the status file is written from it.
    struct CmdState
    {
        int seq = 0;                   // last executed seq (seeded from the file at load)
        int ship = 0;                  // ship of the last command
        int order_id = 0;              // DeliverOrder id of the last dock job of that ship (status readback), 0 = none
        std::string last_cmd = "none"; // name of the last command, shown in the status
        std::wstring result = STR("waiting");   // text result of the last command, shown in the status
        std::string bad_line;          // last malformed line logged (logged once)
    };

    // WHAT: the one command-poller state instance. WHY: see CmdState.
    CmdState g_cmd;

    // WHAT: seconds since the last command poll, summed from the tick step. WHY: tick-driven timers, not delayed callbacks (delayed callbacks that called FindFirstOf crashed the game).
    double g_cmd_poll_acc = 0.0;

    // ---------------- command file reader and parser ----------------

    // WHAT: reads the first line of MiningCmd.txt, trimmed. false when the file does not exist.
    // WHY: one line per command, read with plain std::ifstream so nothing outside the mod is needed to read it.
    bool read_cmd_line(std::string& line)
    {
        std::ifstream f(get_dll_dir() / "MiningCmd.txt", std::ios::binary);
        if (!f) return false;
        std::getline(f, line);
        line = trim(line);
        return true;
    }


}
