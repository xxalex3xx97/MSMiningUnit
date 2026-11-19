/* FILE: mods/ShipControl/sc_api.h
 * WHAT: C ABI of ShipControl.dll. Other mods (MiningBenchMod) load the DLL and resolve these names with GetProcAddress.
 * WHY: plain C types only, so any compiler and any mod can call it. Append only: never change or reorder an existing export.
 * SC0 exports: api, caps, kill, claim, owner, release, release_all. Every export returns int; err/errlen = wide text reason (errlen wide chars, may be 0).
 * GAME FACTS: none. */
#ifndef SC_API_H
#define SC_API_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_API_LEVEL 1

/* wchar_t must be 16-bit (Windows) to match the DLL */
#ifdef __cplusplus
static_assert(sizeof(wchar_t) == 2, "ShipControl ABI expects 16-bit wchar_t");
#endif

/* API level: 1 = owner registry (SC0). */
int SC_api(void);
/* capability bits: bit 0 = owner registry. */
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

#ifdef __cplusplus
}
#endif

#endif /* SC_API_H */
