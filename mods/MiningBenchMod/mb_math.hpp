// FILE: mods/MiningBenchMod/mb_math.hpp
// WHAT: quaternion, 3-vector and angle helpers on plain doubles. No UE calls.
// WHY separate: pure math, so it can be checked without the game.
// DEPENDS ON (exact): mb_common.hpp (for <cmath> only). This file has no include of its own, so include mb_common.hpp first.
// USED BY: mb_dockjob.hpp (dist3), mb_rocks.hpp (cross3), mb_steer.hpp (dist3, quat_angle_deg); dllmain.cpp includes it.
// GAME FACTS: quaternions come from the game Quat (4 PNum fields) divided by 65536 (mb_ue.hpp quat4). ASSUMPTION: the game Quat is unit length.
#pragma once

namespace
{
    // Quaternions are {W,X,Y,Z} in double[4]. Vectors are double[3]. Every helper writes to an output array and returns nothing.
    // WHAT: o = a * b, Hamilton product of two quaternions.
    // WHY: composes rotations; quat_angle_deg uses it for conj(a) * b, the relative rotation.
    void qmul(const double a[4], const double b[4], double o[4])
    {
        double w = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
        double x = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
        double y = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
        double z = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
        o[0] = w; o[1] = x; o[2] = y; o[3] = z;
    }

    // WHAT: conjugate {w, -x, -y, -z}, the inverse of a unit quaternion.
    // WHY: relative rotation in quat_angle_deg.
    void qconj(const double a[4], double o[4]) { double w = a[0], x = -a[1], y = -a[2], z = -a[3]; o[0] = w; o[1] = x; o[2] = y; o[3] = z; }

    // WHAT: o = a x b, 3-vector cross product.
    // WHY: builds an axis perpendicular to two vectors for the aim code in mb_rocks.hpp.
    void cross3(const double a[3], const double b[3], double o[3])
    {
        double x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
        o[0] = x; o[1] = y; o[2] = z;
    }

    // WHAT: dot product a . b.
    // WHY: base of norm3 and of rotvec's length check.
    double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

    // WHAT: vector length, in the same units as the input (raw or m).
    // WHY: speed and distance checks; the caller decides the units.
    double norm3(const double a[3]) { return std::sqrt(dot3(a, a)); }

    // WHAT: distance between points a and b, in the same units as the input (raw or m).
    // WHY: the approach and move jobs use it for ship-to-rock and ship-to-dock distance.
    double dist3(const double a[3], const double b[3])
    {
        double d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
        return norm3(d);
    }

    // WHAT: rotation vector (axis times angle, in rad) of a unit quaternion.
    // WHY: w < 0 is negated first, so the angle is the short way round (at most pi). A near-zero vector returns zero to avoid divide-by-zero.
    void rotvec(const double qr[4], double out[3])
    {
        double s = qr[0] < 0.0 ? -1.0 : 1.0;
        double vn = std::sqrt(qr[1] * qr[1] + qr[2] * qr[2] + qr[3] * qr[3]);
        if (vn < 1e-9) { out[0] = out[1] = out[2] = 0.0; return; }
        double ang = 2.0 * std::atan2(vn, s * qr[0]);
        for (int i = 0; i < 3; ++i) out[i] = s * qr[i + 1] / vn * ang;
    }

    // WHAT: pi, unitless. Used to convert rad to deg.
    constexpr double MV_PI = 3.14159265358979323846;

    // WHAT: shortest angle in deg between orientations a and b (0 = same orientation).
    // WHY: mb_steer.hpp uses it as the angular rate (angle between two samples / time), so it must be the short-way angle.
    double quat_angle_deg(const double a[4], const double b[4])
    {
        double qc[4], dq[4], rv[3];
        qconj(a, qc);
        qmul(qc, b, dq);
        rotvec(dq, rv);
        return std::sqrt(dot3(rv, rv)) * 180.0 / MV_PI;
    }
}
