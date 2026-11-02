// FILE: mods/MiningBenchMod/mb_natives.hpp
// WHAT: finds the three game natives used by chunk hold (get_system, set level, apply) by byte pattern in the running exe (package E).
//       nat_resolve() scans the executable sections once; each pattern must match exactly once, else every pointer stays null and why says which pattern failed.
// WHY: a fixed RVA only works for one exe. A pattern lookup fails closed after a game patch: chunk hold turns off, the rest of the mod keeps running.
// SAFETY: the scan reads game memory inside SEH (nat_seh_count, nat_seh_hash), so an unreadable page is a failed lookup, not a crash.
// LOG: 'COMPAT natives ...' lines. The FNV-1a 64 of the first 64 bytes of each native is logged for information only (not checked).
// DEPENDS ON: mb_common.hpp (log_line, STR, widen, Windows.h), mb_pattern.hpp (pat_parse, pat_count).
// USED BY: mb_chunkhold.hpp (g_nat pointers, nat_resolve), mb_compat_run.hpp (natives row of the self-test).
#pragma once

#include <Windows.h>
#include "mb_pattern.hpp"

namespace
{
    // Patterns of the three natives in the tested exe (build size 149200896). Each must match exactly once.
    constexpr const char* NAT_PAT_SYSTEM = "48 83 EC 28 4C 8B 49 28 4D 85 C9 75 07 33 C0 48 83 C4 28 C3 41 8B 41 48";   // rva 0x425f2e0 len 24 unique fnv1a64_64=0x18b8bd3d3c7fc926
    constexpr const char* NAT_PAT_SETLEVEL = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F2 41 8B E8 48 8B F9";   // rva 0x42512e0 len 29 unique fnv1a64_64=0x7d4bca3907526cab
    constexpr const char* NAT_PAT_APPLY = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F2 48 8B F9 48 8B CE 45 33 C0";   // rva 0x424cbc0 len 27 unique fnv1a64_64=0x5eb122869739815d
    constexpr size_t NAT_INFO_HASH_LEN = 64;

    // one resolved native pair set: absolute addresses (null until resolved), why = first failure text
    struct Nat
    {
        void* system = nullptr;
        void* setlevel = nullptr;
        void* apply = nullptr;
        bool tried = false;
        bool resolved = false;
        std::wstring why;
    };
    Nat g_nat;

    // one executable scan range of the exe
    struct NatRange { const uint8_t* p; size_t n; };

    // ---- SEH helpers: POD arguments only (MSVC forbids __try in functions with C++ objects) ----

    // WHAT: pat_count over buf inside SEH. false when a read faults.
    bool nat_seh_count(const uint8_t* buf, size_t n, const int16_t* pat, size_t plen, size_t& cnt, size_t& first)
    {
        __try { cnt = pat_count(buf, n, pat, plen, &first); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: FNV-1a 64 of n bytes at p inside SEH. false when a read faults.
    bool nat_seh_hash(const uint8_t* p, size_t n, uint64_t& out)
    {
        __try
        {
            uint64_t h = 0xcbf29ce484222325ULL;
            for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ULL; }
            out = h;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: "0x<hex>" of a value.
    std::wstring nat_hex(uint64_t v) { wchar_t b[32]; swprintf(b, 32, L"0x%llx", static_cast<unsigned long long>(v)); return b; }

    // WHAT: the scan ranges: the .text section, or every executable section when there is no .text.
    std::vector<NatRange> nat_ranges(const uint8_t* base)
    {
        std::vector<NatRange> out;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        const WORD count = nt->FileHeader.NumberOfSections;
        for (WORD i = 0; i < count; ++i)
            if (memcmp(sec[i].Name, ".text\0\0\0", 8) == 0)
                return { NatRange{ base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize } };
        for (WORD i = 0; i < count; ++i)
            if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
                out.push_back(NatRange{ base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize });
        return out;
    }

    // WHAT: resolves the three natives once. Each pattern must match exactly once in the scan ranges, else all pointers are cleared
    //       and why names the pattern. Returns resolved. Later calls return the first result.
    // WHY: chunk hold calls these with raw pointers, so a wrong match would call the wrong function.
    bool nat_resolve()
    {
        if (g_nat.tried) return g_nat.resolved;
        g_nat.tried = true;
        const auto fail = [](const std::wstring& why) {
            g_nat.system = g_nat.setlevel = g_nat.apply = nullptr;
            g_nat.why = why;
            log_line(STR("COMPAT natives failed: ") + why);
            return false;
        };
        const uint8_t* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return fail(STR("no module base"));
        const std::vector<NatRange> ranges = nat_ranges(base);
        if (ranges.empty()) return fail(STR("no executable section"));

        struct Want { const char* name; const char* text; void** out; };
        const Want wants[3] = {
            { "system", NAT_PAT_SYSTEM, &g_nat.system },
            { "setlevel", NAT_PAT_SETLEVEL, &g_nat.setlevel },
            { "apply", NAT_PAT_APPLY, &g_nat.apply },
        };
        for (const Want& w : wants)
        {
            std::vector<int16_t> pat;
            if (!pat_parse(w.text, pat)) return fail(widen(w.name) + STR(" pattern invalid"));
            size_t total = 0;
            const uint8_t* hit = nullptr;
            for (const NatRange& r : ranges)
            {
                size_t c = 0, f = 0;
                if (!nat_seh_count(r.p, r.n, pat.data(), pat.size(), c, f)) return fail(widen(w.name) + STR(" scan faulted"));
                if (c && !hit) hit = r.p + f;
                total += c;
            }
            if (total != 1) return fail(widen(w.name) + STR(" pattern matched ") + std::to_wstring(total) + STR(" times"));
            *w.out = const_cast<uint8_t*>(hit);
        }
        g_nat.resolved = true;
        log_line(STR("COMPAT natives resolved system=") + nat_hex(reinterpret_cast<uint64_t>(g_nat.system)) +
                 STR(" setlevel=") + nat_hex(reinterpret_cast<uint64_t>(g_nat.setlevel)) +
                 STR(" apply=") + nat_hex(reinterpret_cast<uint64_t>(g_nat.apply)));
        // informational: the first 64 bytes of each native, so a log shows which build was matched
        void* fns[3] = { g_nat.system, g_nat.setlevel, g_nat.apply };
        const wchar_t* names[3] = { L"system", L"setlevel", L"apply" };
        for (int i = 0; i < 3; ++i)
        {
            uint64_t h = 0;
            if (nat_seh_hash(static_cast<const uint8_t*>(fns[i]), NAT_INFO_HASH_LEN, h))
                log_line(STR("COMPAT natives fnv64 ") + std::wstring(names[i]) + STR(" = ") + nat_hex(h) + STR(" (info only)"));
        }
        return true;
    }
}
