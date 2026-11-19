// FILE: mods/ShipControl/sc_math.hpp
// WHAT: quaternion, 3-vector and angle helpers on plain doubles (dot3, norm3, dist3, qmul, qconj, cross3, rotvec, quat_angle_deg, quat_rot, qcal_*). Copies of MiningBenchMod mb_math.hpp (MV_PI renamed SC_PI).
// WHY separate: ShipControl must not include mining code. Same formulas as mb_math.hpp.
// DEPENDS ON (exact): <algorithm>, <cmath>
// USED BY: sc_speed.hpp (dist3)
// GAME FACTS: none. Units are the caller's (raw or m).
#pragma once

#include <algorithm>
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
    constexpr double SC_PI = 3.14159265358979323846;

    // WHAT: shortest angle in deg between orientations a and b (0 = same orientation).
    // WHY: mb_steer.hpp uses it as the angular rate (angle between two samples / time), so it must be the short-way angle.
    double quat_angle_deg(const double a[4], const double b[4])
    {
        double qc[4], dq[4], rv[3];
        qconj(a, qc);
        qmul(qc, b, dq);
        rotvec(dq, rv);
        return std::sqrt(dot3(rv, rv)) * 180.0 / SC_PI;
    }

    // WHAT: o = v rotated by unit quaternion q ({W,X,Y,Z}): t = 2 * (qv x v), o = v + w * t + qv x t.
    // WHY: maps a ship-object axis into world space for the object-based aim frame in mb_rocks.hpp.
    void quat_rot(const double q[4], const double v[3], double o[3])
    {
        const double qv[3] = {q[1], q[2], q[3]};
        double c[3], t[3], c2[3];
        cross3(qv, v, c);
        for (int i = 0; i < 3; ++i) t[i] = 2.0 * c[i];
        cross3(qv, t, c2);
        for (int i = 0; i < 3; ++i) o[i] = v[i] + q[0] * t[i] + c2[i];
    }

    // WHAT: unit axis for idx 0..5 = +X, -X, +Y, -Y, +Z, -Z (ship object local axes).
    // WHY: the calibration tries these six candidates against the actor axes.
    void qcal_axis(int idx, double v[3])
    {
        v[0] = v[1] = v[2] = 0.0;
        v[idx / 2] = (idx % 2) ? -1.0 : 1.0;
    }

    // WHAT: world direction of object axis idx under quaternion q; qmode 1 uses conj(q) instead.
    // WHY: the game may store the rotation either way round; qcal_pick finds which one matches the actor.
    void qcal_out(const double q[4], int qmode, int idx, double o[3])
    {
        double qc[4], v[3];
        if (qmode) qconj(q, qc); else std::copy(q, q + 4, qc);
        qcal_axis(idx, v);
        quat_rot(qc, v, o);
    }

    // WHAT: find the axis map (qmode, ax[3]) so that the object axes ax[0..2] under q match the actor fw / R / U (dot >= 0.98 each).
    //       true only when exactly one qmode matches. ax[] is the object axis index for fw, R, U.
    // WHY: the object-to-actor axis convention is not known up front; one ship with an actor teaches it.
    bool qcal_pick(const double q[4], const double fw[3], const double R[3], const double U[3], int& qmode, int ax[3])
    {
        const double* tgt[3] = {fw, R, U};
        int hits = 0;
        for (int qm = 0; qm < 2; ++qm)
        {
            int cand[3] = {0, 0, 0};
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k)
            {
                double best = -2.0;
                for (int idx = 0; idx < 6; ++idx)
                {
                    double o[3];
                    qcal_out(q, qm, idx, o);
                    const double d = dot3(o, tgt[k]);
                    if (d > best) { best = d; cand[k] = idx; }
                }
                ok = best >= 0.98;
            }
            if (ok) { ++hits; qmode = qm; std::copy(cand, cand + 3, ax); }
        }
        return hits == 1;
    }

    // WHAT: writes the world axes of object axes ax[0..2] under q (qmode as in qcal_out) to fw / R / U.
    // WHY: the object-based aim frame in mb_rocks.hpp uses this once the map is calibrated.
    void qcal_apply(const double q[4], int qmode, const int ax[3], double fw[3], double R[3], double U[3])
    {
        qcal_out(q, qmode, ax[0], fw);
        qcal_out(q, qmode, ax[1], R);
        qcal_out(q, qmode, ax[2], U);
    }
}
