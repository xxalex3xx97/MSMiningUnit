// FILE: mods/MiningBenchMod/mb_version.hpp
// WHAT: holds the mod version text (kVersion), which the DLL logs when it loads.
// WHY separate: it changes on every release, so there is one place to bump it.
// DEPENDS ON (exact): none
// USED BY: dllmain.cpp (constructor load log line)
// GAME FACTS: none
#pragma once

// WHAT: kVersion = mod version text, e.g. "0.0.1" (no units). Logged once by the MiningBenchMod constructor.
// WHY: anonymous namespace keeps the constant private to each .cpp that includes this header.
namespace
{
    constexpr const char* kVersion = "0.2.14";
    // informational: exe file size of the build the contract table was tested on (compat self-test logs a difference, no gate)
    constexpr unsigned long long kKnownBuildSize = 149200896ULL;
}
