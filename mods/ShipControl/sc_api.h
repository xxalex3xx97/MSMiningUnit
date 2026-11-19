/* FILE: mods/ShipControl/sc_api.h
 * WHAT: C ABI of ShipControl.dll. Other mods (MiningBenchMod) load the DLL and resolve these names with GetProcAddress.
 * WHY: plain C types only, so any compiler and any mod can call it. Append only: never change or reorder an existing export.
 * SC0 exports: api, caps, kill, claim, owner, release, release_all. Every export returns int; err/errlen = wide text reason (errlen wide chars, may be 0).
 * SC2b (API level 3) appends: move jobs (SC_move_start, SC_move_cancel, SC_move_active, SC_move_status, SC_tick_frame) and ScHost 80 bytes.
 * SC3 (API level 4) appends: bubble ladder (SC_tick_bubble, SC_bubble_status), ScHost 112 bytes (4 callbacks), structs ScBlockHit, ScBubbleProfile, ScBubbleEvent, ScBubbleStatus.
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

#define SC_API_LEVEL 4

/* wchar_t must be 16-bit (Windows) to match the DLL */
#ifdef __cplusplus
static_assert(sizeof(wchar_t) == 2, "ShipControl ABI expects 16-bit wchar_t");
#endif

#ifndef SC_API_NO_PROTOTYPES   /* SC_API_NO_PROTOTYPES: the DLL defines it before including this header (it needs the types only). MSVC rejects dllexport definitions that also have these prototypes (C2375). */

/* API level: 1 = owner registry (SC0). 2 = speed monitor (SC2a): needs MSCore API >= 10 (0.8.13) at runtime. 3 = move jobs (SC2b). 4 = bubble ladder (SC3). */
int SC_api(void);
/* capability bits: bit 0 = owner registry, bit 1 = speed monitor (MSCore usable), bit 2 (value 4) = move jobs (API 3), bit 3 (value 8) = bubble ladder (API 4). */
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
/* size of ScHost as shipped in API level 3 (move callbacks). SC_set_host accepts it too. */
#define SC_HOST_SIZE_L3 80

/* forward declarations: the level 4 structs are used by the host callbacks below */
typedef struct ScBlockHit ScBlockHit;
typedef struct ScBubbleProfile ScBubbleProfile;
typedef struct ScBubbleEvent ScBubbleEvent;

/* host callbacks. size must be SC_HOST_SIZE_L2 (24, API 2 layout), SC_HOST_SIZE_L3 (80, API 3) or sizeof(ScHost) (112, API 4).
 * Level 2 fields: log (may be null), on_speed_abort (once per abort, after the ship is zeroed).
 * Level 3 fields (all may be null; a null move callback fails closed in ShipControl):
 *   tlog: mining tlog (prefix 'test cmd: '); every move-job text line goes here.
 *   on_move_done(sid, seq, result): job ended by ShipControl (job already erased, ship zeroed). Runs after the registry lock is released.
 *   warp_begin(sid, req): called once inside SC_move_start for kind 1 only. 1 = warp sent (job waits for the landing).
 *   remain_m(sid, ship_m[3], out_m): 1 = ok, out_m = surface distance (m) of the rock with tag > 0 (warp settle). Only mode 0.
 *   target_ok(sid, handle): 1 = rock actor still valid.
 *   ore_loc(sid, handle, out3_uu): 1 = ok, out3_uu = actor location (uu).
 *   braking(sid): 1 = bubble brake holds Throttle 0.
 * Level 4 fields (API 4; may be null: SC then runs no bubble for the host, the legacy path stays in the host):
 *   first_blocker(sid, sys, phase, rock, p, b, clear_m, out): blocker id on segment p..b (0 = clear, < 0 = ship sphere). Fills out (ScBlockHit).
 *   bubble_profile(sid, out): fills out (ScBubbleProfile). 1 = ok, 0 = refused (SC erases the ship's state).
 *   on_bubble(sid, ev): event of the ladder (ScBubbleEvent). 1 = done (FLY: flight started).
 *   near_rock(sid, p, ship_r_m, out_c3, out_r): 1 = ship too near a rock (centre and radius out).
 * Contract: callbacks run on the game thread, may call SC_*, must never throw. SC copies ids first and re-finds its state after every callback. */
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
    int (*first_blocker)(int sid, int sys, int phase, int rock, const double p[3], const double b[3], double clear_m, ScBlockHit* out); /* off 80 */
    int (*bubble_profile)(int sid, ScBubbleProfile* out);      /* off 88 */
    int (*on_bubble)(int sid, const ScBubbleEvent* ev);        /* off 96 */
    int (*near_rock)(int sid, const double p[3], double ship_r_m, double* out_c3, double* out_r); /* off 104 */
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

