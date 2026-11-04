// FILE: mods/MiningBenchMod/mb_wd_logic.hpp
// WHAT: watchdog rules and shared atomics, no Windows headers: stall threshold, repeat period, line cap, enabled flag, heartbeat counter.
// WHY separate: tests/test_logic.cpp includes this file without windows.h, so the stall decision and the rate limit are unit tested.
// USED BY: mb_config.hpp (g_wd_enabled from INI watchdog=), mb_testcmd.hpp (cmd_tick bumps g_heartbeat), mb_watchdog.hpp (the thread).
// GAME FACTS: none. The heartbeat is the game thread's cmd_tick (engine tick, once per frame).
#pragma once
#include <atomic>
#include <cstdint>

// WHAT: seconds without a heartbeat before the game thread counts as stalled.
// WHY: 10 s is far above a normal frame and above a slow load step; short hitches stay silent.
constexpr double WD_STALL_S = 10.0;
// WHAT: while still stalled, one more line every this many seconds.
constexpr double WD_REPEAT_S = 30.0;
// WHAT: hard cap on stall lines per game session (MiningFreeze.txt).
constexpr int WD_MAX_LINES = 20;
// WHAT: seconds between watchdog checks of the heartbeat.
constexpr double WD_POLL_S = 2.0;

// WHAT: the watchdog is on unless the INI has watchdog=0 (global key). Set by config reload.
inline std::atomic<bool> g_wd_enabled{true};
// WHAT: bumped by cmd_tick on every game-thread frame. Watchdog compares it between polls.
inline std::atomic<std::uint64_t> g_heartbeat{0};
// WHAT: thread id of the game thread, recorded by cmd_tick (the thread the watchdog suspends).
inline std::atomic<std::uint32_t> g_game_tid{0};

// WHAT: true when a heartbeat gap counts as a stall.
inline bool wd_is_stalled(double stalled_s) { return stalled_s >= WD_STALL_S; }

// WHAT: true when a stall line may be written now.
// WHY: the first line of a stall episode is always due (under the cap). Later lines wait WD_REPEAT_S since the last one.
// first_in_episode: true for the first line of this stall. since_last_line_s: seconds since the last line written.
inline bool wd_line_due(int lines_total, bool first_in_episode, double since_last_line_s)
{
    if (lines_total >= WD_MAX_LINES) return false;
    return first_in_episode || since_last_line_s >= WD_REPEAT_S;
}
