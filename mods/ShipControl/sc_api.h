/* FILE: mods/ShipControl/sc_api.h
 * WHAT: C ABI of ShipControl.dll. Other mods (MiningBenchMod) load the DLL and resolve these names with GetProcAddress.
 * WHY: plain C types only, so any compiler and any mod can call it. Append only: never change or reorder an existing export.
 * SC0 exports: api, caps, kill, claim, owner, release, release_all. Every export returns int; err/errlen = wide text reason (errlen wide chars, may be 0).
 * SC2b (API level 3) appends: move jobs (SC_move_start, SC_move_cancel, SC_move_active, SC_move_status, SC_tick_frame) and ScHost 80 bytes.
 * GAME FACTS: none. */
#ifndef SC_API_H
#define SC_API_H

#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
#include <stddef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SC_API_LEVEL 3

/* wchar_t must be 16-bit (Windows) to match the DLL */
#ifdef __cplusplus
static_assert(sizeof(wchar_t) == 2, "ShipControl ABI expects 16-bit wchar_t");
#endif

#ifndef SC_API_NO_PROTOTYPES   /* SC_API_NO_PROTOTYPES: the DLL defines it before including this header (it needs the types only). MSVC rejects dllexport definitions that also have these prototypes (C2375). */

/* API level: 1 = owner registry (SC0). 2 = speed monitor (SC2a): needs MSCore API >= 10 (0.8.13) at runtime. 3 = move jobs (SC2b). */
int SC_api(void);
/* capability bits: bit 0 = owner registry, bit 1 = speed monitor (MSCore usable), bit 2 (value 4) = move jobs (API 3). */
int SC_caps(void);
/* kill switch: on=1 refuses every control export, on=0 re-enables. 1 = ok, 0 = failed. */
int SC_kill(int on, wchar_t* err, int errlen);
/* claim ship for owner. 1 = owned now (free or already owner), 0 = refused (err says who owns it). */
int SC_claim(const wchar_t* owner, int ship, wchar_t* err, int errlen);
/* owner of ship: 1 = owned (name copied to out, NUL-terminated), 0 = free or failed. */
int SC_owner(int ship, wchar_t* out, int outlen);
/* release ship held by owner. 1 = released or was free, 0 = refused (another owner). */
int SC_release(const wchar_t* owner, int ship, wchar_t* err, int errlen);
/* release every ship held by owner. >= 0 = number released, -1 = failed. */
int SC_release_all(const wchar_t* owner, wchar_t* err, int errlen);

#endif /* SC_API_NO_PROTOTYPES */

/* ---- level 2: speed monitor (append-only) ---- */

/* forward declaration: ScMoveReq is used by the host callback warp_begin (level 3) */
typedef struct ScMoveReq ScMoveReq;

/* size of ScHost as shipped in API level 2 (3 fields). SC_set_host accepts this size as well as sizeof(ScHost). */
#define SC_HOST_SIZE_L2 24

/* host callbacks. size must be SC_HOST_SIZE_L2 (24, API 2 layout) or sizeof(ScHost) (80, API 3).
 * Level 2 fields: log (may be null), on_speed_abort (once per abort, after the ship is zeroed).
 * Level 3 fields (all may be null; a null move callback fails closed in ShipControl):
 *   tlog: mining tlog (prefix 'test cmd: '); every move-job text line goes here.
 *   on_move_done(sid, seq, result): job ended by ShipControl (job already erased, ship zeroed). Runs after the registry lock is released.
 *   warp_begin(sid, req): called once inside SC_move_start for kind 1 only. 1 = warp sent (job waits for the landing).
 *   remain_m(sid, ship_m[3], out_m): 1 = ok, out_m = surface distance (m) of the rock with tag > 0 (warp settle). Only mode 0.
 *   target_ok(sid, handle): 1 = rock actor still valid.
 *   ore_loc(sid, handle, out3_uu): 1 = ok, out3_uu = actor location (uu).
 *   braking(sid): 1 = bubble brake holds Throttle 0.
 * Contract: callbacks run on the game thread, may call SC_*, must never throw. */
