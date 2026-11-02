// FILE: mods/MiningBenchMod/mb_compat.hpp
// WHAT: feature gate (package E). Each feature has a list of checks (CompatRow). ok(f) = no failed check of f and every dependency ok.
//       usable(f) = done && ok(f). reason(f) says why a feature is off. SafeMode counts faults and trips after N in a row.
// WHY: when the game is patched or a native lookup fails, the mod turns off only the features that need it instead of crashing.
// DEPENDS ON: standard library only.
// USED BY: dllmain.cpp and the feature code (later slices), tests/test_compat.cpp.
// Dependencies: MINING needs CORE, STEER, WEAPONS, DOCK; WARPREAL needs WARP; every other feature needs CORE.
#pragma once

#include <string>
#include <vector>

namespace
{
    enum Feature { F_CORE, F_STEER, F_WEAPONS, F_DOCK, F_WARP, F_WARPREAL, F_CHUNKHOLD, F_MINING, F_COUNT };

    // WHAT: feature name as shown in logs and summary.
    std::string feature_name(Feature f)
    {
        static const char* names[] = { "CORE", "STEER", "WEAPONS", "DOCK", "WARP", "WARPREAL", "CHUNKHOLD", "MINING" };
        return (f >= 0 && f < F_COUNT) ? names[f] : "?";
    }

    struct CompatRow
    {
        Feature f;
        std::string what;
        bool ok;
        std::string why;
    };

    struct Compat
    {
        std::vector<CompatRow> rows;
        bool done = false;  // set after every add() of the startup check

        void add(Feature f, const std::string& what, bool ok, const std::string& why)
        {
            rows.push_back({ f, what, ok, why });
        }

        // WHAT: direct dependencies of f.
        static std::vector<Feature> deps(Feature f)
        {
            switch (f)
            {
            case F_CORE: return {};
            case F_MINING: return { F_CORE, F_STEER, F_WEAPONS, F_DOCK };
            case F_WARPREAL: return { F_WARP };
            default: return { F_CORE };
            }
        }

        // WHAT: true when no row of f failed (dependencies not checked).
        bool own_ok(Feature f) const
        {
            for (const CompatRow& r : rows)
                if (r.f == f && !r.ok) return false;
            return true;
        }

        // WHAT: true when f and all its dependencies (recursively) are ok.
        bool ok(Feature f) const
        {
            if (!own_ok(f)) return false;
            for (Feature d : deps(f))
                if (!ok(d)) return false;
            return true;
        }

        bool usable(Feature f) const { return done && ok(f); }

        // WHAT: "what: why" of the first failed row of f, else "needs <dep>" for the first failing dependency, else "".
        std::string reason(Feature f) const
        {
            for (const CompatRow& r : rows)
                if (r.f == f && !r.ok) return r.what + ": " + r.why;
            for (Feature d : deps(f))
                if (!ok(d)) return "needs " + feature_name(d);
            return "";
        }

        // WHAT: one line per feature: "NAME=OK" or "NAME=DISABLED" (usable()).
        std::string summary() const
        {
            std::string s;
            for (int i = 0; i < F_COUNT; ++i)
            {
                Feature f = (Feature)i;
                s += feature_name(f) + (usable(f) ? "=OK\n" : "=DISABLED\n");
            }
            return s;
        }
    };

    // WHAT: fault counter. fault() returns true once `limit` faults came in a row (streak). ok() resets the streak. total counts all faults.
    struct SafeMode
    {
        int limit = 5;
        int streak = 0;
        int total = 0;
        bool tripped = false;

        void ok() { streak = 0; }

        bool fault()
        {
            ++total;
            if (++streak >= limit) tripped = true;
            return tripped;
        }
    };

    Compat g_compat;
}
