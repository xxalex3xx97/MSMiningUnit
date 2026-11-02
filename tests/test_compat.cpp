// FILE: tests/test_compat.cpp
// WHAT: unit checks for mb_pattern.hpp (pat_parse, pat_count) and mb_compat.hpp (Compat, SafeMode). No game needed.
// WHY: the feature gate and the signature scan decide what the mod turns on; these checks keep their rules fixed.
// RUN: tests\run_compat.ps1 (builds with cl, runs, exit code 0 = all PASS).
// DEPENDS ON: mods/MiningBenchMod/mb_pattern.hpp, mods/MiningBenchMod/mb_compat.hpp.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "mb_pattern.hpp"
#include "mb_compat.hpp"

static int g_pass = 0;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } ++g_pass; } while (0)

static std::vector<int16_t> pat(std::initializer_list<int> v)
{
    std::vector<int16_t> p;
    for (int x : v) p.push_back((int16_t)x);
    return p;
}

static void test_pat_parse()
{
    std::vector<int16_t> out;
    const std::string good = "48 89 5C 24 ? 08 48 89 74 24 10 57 48 83 EC 20 E8";
    CHECK(pat_parse(good, out));
    CHECK(out.size() == 17);
    CHECK(out[0] == 0x48 && out[2] == 0x5C && out[4] == -1 && out[5] == 0x08);

    CHECK(pat_parse("48 89 ?? 5C 24 08 48 89 74 24 10 57 48 83 EC 20 E8", out));  // "??" is a wildcard too
    CHECK(out[2] == -1);

    CHECK(!pat_parse("48 89 GG 5C 24 08 48 89 74 24 10 57 48 83 EC 20 E8", out));  // bad hex
    CHECK(!pat_parse("48 89 5 5C 24 08 48 89 74 24 10 57 48 83 EC 20 E8", out));   // one digit token
    CHECK(!pat_parse("? 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 E8", out));   // leading wildcard
    CHECK(!pat_parse("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20", out));        // 15 fixed bytes
    CHECK(!pat_parse("", out));
    CHECK(pat_parse("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 E8 ?", out));    // 16 fixed, trailing wildcard ok
}

static void test_pat_count()
{
    size_t first = 99;
    uint8_t zero_hit[] = { 0, 1, 2, 3, 0 };
    CHECK(pat_count(zero_hit, 5, pat({ 9 }).data(), 1, &first) == 0);
    CHECK(first == 0);

    CHECK(pat_count(zero_hit, 5, pat({ 1, 2, 3 }).data(), 3, &first) == 1);
    CHECK(first == 1);

    uint8_t two_hits[] = { 1, 2, 3, 1, 2, 3 };
    CHECK(pat_count(two_hits, 6, pat({ 1, 2, 3 }).data(), 3, &first) == 2);
    CHECK(first == 0);

    uint8_t three_hits[] = { 1, 2, 3, 1, 2, 3, 1, 2, 3 };
    CHECK(pat_count(three_hits, 9, pat({ 1, 2, 3 }).data(), 3, &first) == 2);  // counting stops at 2

    uint8_t wild[] = { 7, 5, 9, 0, 1, 2, 3 };
    CHECK(pat_count(wild, 7, pat({ 7, -1, 9 }).data(), 3, &first) == 1);
    CHECK(first == 0);
    uint8_t wild2[] = { 7, 5, 9, 0, 7, 6, 8 };
    CHECK(pat_count(wild2, 7, pat({ 7, -1, 8 }).data(), 3, &first) == 1);
    CHECK(first == 4);

    uint8_t at_end[] = { 0, 0, 1, 2, 3 };
    CHECK(pat_count(at_end, 5, pat({ 1, 2, 3 }).data(), 3, &first) == 1);
    CHECK(first == 2);

    uint8_t small[] = { 1, 2 };
    CHECK(pat_count(small, 2, pat({ 1, 2, 3 }).data(), 3, &first) == 0);  // pattern longer than buffer
}

static void test_compat()
{
    {
        Compat c;
        c.add(F_CORE, "core", true, "");
        c.add(F_STEER, "steer fn", false, "rva miss");
        c.add(F_WEAPONS, "weapons", true, "");
        c.add(F_DOCK, "dock", true, "");
        c.add(F_WARP, "warp", true, "");
        c.done = true;
        CHECK(!c.ok(F_MINING));
        CHECK(c.ok(F_WARP));
        CHECK(c.usable(F_WARP));
        CHECK(!c.usable(F_STEER));
        CHECK(c.reason(F_MINING) == "needs STEER");
        CHECK(c.reason(F_STEER) == "steer fn: rva miss");
        CHECK(c.reason(F_CORE) == "");
        CHECK(c.summary().find("STEER=DISABLED") != std::string::npos);
        CHECK(c.summary().find("WARP=OK") != std::string::npos);
    }
    {
        Compat c;
        c.add(F_CORE, "core", true, "");
        c.add(F_WARP, "warp fn", false, "pattern not unique");
        c.add(F_MINING, "mining", true, "");
        c.add(F_STEER, "steer", true, "");
        c.add(F_WEAPONS, "weapons", true, "");
        c.add(F_DOCK, "dock", true, "");
        c.done = true;
        CHECK(!c.ok(F_WARPREAL));
        CHECK(c.reason(F_WARPREAL) == "needs WARP");
        CHECK(c.ok(F_MINING));
        CHECK(c.usable(F_MINING));
    }
    {
        Compat c;
        c.add(F_CORE, "core", true, "");
        CHECK(!c.usable(F_CORE));  // not done yet
        c.done = true;
        CHECK(c.usable(F_CORE));
    }
}

static void test_safemode()
{
    SafeMode s;
    CHECK(s.limit == 5);
    for (int i = 0; i < 4; ++i) CHECK(!s.fault());
    CHECK(s.total == 4 && s.streak == 4 && !s.tripped);
    s.ok();
    CHECK(s.streak == 0);
    for (int i = 0; i < 4; ++i) CHECK(!s.fault());
    CHECK(!s.tripped && s.total == 8);

    SafeMode t;
    for (int i = 0; i < 4; ++i) CHECK(!t.fault());
    CHECK(t.fault());  // 5th in a row trips
    CHECK(t.tripped && t.total == 5);
}

int main()
{
    test_pat_parse();
    test_pat_count();
    test_compat();
    test_safemode();
    std::printf("PASS %d\n", g_pass);
    return 0;
}
