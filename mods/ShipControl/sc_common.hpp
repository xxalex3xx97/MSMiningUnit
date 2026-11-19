// FILE: mods/ShipControl/sc_common.hpp
// WHAT: UE4SS includes, widen(), log_line(). Trimmed from MiningBenchMod mb_common.hpp.
// WHY separate: dllmain.cpp needs the UE4SS log; the owner registry (sc_owner.hpp) must stay pure and does not include this.
// DEPENDS ON (exact): Mod/CppUserModBase.hpp, DynamicOutput/DynamicOutput.hpp; std string
// USED BY: dllmain.cpp
// GAME FACTS: none. alive() from mb_common.hpp is not copied: SC0 holds no UObject.
#pragma once

#include <string>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

using namespace RC;

// WHAT: narrow to wide (ASCII text only).
// WHY: UE4SS log and the C ABI use wide strings.
inline std::wstring widen(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

// WHAT: writes the text with a [ShipControl] prefix to the UE4SS log.
// WHY: the user checks UE4SS.log for these lines.
inline void log_line(const std::wstring& s)
{
    Output::send<LogLevel::Normal>(STR("[ShipControl] {}\n"), s);
}
