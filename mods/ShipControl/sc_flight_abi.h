// FILE: mods/ShipControl/sc_flight_abi.h
// WHAT: MsFlight struct, FL_* read groups and FF_* flag bits of the MSCore flight API (API 10), with the layout static_asserts.
// WHY: the speed monitor and the ShipControl logic read the same snapshot. Own header so mining can include it: mining cannot include sc_mscore.hpp (its anonymous-namespace g_ms collides with mining's g_ms).
// DEPENDS ON (exact): <cstddef>, <cstdint>. No windows.h, no UE headers.
// USED BY: sc_mscore.hpp (includes it); build.ps1 copies it into the MiningBenchMod mirror.
// GAME FACTS: none. The MsFlight block is a byte-for-byte copy of cpp/MSCore/msc_api.hpp (origin/main, lines 63-120). Keep the two copies identical.
#pragma once

#include <cstddef>
#include <cstdint>

// flight state of one ship (API 10, msc_flight.hpp). Caller sets size = sizeof(MsFlight) (mismatch -> -3). Each value is written only when its read worked; flags say which.
// Units: raw = game fixed point (65536 = 1 unit); uu = Unreal units (100 uu = 1 m); chunk = 2048 m cells.
struct MsFlight
{
    int32_t size;               // off 0: set by the caller
    int32_t flags;              // off 4: FF_* bits
    int32_t sid;                // off 8: ship id
    int32_t sys;                // off 12: solar system id (POSE)
    int32_t throttle;           // off 16: Throttle prop (STATE)
    int32_t steps;              // off 20: GetThrottleSteps (STATE)
    int32_t lp_thr;             // off 24: FlightModel LowPowerThrottle (POWER)
    int32_t pad;                // off 28
    int64_t ch[3];              // off 32: chunk location (POSE)
    int64_t max_mod_raw;        // off 56: GetModifiedMaxSpeed (LIMITS), raw
    int64_t max_base_raw;       // off 64: GetMaxSpeed (LIMITS), raw
    int64_t coll_radius_raw;    // off 72: CollisionRadius (STATE), raw
    double act_loc[3];          // off 80: actor location, uu (POSE)
    double fw[3];               // off 104: actor forward axis (POSE)
    double R[3];                // off 128: actor right axis (POSE)
    double U[3];                // off 152: actor up axis (POSE)
    double loc_raw[3];          // off 176: ship object Location, raw, chunk-relative (POSE)
    double quat[4];             // off 200: ship Quat W,X,Y,Z, already / 65536 (POSE)
    double grid_prod;           // off 232: PowerGrid.TotalProduction (POWER)
    double grid_cons;           // off 240: PowerGrid.TotalConsumption (POWER)
    double grid_eff;            // off 248: PowerGrid.ConsumerEfficiency (POWER)
    double lp_frac;             // off 256: FlightModel MaxLowPowerSpeed, fraction (POWER)
};

// MsFlight read groups (mask argument of MSCore_ship_flight)
constexpr int FL_POSE = 1;     // actor frame, ch, loc_raw, quat, sys
constexpr int FL_STATE = 2;    // throttle, steps, the 3 bool flags, collision radius
constexpr int FL_LIMITS = 4;   // both max speeds
constexpr int FL_POWER = 8;    // power grid, low-power flag, low-power limits

// MsFlight.flags bits
constexpr int FF_ALIVE = 1;        // ship found and alive
constexpr int FF_HAS_ACTOR = 2;    // Actor property non-null
constexpr int FF_ACTOR_FRAME = 4;  // act_loc / fw / R / U read
constexpr int FF_OBJ_POSE = 8;     // ch + loc_raw + quat read
constexpr int FF_AUTOPILOT = 16;   // bAutopilot true
constexpr int FF_DOCK_AP = 32;     // bDockingAutopilot true
constexpr int FF_TRAVELLING = 64;  // bTravelling true
constexpr int FF_GRID = 128;       // power grid numbers read
constexpr int FF_LP_KNOWN = 256;   // low-power flag readable
constexpr int FF_LP_ON = 512;      // low-power flag true
constexpr int FF_LP_LIMITS = 1024; // LowPowerThrottle and MaxLowPowerSpeed read
constexpr int FF_THROTTLE = 2048;  // Throttle read
constexpr int FF_STEPS = 4096;     // GetThrottleSteps read
constexpr int FF_COLL = 8192;      // CollisionRadius read and > 0

static_assert(sizeof(MsFlight) == 264, "MsFlight layout must match the MSCore native API 10 doc");
static_assert(offsetof(MsFlight, size) == 0 && offsetof(MsFlight, flags) == 4 && offsetof(MsFlight, sid) == 8, "MsFlight header offsets");
static_assert(offsetof(MsFlight, sys) == 12 && offsetof(MsFlight, throttle) == 16 && offsetof(MsFlight, steps) == 20, "MsFlight state offsets");
static_assert(offsetof(MsFlight, lp_thr) == 24 && offsetof(MsFlight, pad) == 28 && offsetof(MsFlight, ch) == 32, "MsFlight power/chunk offsets");
static_assert(offsetof(MsFlight, max_mod_raw) == 56 && offsetof(MsFlight, max_base_raw) == 64 && offsetof(MsFlight, coll_radius_raw) == 72, "MsFlight limit offsets");
static_assert(offsetof(MsFlight, act_loc) == 80 && offsetof(MsFlight, fw) == 104 && offsetof(MsFlight, R) == 128 && offsetof(MsFlight, U) == 152, "MsFlight frame offsets");
static_assert(offsetof(MsFlight, loc_raw) == 176 && offsetof(MsFlight, quat) == 200 && offsetof(MsFlight, grid_prod) == 232, "MsFlight pose/power offsets");
static_assert(offsetof(MsFlight, grid_cons) == 240 && offsetof(MsFlight, grid_eff) == 248 && offsetof(MsFlight, lp_frac) == 256, "MsFlight power offsets");
