// FILE: mods/MiningBenchMod/mb_pattern.hpp
// WHAT: byte pattern parser and matcher for the native signature scan (package E). pat_parse turns "48 89 ? 5C ..." into int16 tokens (-1 = wildcard).
//       pat_count counts matches in a buffer (stops at 2) and reports the first match offset.
// WHY: native functions are found by byte pattern, not a fixed RVA, so a game patch makes the lookup fail closed instead of calling the wrong code.
// DEPENDS ON: standard library only.
// USED BY: mb_compat.hpp (none), the native lookup (later slice), tests/test_compat.cpp.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // WHAT: parse a space separated pattern. '?' or '??' = wildcard (-1), two hex digits = byte. false on a bad token,
    //       a leading wildcard, or fewer than 16 fixed bytes (too short to be unique).
    bool pat_parse(const std::string& s, std::vector<int16_t>& out)
    {
        out.clear();
        size_t i = 0;
        while (i < s.size())
        {
            if (s[i] == ' ') { ++i; continue; }
            size_t j = s.find(' ', i);
            if (j == std::string::npos) j = s.size();
            std::string t = s.substr(i, j - i);
            i = j;
            if (t == "?" || t == "??") { out.push_back(-1); continue; }
            if (t.size() != 2 || !isxdigit((unsigned char)t[0]) || !isxdigit((unsigned char)t[1])) { out.clear(); return false; }
            out.push_back((int16_t)std::stoi(t, nullptr, 16));
        }
        if (out.empty() || out[0] < 0) { out.clear(); return false; }
        size_t fixed = 0;
        for (int16_t v : out) if (v >= 0) ++fixed;
        if (fixed < 16) { out.clear(); return false; }
        return true;
    }

    // WHAT: number of positions where pat matches buf (0, 1 or 2; counting stops at 2). *first = offset of the first match (0 when none).
    //       pat[0] must be fixed (pat_parse guarantees it); it drives a memchr scan.
    size_t pat_count(const uint8_t* buf, size_t n, const int16_t* pat, size_t plen, size_t* first)
    {
        if (first) *first = 0;
        if (!buf || !pat || plen == 0 || plen > n || pat[0] < 0) return 0;
        size_t cnt = 0;
        const uint8_t* p = buf;
        const uint8_t* end = buf + (n - plen) + 1;
        while (p < end)
        {
            const uint8_t* hit = (const uint8_t*)memchr(p, (uint8_t)pat[0], (size_t)(end - p));
            if (!hit) break;
            bool ok = true;
            for (size_t k = 1; k < plen && ok; ++k)
                if (pat[k] >= 0 && hit[k] != (uint8_t)pat[k]) ok = false;
            if (ok)
            {
                if (cnt == 0 && first) *first = (size_t)(hit - buf);
                if (++cnt >= 2) return cnt;
            }
            p = hit + 1;
        }
        return cnt;
    }
}