typedef struct ScHost
{
    int32_t size;                                              /* off 0 */
    void (*log)(const wchar_t* text);                          /* off 8 */
    void (*on_speed_abort)(int sid, double ratio);             /* off 16 */
    void (*tlog)(const wchar_t* text);                         /* off 24 */
    void (*on_move_done)(int sid, int seq, const wchar_t* result); /* off 32 */
    int (*warp_begin)(int sid, const ScMoveReq* req);          /* off 40 */
    int (*remain_m)(int sid, const double ship_m[3], double* out_m); /* off 48 */
    int (*target_ok)(int sid, void* handle);                   /* off 56 */
    int (*ore_loc)(int sid, void* handle, double* out3_uu);    /* off 64 */
    int (*braking)(int sid);                                   /* off 72 */
} ScHost;

/* tuning from mining config. size must equal sizeof(ScTune). speed_k = steer_probe_speed_k (limit = MaxSpeed * speed_k). */
typedef struct ScTune
{
    int32_t size;
    double speed_k;
} ScTune;

/* speed reading of a watched ship. size must equal sizeof(ScSpeed). have = 1 when v is valid. aborted = over-limit already reported. */
typedef struct ScSpeed
{
    int32_t size;
    int32_t have;
    double v_raw;      /* raw per s */
    double lim_raw;    /* raw per s */
    int32_t aborted;
} ScSpeed;

/* ---- level 3: move jobs (append-only). ScMoveReq: size = sizeof(ScMoveReq). kind 0 face, 1 approach. seq -1 = internal. ---- */
/* mode 0: fixed world point pt_uu (rock actor location) + optional handle liveness; tag > 0 = registry rock for the warp settle.
 * mode 1: chunk point (sys, ch, loc raw as MsFlight.loc_raw).
 * mode 2: floating ore: handle = ore actor, pt_uu = start scan point. radius_m = 0 for modes 1 and 2. stop_m = surface stop distance (m), kind 1.
 * out_cid / out_steps are set by SC_move_start. */
struct ScMoveReq
{
    int32_t size;        /* off 0 */
    int32_t kind;        /* off 4 */
    int32_t seq;         /* off 8 */
    int32_t mode;        /* off 12 */
    int32_t sys;         /* off 16 */
    int32_t tag;         /* off 20 */
    int64_t ch[3];       /* off 24 */
    double loc[3];       /* off 48 */
    double pt_uu[3];     /* off 72 */
    void* handle;        /* off 96 */
    double radius_m;     /* off 104 */
    double stop_m;       /* off 112 */
    int32_t out_cid;     /* off 120 OUT */
    int32_t out_steps;   /* off 124 OUT */
};

/* ScMoveStatus: size = sizeof(ScMoveStatus). phase 1 aim, 2 approach. err_deg -1 = none yet. kind as ScMoveReq.kind. */
struct ScMoveStatus
{
    int32_t size;        /* off 0 */
    int32_t active;      /* off 4 */
    int32_t phase;       /* off 8 */
    int32_t warp_wait;   /* off 12 */
    int32_t thr;         /* off 16 */
    int32_t kind;        /* off 20 */
    double err_deg;      /* off 24 */
    double t_s;          /* off 32 */
    double ore_loc[3];   /* off 40 */
};

