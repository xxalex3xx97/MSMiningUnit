// FILE: mods/MiningBenchMod/mb_watchdog.hpp
// WHAT: freeze watchdog. A thread checks g_heartbeat every WD_POLL_S. No beat for WD_STALL_S = stall: it suspends the game thread,
//       copies its RIP and 512 stack slots, resumes it, maps the addresses to module+offset and appends one line to MiningFreeze.txt.
//       Repeats every WD_REPEAT_S while stalled (max WD_MAX_LINES lines). "recovered after N s" when the beat returns.
// WHY: the game hung hard (window dead, all threads in Wait) with no crash report. This records where the game thread was.
// RULES: no UObject access, no Output::send, no loader lock while the game thread is suspended (copy first, map after resume),
//        SEH around the suspend window, the thread is always resumed. Never crashes the game: any failure = skip the line.
// DEPENDS ON: mb_wd_logic.hpp (rules, atomics), mb_common.hpp (get_dll_dir, g_last_step).
// USED BY: dllmain.cpp (wd_start in the ctor, wd_stop in the dtor).
#pragma once
#include <tlhelp32.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include "mb_wd_logic.hpp"

namespace mb_wd
{
    constexpr int STACK_SLOTS = 512;   // stack words copied from RSP
    constexpr int MAX_FRAMES = 32;     // frames written per line

    // WHAT: raw copy of the suspended thread. POD only (filled inside __try).
    struct Raw { std::uint64_t rip; std::uint64_t rsp; std::uint64_t slots[STACK_SLOTS]; int n; };
    struct Mod { std::uintptr_t lo; std::uintptr_t hi; wchar_t name[64]; };

    inline std::thread g_thread;
    inline std::atomic<bool> g_stop{false};
    inline Mod g_mods[256];        // written only by the watchdog thread
    inline int g_mod_n = 0;
    inline std::wstring g_dir;     // computed in wd_start (not on the watchdog thread: get_dll_dir takes the loader lock)

    // WHAT: copies the module list (name, image range). Called only while the game is healthy.
    // WHY: the list is used later while the game thread may be stuck, so it cannot be read then.
    inline void snapshot_modules()
    {
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (s == INVALID_HANDLE_VALUE) return;
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        int n = 0;
        if (Module32FirstW(s, &me))
        {
            do
            {
                if (n >= 256) break;
                g_mods[n].lo = reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
                g_mods[n].hi = g_mods[n].lo + me.modBaseSize;
                wcsncpy_s(g_mods[n].name, me.szModule, _TRUNCATE);
                ++n;
            } while (Module32NextW(s, &me));
        }
        CloseHandle(s);
        g_mod_n = n;
    }