/* ---- level 4: bubble ladder (append-only). Every struct: size = sizeof(struct). ---- */
/* blocker of one bubble query (ScHost.first_blocker out). geom_ok = 1 when the blocker geometry was read (set by the host). */
struct ScBlockHit
{
    int32_t size;        /* off 0 */
    int32_t geom_ok;     /* off 4 */
    double d;            /* off 8: blocker centre to the nose line (m) */
    double block_r;      /* off 16: blocker radius (m), the log rad= */
    double rock_r;       /* off 24: rock radius (m) for the waypoint (0 = no id) */
    double c[3];         /* off 32: blocker centre (m) */
};

/* host profile of one ship for the bubble pass (ScHost.bubble_profile out). active = 0: SC erases the ship's state. thrust = approach_rock job. */
struct ScBubbleProfile
{
    int32_t size;        /* off 0 */
    int32_t active;      /* off 4 */
    int32_t thrust;      /* off 8 */
    int32_t pt_on;       /* off 12 */
    int32_t ore_on;      /* off 16 */
    int32_t job_active;  /* off 20 */
    int32_t warp_wait;   /* off 24 */
    int32_t rock;        /* off 28: target rock id (0 none) */
    int32_t dst_ok;      /* off 32 */
    int32_t pad;         /* off 36 */
    double dst_m[3];     /* off 40: flight destination (m) */
    double clearance_m;  /* off 64: bubble clearance (m) */
};

/* ladder event sent to ScHost.on_bubble. kind 1 REPLAN, 2 SKIP, 3 FLY. reason (FLY): 1 waypoint, 2 nudge, 3 resume. loc / ch = FLY target (PtTarget meaning). */
struct ScBubbleEvent
{
    int32_t size;        /* off 0 */
    int32_t kind;        /* off 4 */
    int32_t phase;       /* off 8 */
    int32_t act;         /* off 12 */
    int32_t blk;         /* off 16 */
    int32_t reason;      /* off 20 */
    int32_t sys;         /* off 24 */
    int32_t pad;         /* off 28 */
    double blk_d;        /* off 32 */
    int64_t ch[3];       /* off 40 */
    double loc[3];       /* off 64 */
};

/* ScBubbleStatus: state of one ship's ladder (SC_bubble_status). have = 0 when no state. */
struct ScBubbleStatus
{
    int32_t size;        /* off 0 */
    int32_t have;        /* off 4 */
    int32_t braking;     /* off 8 */
    int32_t replans;     /* off 12 */
    int32_t pending;     /* off 16 */
    int32_t wp;          /* off 20 */
    int32_t resume;      /* off 24 */
    int32_t blk;         /* off 28 */
    int32_t phase;       /* off 32 */
    int32_t why;         /* off 36 */
    int32_t rock;        /* off 40 */
    int32_t pad;         /* off 44 */
    double blk_d;        /* off 48 */
};

