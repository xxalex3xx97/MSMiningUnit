/* FILE: mods/ShipControl/sc_api.h
 * WHAT: C ABI of ShipControl.dll. Other mods (MiningBenchMod) load the DLL and resolve these names with GetProcAddress.
 * WHY: plain C types only, so any compiler and any mod can call it. Append only: never change or reorder an existing export.
 * SC0 exports: api, caps, kill, claim, owner, release, release_all. Every export returns int; err/errlen = wide text reason (errlen wide chars, may be 0).
 * GAME FACTS: none. */
#ifndef SC_API_H
#define SC_API_H

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_API_LEVEL 2

/* wchar_t must be 16-bit (Windows) to match the DLL */
#ifdef __cplusplus
static_assert(sizeof(wchar_t) == 2, "ShipControl ABI expects 16-bit wchar_t");
#endif

#ifndef SC_API_NO_PROTOTYPES   /* SC_API_NO_PROTOTYPES: the DLL defines it before including this header (it needs the types only). MSVC rejects dllexport definitions that also have these prototypes (C2375). */

/* API level: 1 = owner registry (SC0). 2 = speed monitor (SC2a): needs MSCore API >= 10 (0.8.13) at runtime. */
int SC_api(void);
/* capability bits: bit 0 = owner registry, bit 1 = speed monitor (MSCore usable). */
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

/* host callbacks. size must equal sizeof(ScHost). log may be null. on_speed_abort runs once per abort, after the ship is zeroed. */
typedef struct ScHost
{
    int32_t size;
    void (*log)(const wchar_t* text);
    void (*on_speed_abort)(int sid, double ratio);
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

#ifndef SC_API_NO_PROTOTYPES
/* install host callbacks. 1 = ok, 0 = refused (size mismatch or killed). */
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

#ifdef __cplusplus
}
#endif

#endif /* SC_API_H */