    // WHAT: suspends the game thread, copies RIP/RSP and the stack words, always resumes it. Returns false when it could not read.
    // WHY: only POD and kernel calls inside the suspend window (no loader lock, no allocation, no C++ unwinding).
    inline bool grab(std::uint32_t tid, Raw& out)
    {
        out.n = 0;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
        if (!th) return false;
        bool got = false;
        if (SuspendThread(th) != static_cast<DWORD>(-1))
        {
            __try
            {
                CONTEXT c{};
                c.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(th, &c))
                {
                    out.rip = c.Rip;
                    out.rsp = c.Rsp;
                    static const SIZE_T sizes[] = { STACK_SLOTS * sizeof(std::uint64_t), 128 * sizeof(std::uint64_t), 0 };
                    for (SIZE_T sz : sizes)
                    {
                        SIZE_T rd = 0;
                        if (sz == 0) break;
                        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(c.Rsp), out.slots, sz, &rd) && rd == sz)
                        {
                            out.n = static_cast<int>(sz / sizeof(std::uint64_t));
                            got = true;
                            break;
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                got = false;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
        return got;
    }

    // WHAT: module+offset text for an address inside a loaded module image, else false.
    inline bool locate(std::uintptr_t v, char* buf, std::size_t cap)
    {
        for (int i = 0; i < g_mod_n; ++i)
        {
            if (v >= g_mods[i].lo && v < g_mods[i].hi)
            {
                char nm[64];
                for (int k = 0; k < 63; ++k) { nm[k] = static_cast<char>(g_mods[i].name[k]); if (!nm[k]) break; nm[63] = 0; }
                std::snprintf(buf, cap, "%s+0x%llx", nm, static_cast<unsigned long long>(v - g_mods[i].lo));
                return true;
            }
        }
        return false;
    }

    // WHAT: appends one line to MiningFreeze.txt in the dlls folder. Failures are dropped.
    inline void write_line(const std::string& line)
    {
        std::ofstream f(std::filesystem::path(g_dir) / L"MiningFreeze.txt", std::ios::app | std::ios::binary);
        if (f) f << line << "\n";
    }

    // WHAT: "YYYY-MM-DD hh:mm:ss" from the local clock (no locks).
    inline std::string stamp()
    {
        SYSTEMTIME t{};
        GetLocalTime(&t);
        char b[32];
        std::snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        return b;
    }

    // WHAT: last non-empty line of MiningCmd.txt (the last command the Lua side or the test tab sent), trimmed to 200 chars.
    inline std::string last_cmd()
    {
        std::ifstream f(std::filesystem::path(g_dir) / L"MiningCmd.txt", std::ios::binary);
        std::string last, cur;
        while (f && std::getline(f, cur)) { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); if (!cur.empty()) last = cur; }
        if (last.size() > 200) last.resize(200);
        return last;
    }

    // WHAT: g_last_step (wide, written by the game thread) as narrow text. Bounded copy with terminator, so a torn read stays in the buffer.
    // WHY: the game thread may be writing it right now; a torn label is acceptable, a crash is not.
    inline std::string step_text()
    {
        std::string s;
        for (int i = 0; i < 159; ++i) { wchar_t c = g_last_step[i]; if (!c) break; s.push_back(c < 128 ? static_cast<char>(c) : '?'); }
        return s;
    }

    // WHAT: one stall line: time, stall length, tid, step, last command, RIP and up to 32 module+offset frames.
    inline void stall_line(int dump_no, double stalled_s, std::uint32_t tid, const Raw& r, bool have)
    {
        std::string s = stamp() + " FREEZE #" + std::to_string(dump_no) + " stalled " + std::to_string(static_cast<int>(stalled_s))
                      + " s tid=" + std::to_string(tid) + " step=" + step_text() + " cmd=[" + last_cmd() + "]";
        if (!have) { write_line(s + " (stack not read)"); return; }
        char buf[128];
        s += " rip=";
        if (locate(static_cast<std::uintptr_t>(r.rip), buf, sizeof(buf))) s += buf; else s += "?";
        s += " frames=";
        int k = 0;
        for (int i = 0; i < r.n && k < MAX_FRAMES; ++i)
        {
            if (!locate(static_cast<std::uintptr_t>(r.slots[i]), buf, sizeof(buf))) continue;
            s += (k ? " " : "") + std::string(buf);
            ++k;
        }
        write_line(s);
    }

    // WHAT: the watchdog thread body. Polls every WD_POLL_S, writes the stall lines, sleeps in 100 ms slices so stop is quick.
    inline void loop()
    {
        std::uint64_t last_hb = g_heartbeat.load();
        auto last_change = std::chrono::steady_clock::now();
        auto last_mods = last_change - std::chrono::hours(1);
        bool episode = false, first = false;
        auto stall_start = last_change, last_line = last_change;
        int lines = 0, dumps = 0;
        while (!g_stop.load())
        {
            for (int i = 0; i < 20 && !g_stop.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (g_stop.load()) break;
            auto now = std::chrono::steady_clock::now();
            if (!g_wd_enabled.load()) { last_change = now; episode = false; continue; }   // watchdog=0: idle (no stall lines)
            std::uint64_t hb = g_heartbeat.load();
            if (hb != last_hb)
            {
                last_hb = hb;
                last_change = now;
                if (episode)
                {
                    double secs = std::chrono::duration<double>(now - stall_start).count();
                    write_line(stamp() + " recovered after " + std::to_string(static_cast<int>(secs)) + " s");
                    episode = false;
                }
                if (now - last_mods > std::chrono::seconds(30)) { snapshot_modules(); last_mods = now; }
                continue;
            }
            double stalled = std::chrono::duration<double>(now - last_change).count();
            if (!wd_is_stalled(stalled)) continue;
            if (!episode) { episode = true; first = true; stall_start = last_change; }
            double since = std::chrono::duration<double>(now - last_line).count();
            if (!wd_line_due(lines, first, since)) continue;
            first = false;
            Raw r{};
            bool have = grab(g_game_tid.load(), r);
            stall_line(++dumps, stalled, g_game_tid.load(), r, have);
            ++lines;
            last_line = now;
        }
    }

    // WHAT: thread entry with SEH around the whole loop: an exception ends the watchdog, never the game.
    inline void thread_main()
    {
        __try { loop(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { }
    }

    // WHAT: starts the watchdog thread once. Called from the mod ctor (not on the game thread's hot path).
    inline void start(const std::wstring& dir)
    {
        if (g_thread.joinable()) return;
        g_dir = dir;
        g_stop = false;
        try { g_thread = std::thread(thread_main); } catch (...) {}
    }

    // WHAT: stops and joins the thread. Called from the mod dtor.
    inline void stop()
    {
        g_stop = true;
        if (g_thread.joinable()) g_thread.join();
    }
}