#ifdef __cplusplus
static_assert(sizeof(void*) == 8, "ShipControl ABI expects 64-bit pointers");
static_assert(sizeof(ScHost) == 112, "ScHost API 4 size");
static_assert(SC_HOST_SIZE_L3 == 80 && offsetof(ScHost, braking) + sizeof(void*) == SC_HOST_SIZE_L3, "ScHost API 3 size is the prefix before the level 4 fields");
static_assert(offsetof(ScHost, first_blocker) == 80 && offsetof(ScHost, bubble_profile) == 88 && offsetof(ScHost, on_bubble) == 96 && offsetof(ScHost, near_rock) == 104, "ScHost level 4 offsets");
static_assert(sizeof(ScBlockHit) == 56, "ScBlockHit API 4 size");
static_assert(offsetof(ScBlockHit, size) == 0 && offsetof(ScBlockHit, geom_ok) == 4 && offsetof(ScBlockHit, d) == 8, "ScBlockHit header offsets");
static_assert(offsetof(ScBlockHit, block_r) == 16 && offsetof(ScBlockHit, rock_r) == 24 && offsetof(ScBlockHit, c) == 32, "ScBlockHit value offsets");
static_assert(sizeof(ScBubbleProfile) == 72, "ScBubbleProfile API 4 size");
static_assert(offsetof(ScBubbleProfile, size) == 0 && offsetof(ScBubbleProfile, active) == 4 && offsetof(ScBubbleProfile, thrust) == 8, "ScBubbleProfile header offsets");
static_assert(offsetof(ScBubbleProfile, pt_on) == 12 && offsetof(ScBubbleProfile, ore_on) == 16 && offsetof(ScBubbleProfile, job_active) == 20, "ScBubbleProfile state offsets");
static_assert(offsetof(ScBubbleProfile, warp_wait) == 24 && offsetof(ScBubbleProfile, rock) == 28 && offsetof(ScBubbleProfile, dst_ok) == 32, "ScBubbleProfile target offsets");
static_assert(offsetof(ScBubbleProfile, pad) == 36 && offsetof(ScBubbleProfile, dst_m) == 40 && offsetof(ScBubbleProfile, clearance_m) == 64, "ScBubbleProfile value offsets");
static_assert(sizeof(ScBubbleEvent) == 88, "ScBubbleEvent API 4 size");
static_assert(offsetof(ScBubbleEvent, size) == 0 && offsetof(ScBubbleEvent, kind) == 4 && offsetof(ScBubbleEvent, phase) == 8 && offsetof(ScBubbleEvent, act) == 12, "ScBubbleEvent header offsets");
static_assert(offsetof(ScBubbleEvent, blk) == 16 && offsetof(ScBubbleEvent, reason) == 20 && offsetof(ScBubbleEvent, sys) == 24 && offsetof(ScBubbleEvent, pad) == 28, "ScBubbleEvent state offsets");
static_assert(offsetof(ScBubbleEvent, blk_d) == 32 && offsetof(ScBubbleEvent, ch) == 40 && offsetof(ScBubbleEvent, loc) == 64, "ScBubbleEvent value offsets");
static_assert(sizeof(ScBubbleStatus) == 56, "ScBubbleStatus API 4 size");
static_assert(offsetof(ScBubbleStatus, size) == 0 && offsetof(ScBubbleStatus, have) == 4 && offsetof(ScBubbleStatus, braking) == 8, "ScBubbleStatus header offsets");
static_assert(offsetof(ScBubbleStatus, replans) == 12 && offsetof(ScBubbleStatus, pending) == 16 && offsetof(ScBubbleStatus, wp) == 20, "ScBubbleStatus ladder offsets");
static_assert(offsetof(ScBubbleStatus, resume) == 24 && offsetof(ScBubbleStatus, blk) == 28 && offsetof(ScBubbleStatus, phase) == 32, "ScBubbleStatus flight offsets");
static_assert(offsetof(ScBubbleStatus, why) == 36 && offsetof(ScBubbleStatus, rock) == 40 && offsetof(ScBubbleStatus, pad) == 44 && offsetof(ScBubbleStatus, blk_d) == 48, "ScBubbleStatus tail offsets");
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

/* ---- level 4 exports (bubble ladder) ---- */
#ifndef SC_API_NO_PROTOTYPES
/* one bubble pass for every watched ship and every ship with a ladder state (dt = s). >= 0 = ships passed, -1 = killed or MSCore unusable (no game writes). */
int SC_tick_bubble(double dt);
/* ladder state of ship sid into out (size must be sizeof(ScBubbleStatus)). 1 = ok (have 0 when no state), 0 = refused (killed or bad size). */
int SC_bubble_status(int sid, ScBubbleStatus* out);
#endif /* SC_API_NO_PROTOTYPES */

#ifdef __cplusplus
}
#endif

#endif /* SC_API_H */
