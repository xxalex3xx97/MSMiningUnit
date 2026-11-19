// FILE: tests/test_shipcontrol.cpp
// WHAT: unit checks for the ShipControl owner registry (mods/ShipControl/sc_owner.hpp): claim, same-owner claim, refused claim, release, release_all, owner_of. No game needed.
// WHY: the registry decides which behaviour may drive a ship; the checks keep the refuse rules fixed.
// RUN: tests\run.ps1 (builds with cl, runs, exit code 0 = all PASS). Or by hand: see run.ps1.
// DEPENDS ON: mods/ShipControl/sc_owner.hpp (pure std, no UE headers).
// NOTE: CHECK counts failures and keeps going; the summary line prints the pass and fail counts.
#include <cstdio>
#include <string>

#include "sc_owner.hpp"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main()
{
    std::string why;

    // free ship: first claim wins
    {
        ScOwners o;
        CHECK(o.owner_of(7).empty());
        CHECK(o.claim(7, "mining", why));
        CHECK(o.owner_of(7) == "mining");
    }
    // same owner again: ok, still held
    {
        ScOwners o;
        o.claim(7, "mining", why);
        CHECK(o.claim(7, "mining", why));
        CHECK(o.owner_of(7) == "mining");
    }
    // other owner: refused, reason names the holder, holder unchanged
    {
        ScOwners o;
        o.claim(7, "mining", why);
        why.clear();
        CHECK(!o.claim(7, "attack", why));
        CHECK(why == "owned by mining");
        CHECK(o.owner_of(7) == "mining");
    }
    // release by holder frees the ship; a second owner can then claim it
    {
        ScOwners o;
        o.claim(7, "mining", why);
        CHECK(o.release(7, "mining", why));
        CHECK(o.owner_of(7).empty());
        CHECK(o.claim(7, "attack", why));
    }
    // release by non-holder refused, ship keeps its owner
    {
        ScOwners o;
        o.claim(7, "mining", why);
        why.clear();
        CHECK(!o.release(7, "attack", why));
        CHECK(why == "owned by mining");
        CHECK(o.owner_of(7) == "mining");
    }
    // release of a free ship is a no-op success
    {
        ScOwners o;
        CHECK(o.release(9, "mining", why));
    }
    // release_all frees only that owner's ships and returns the count
    {
        ScOwners o;
        o.claim(1, "mining", why);
        o.claim(2, "mining", why);
        o.claim(3, "attack", why);
        CHECK(o.release_all("mining") == 2);
        CHECK(o.owner_of(1).empty());
        CHECK(o.owner_of(2).empty());
        CHECK(o.owner_of(3) == "attack");
        CHECK(o.release_all("mining") == 0);
    }

    std::printf("test_shipcontrol: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
