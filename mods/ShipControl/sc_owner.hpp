// FILE: mods/ShipControl/sc_owner.hpp
// WHAT: ship owner registry: one controller per ship. claim, release, release_all, owner_of. Pure std, no game types.
// WHY: a ship is driven by one behaviour at a time (mining, attack, follow, defend). Refusing a second owner keeps two controllers from fighting over the same ship.
// DEPENDS ON (exact): <map>, <string>
// USED BY: dllmain.cpp (exports), tests/test_shipcontrol.cpp
// GAME FACTS: none. Ship ids are plain ints chosen by the caller.
#pragma once

#include <map>
#include <string>

class ScOwners
{
public:
    // WHAT: gives ship to owner. Free ship -> owned by owner (true). Same owner -> true, no change. Other owner -> false, why = "owned by X".
    // WHY: the first claimer wins; the second gets a reason it can log instead of silently taking the ship.
    bool claim(int ship, const std::string& owner, std::string& why)
    {
        auto it = m_own.find(ship);
        if (it == m_own.end()) { m_own[ship] = owner; return true; }
        if (it->second == owner) return true;
        why = "owned by " + it->second;
        return false;
    }

    // WHAT: frees ship if owner holds it. Free ship -> true (no-op). Other owner -> false, why = "owned by X".
    // WHY: only the holder may let go, so one mod cannot free another mod's ship.
    bool release(int ship, const std::string& owner, std::string& why)
    {
        auto it = m_own.find(ship);
        if (it == m_own.end()) return true;
        if (it->second != owner) { why = "owned by " + it->second; return false; }
        m_own.erase(it);
        return true;
    }

    // WHAT: frees every ship held by owner. Returns how many were freed.
    // WHY: a behaviour that stops (mode change, mod unload) gives all its ships back in one call.
    int release_all(const std::string& owner)
    {
        int n = 0;
        for (auto it = m_own.begin(); it != m_own.end();)
        {
            if (it->second == owner) { it = m_own.erase(it); ++n; }
            else ++it;
        }
        return n;
    }

    // WHAT: true when owner may drive ship: free, or already held by owner. false = why set ("owned by X").
    // WHY: SC_zero and SC_watch use it, so one behaviour cannot stop or watch a ship another owner holds.
    bool may_act(int ship, const std::string& owner, std::string& why) const
    {
        const std::string held = owner_of(ship);
        if (held.empty() || held == owner) return true;
        why = "owned by " + held;
        return false;
    }

    // WHAT: owner name of ship, "" when free.
    std::string owner_of(int ship) const
    {
        auto it = m_own.find(ship);
        return it == m_own.end() ? std::string() : it->second;
    }

private:
    std::map<int, std::string> m_own;   // ship id -> owner name; absent = free
};