#ifdef __cplusplus
static_assert(sizeof(void*) == 8, "ShipControl ABI expects 64-bit pointers");
static_assert(sizeof(ScHost) == 80, "ScHost API 3 size");
static_assert(offsetof(ScHost, size) == 0 && offsetof(ScHost, log) == 8 && offsetof(ScHost, on_speed_abort) == 16, "ScHost level 2 offsets");
static_assert(offsetof(ScHost, tlog) == 24 && offsetof(ScHost, on_move_done) == 32 && offsetof(ScHost, warp_begin) == 40, "ScHost level 3 offsets");
static_assert(offsetof(ScHost, remain_m) == 48 && offsetof(ScHost, target_ok) == 56 && offsetof(ScHost, ore_loc) == 64 && offsetof(ScHost, braking) == 72, "ScHost callback offsets");
static_assert(sizeof(ScMoveReq) == 128, "ScMoveReq API 3 size");
static_assert(offsetof(ScMoveReq, size) == 0 && offsetof(ScMoveReq, kind) == 4 && offsetof(ScMoveReq, seq) == 8 && offsetof(ScMoveReq, mode) == 12, "ScMoveReq header offsets");
static_assert(offsetof(ScMoveReq, sys) == 16 && offsetof(ScMoveReq, tag) == 20 && offsetof(ScMoveReq, ch) == 24 && offsetof(ScMoveReq, loc) == 48, "ScMoveReq chunk offsets");
static_assert(offsetof(ScMoveReq, pt_uu) == 72 && offsetof(ScMoveReq, handle) == 96 && offsetof(ScMoveReq, radius_m) == 104 && offsetof(ScMoveReq, stop_m) == 112, "ScMoveReq target offsets");
static_assert(offsetof(ScMoveReq, out_cid) == 120 && offsetof(ScMoveReq, out_steps) == 124, "ScMoveReq out offsets");
static_assert(sizeof(ScMoveStatus) == 64, "ScMoveStatus API 3 size");
static_assert(offsetof(ScMoveStatus, size) == 0 && offsetof(ScMoveStatus, active) == 4 && offsetof(ScMoveStatus, phase) == 8, "ScMoveStatus header offsets");
static_assert(offsetof(ScMoveStatus, warp_wait) == 12 && offsetof(ScMoveStatus, thr) == 16 && offsetof(ScMoveStatus, kind) == 20, "ScMoveStatus state offsets");
static_assert(offsetof(ScMoveStatus, err_deg) == 24 && offsetof(ScMoveStatus, t_s) == 32 && offsetof(ScMoveStatus, ore_loc) == 40, "ScMoveStatus value offsets");
#endif

#ifndef SC_API_NO_PROTOTYPES
/* install host callbacks. size = SC_HOST_SIZE_L2 or sizeof(ScHost). 1 = ok, 0 = refused (size mismatch or killed). */
int SC_set_host(const ScHost* host);
/* install tuning. 1 = ok, 0 = refused (size mismatch or killed). */
int SC_set_tune(const ScTune* tune);
/* watch (on=1) or stop watching (on=0) ship for owner. on=1 refused when another owner holds the ship or MSCore is too old (needs 0.8.13). 1 = ok, 0 = refused. */
int SC_watch(const char* owner, int sid, int on);
/* one poll step for every watched ship (dt = s since last step). >= 0 = fresh aborts this step, -1 = MSCore not usable or killed. */
int SC_tick_speed(double dt);
/* speed reading of a watched ship into out. 1 = ok, 0 = not watched / failed. */
int SC_speed(int sid, ScSpeed* out);
/* zero Throttle and rotation of ship for owner. refused when another owner holds the ship. 1 = zeroed, 0 = refused or failed. */
int SC_zero(const char* owner, int sid);
#endif /* SC_API_NO_PROTOTYPES */

/* ---- level 3 exports (move jobs) ---- */
#ifndef SC_API_NO_PROTOTYPES
/* start a move job for ship sid (owner must may_act). 1 = started (out_cid / out_steps set), 0 = refused: err holds the text ('error: ...'). */
int SC_move_start(const char* owner, int sid, ScMoveReq* req, wchar_t* err, int errlen);
/* cancel the move job of ship sid for owner: stops the aim offset, erases the job, no zero, no callback. 1 = cancelled or none, 0 = other owner or killed. */
int SC_move_cancel(const char* owner, int sid);
/* 1 = move job of ship sid runs, 0 = none. */
int SC_move_active(int sid);
/* status of the move job of ship sid into out (size must be sizeof(ScMoveStatus)). 1 = ok (active 0 when none), 0 = refused. */
int SC_move_status(int sid, ScMoveStatus* out);
/* one tick of every move job (dt = s). >= 0 = jobs stepped, -1 = killed or MSCore unusable (every job is ended first, on_move_done per job). */
int SC_tick_frame(double dt);
#endif /* SC_API_NO_PROTOTYPES */

#ifdef __cplusplus
}
#endif

#endif /* SC_API_H */
