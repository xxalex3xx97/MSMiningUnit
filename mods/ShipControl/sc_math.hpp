// FILE: mods/ShipControl/sc_math.hpp
// WHAT: 3-vector helpers on plain doubles (dot3, norm3, dist3). Copy of the parts of MiningBenchMod mb_math.hpp the speed monitor needs.
// WHY separate: ShipControl must not include mining code. Same formulas as mb_math.hpp.
// DEPENDS ON (exact): <cmath>
// USED BY: sc_speed.hpp (dist3)
// GAME FACTS: none. Units are the caller's (raw or m).
#pragma once

#include <cmath>

namespace
{
    // WHAT: dot product a . b.
    // WHY: base of norm3.
    double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

    // WHAT: vector length.
    // WHY: distance and speed.
    double norm3(const double a[3]) { return std::sqrt(dot3(a, a)); }

    // WHAT: distance between points a and b.
    // WHY: speed = distance moved / dt.
    double dist3(const double a[3], const double b[3])
    {
        double d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        return norm3(d);
    }
}
