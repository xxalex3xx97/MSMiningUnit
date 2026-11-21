# ShipControl

Version **0.3.1** (`sc_version.hpp`), API level **4** (`sc_api.h`, `SC_API_LEVEL`). A UE4SS mod that owns ship control for other mods (MiningBenchMod is the first user). It exports a plain C ABI; other mods load `main.dll` and resolve the names with `GetProcAddress`. Needs MSCore 0.8.13 at runtime (flight reads and writes go through MSCore).

The header `sc_api.h` is the contract. Append only: an export or a field is never changed or reordered.

## Changes

- 0.3.1: no code or API change. The version is bumped because the ShipControl source moves to its own repository (history kept). The header comments in `sc_logic_path.hpp` no longer name a single mod, since MiningBenchMod and Fleet both include it. A mod that requires ShipControl@0.3.0 is still satisfied.

## API levels

| Level | Adds | Needs |
|---|---|---|
| 1 | owner registry: `SC_api`, `SC_caps`, `SC_kill`, `SC_claim`, `SC_owner`, `SC_release`, `SC_release_all` | nothing |
| 2 | speed monitor: `SC_set_host`, `SC_set_tune`, `SC_watch`, `SC_tick_speed`, `SC_speed`, `SC_zero`. ScHost 24 bytes | MSCore API >= 10 (0.8.13) |
| 3 | move jobs: `SC_move_start`, `SC_move_cancel`, `SC_move_active`, `SC_move_status`, `SC_tick_frame`. ScHost 80 bytes | MSCore flight |
| 4 | bubble ladder: `SC_tick_bubble`, `SC_bubble_status`. ScHost 112 bytes (4 callbacks: `first_blocker`, `bubble_profile`, `on_bubble`, `near_rock`) | MSCore flight |

Every export returns `int`, takes `wchar_t* err, int errlen` where it can refuse, and catches its own exceptions (a failure is a return code, never a throw).

## Capability bits (`SC_caps`)

| Bit | Value | Meaning |
|---|---|---|
| 0 | 1 | owner registry |
| 1 | 2 | speed monitor (MSCore usable) |
| 2 | 4 | move jobs (API 3) |
| 3 | 8 | bubble ladder (API 4) |

Callers check the bit they need. A caller that sees a bit clear must not use that feature.

## ScHost layout (`SC_set_host`)

The host is the mod's table of callbacks, filled by the caller. Offsets on x64:

- 0 `size` (int32)
- 8 `log`, 16 `on_speed_abort`, 24 `tlog`, 32 `on_move_done`, 40 `warp_begin`, 48 `remain_m`, 56 `target_ok`, 64 `ore_loc`, 72 `braking` (level 3 ends at 80)
- 80 `first_blocker`, 88 `bubble_profile`, 96 `on_bubble`, 104 `near_rock` (level 4, size 112)

`SC_set_host` accepts size 24, 80 or `sizeof(ScHost)` (112) and refuses others. It zeroes its copy and copies `min(size, sizeof)`, so a level 2 or 3 caller gets the older set. Callbacks run on the game thread inside the registry lock; they may call `SC_*`, never throw. `ScTune` has an exact size check (`size == sizeof(ScTune)`).

Structs of level 4: `ScBlockHit` (56), `ScBubbleProfile` (72), `ScBubbleEvent` (88), `ScBubbleStatus` (56). Their offsets are checked by `static_assert` in `sc_api.h`.

## Owner model

- One owner per ship. `SC_claim(owner, ship)`: 1 = owned now (free, or already this owner), 0 = refused (err names the owner).
- `SC_owner(ship)` reads the owner. `SC_release(owner, ship)` frees a ship only for its owner. `SC_release_all(owner)` frees every ship of that owner and returns the count.
- Speed watch, zero, move jobs and the bubble ladder are per ship and need the claim first (the owner name is checked against the ship's owner).
- `SC_kill(1)` refuses every control export until `SC_kill(0)`. A killed ShipControl makes every tick return -1, and its callers end their jobs.

## How mining uses it

MiningBenchMod 0.3.0 (requires ShipControl 0.3.0, MSCore 0.8.13):

- Start and Collect (sweep) claim the ship for owner `mining` (`mb_shipctl.hpp`, `sc_claim_ship`). No claim, no loop: refused with `error: ShipControl not installed or too old (needs API 4); update MSCore Manager` when ShipControl or the caps bits are missing, or `error: ship <err>` when another owner holds the ship.
- Every move job (approach a rock, a point, floating ore) is started with `SC_move_start`. ShipControl builds the job, steps the throttle and aim, and calls back `on_move_done`.
- The speed guard runs in `SC_tick_speed`; `SC_zero` stops a ship for mining.
- The bubble ladder (a rock on the nose: brake, replan, waypoint, nudge, resume) runs in `SC_tick_bubble`. Mining answers the ladder through `first_blocker` (registry query, ship spheres), `bubble_profile` (job state), `on_bubble` (replan, skip, fly) and `near_rock`.
- Stop or a finished loop releases the claim (`SC_release`). Mod unload calls `SC_release_all`.

Mining keeps the game data (rock registry, jobs, docks, cargo). ShipControl keeps the ship control.
