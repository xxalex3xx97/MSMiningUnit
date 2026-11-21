// FILE: mods/ShipControl/sc_version.hpp
// WHAT: holds the mod version text (kVersion), which the DLL logs when it loads and build.ps1 writes into mscore.mod.
// WHY separate: it changes on every release, so there is one place to bump it.
// DEPENDS ON (exact): none
// USED BY: dllmain.cpp (constructor load log line), build.ps1 (-Install version)
// GAME FACTS: none
#pragma once

// WHAT: kVersion = mod version text, e.g. "0.1.0" (no units).
// WHY: anonymous namespace keeps the constant private to each .cpp that includes this header.
namespace
{
    constexpr const char* kVersion = "0.3.1";
}
