# Dual-IMU Support (LIS3DH + Polar) — Design

## Goal

Let the DA14531 firmware use either the on-board LIS3DH (boat-mounted) or a
Polar Verity Sense strap (arm-mounted) as the stroke-rate IMU source, and
switch between them at runtime. Polar is preferred when available; LIS3DH is
the always-available fallback.

Today the IMU is selected at **compile time** by `CFG_IMU_LIS3DH` /
`CFG_IMU_MPU6050` / `CFG_IMU_POLAR` (exactly one must be defined) via the
`#ifdef` dispatcher in `firmware/app/include/paddling_pulse_imu.h`. This
design adds a runtime-switched mode without regressing single-IMU builds.

## In Scope

- A new `CFG_IMU_DUAL` compile config that enables both the LIS3DH and Polar
  drivers in one firmware image.
- A manager module that selects the active IMU at runtime from:
  - boot-time auto-preference for Polar with timed fallback to LIS3DH,
  - mid-session Polar drop with timed retry then fallback to LIS3DH,
  - explicit user override via `AT+IMU=AUTO|LIS3DH|POLAR`, persisted in
    retained RAM.
- RSSI-best-wins selection among visible Polar Verity Sense straps during a
  scan window (prevents connecting to a teammate's strap at the dock).
- Per-IMU tuning via a `pp_stroke_rate_params_t` struct owned by each driver,
  consumed by `pp_stroke_rate_*`.
- Correct handoff of the shared `pp_sample_store` on source change: stop old
  source, clear store, re-init stroke-rate, start new source — in that order.
- Console additions: `AT+IMU=<target>` (SET) and enhanced `AT+IMU` (GET).
- Fix of a latent bug where `pp_sample_store_init(52)` is called for Polar but
  the strap may actually negotiate a different PMD rate.
- Host-side unit tests for the manager state machine, RSSI-best-wins picker,
  and console parser.
- Build-verification gates on retained-RAM budget, section placement, and
  linker-warning cleanliness.

## Out of Scope

- Supporting MPU6050 in the dual mode. `CFG_IMU_MPU6050` stays a standalone
  single-IMU config.
- Persistent Polar strap bonding by BD address (rejected during brainstorm in
  favor of strongest-RSSI-wins).
- Flash-backed persistence of the override (board has no flash enabled).
- Graph executor, block registry, OTA pipeline — out of scope on this branch
  as per the earlier Polar-backport spec.
- A second sample store or a pooled allocator.

## Selection Mechanism — Hybrid Auto + Override

Decided during brainstorm (user choices D, B, C, B, B on questions 1–5).

- **Override** `AUTO | LIS3DH | POLAR` held in retained RAM.
- **AUTO:** boot scans for Polar for `PP_POLAR_BOOT_SCAN_TIMEOUT_MS`
  (default 10 s). If found → Polar active. If not → LIS3DH active. If Polar
  disconnects mid-session, manager rescans for
  `PP_POLAR_RECONNECT_TIMEOUT_MS` (default 5 s) before falling back to
  LIS3DH. Once on LIS3DH, no spontaneous retry of Polar — user must restart.
- **POLAR:** scans until found. Never falls back. CSCP emits 0 RPM while
  seeking.
- **LIS3DH:** starts LIS3DH immediately at boot, never scans Polar.
- **Strap selection:** during a scan, collect up to
  `PP_POLAR_MAX_CANDIDATES` (default 4) Verity Sense advertisements with
  their RSSI. After `PP_POLAR_RSSI_WINDOW_MS` (default 2 s), connect to
  the highest-RSSI candidate. No persistent binding.

## Approach

Chosen: **runtime enum + switch dispatch** over a new manager module. Per
brainstorm question comparing three approaches, chosen over a vtable
approach (similar compiled output, less elegant but easier to step through
in a debugger) and over a parallel-drivers approach (doubled sample-store
RAM, infeasible under the retained-RAM pressure introduced in commit
`912fb81`).

Single-IMU builds are untouched: the existing static-inline dispatchers in
`paddling_pulse_imu.h` remain the fast path when `CFG_IMU_DUAL` is not
defined. Only the dual-IMU build introduces the manager indirection.

## Architecture

### Modules

**New: `paddling_pulse_imu_manager` (.h + .c).** Owns:

- `enum pp_imu_source { PP_IMU_NONE, PP_IMU_LIS3DH, PP_IMU_POLAR }`.
- `enum pp_imu_override { PP_IMU_OVERRIDE_AUTO, PP_IMU_OVERRIDE_LIS3DH, PP_IMU_OVERRIDE_POLAR }`.
- The selection state machine (below).
- Public entry points called from `paddling_pulse_app.c`:
  `pp_imu_manager_init`, `pp_imu_manager_start`, `pp_imu_manager_stop`,
  `pp_imu_manager_process` (ticked from the existing `imu_process_timer_cb`
  when source = LIS3DH; no-op when source = Polar).
- Public entry points called from the Polar driver:
  `pp_imu_manager_on_polar_event(enum ev)` with
  `ev ∈ {ON_STREAMING, ON_DISCONNECT, ON_SCAN_FAIL}`.
- Retained-RAM state: current source, override, current store rate, retry
  timer handle (~8 bytes total).

**Changed: `paddling_pulse_imu.h`.** Under `CFG_IMU_DUAL`, the existing
static-inline dispatchers delegate to the manager. Existing single-IMU
builds are unchanged. The existing mutual-exclusion guard is extended:
`CFG_IMU_DUAL` counts as its own option and may not coexist with the
single-IMU configs.

**Changed: `paddling_pulse_imu_polar.{c,h}`.**
- Replace first-match-wins in `pp_imu_polar_on_adv_report` with
  candidate-buffering + RSSI pick on a `PP_POLAR_RSSI_WINDOW_MS` timer.
- Expose `pp_imu_polar_get_actual_sample_rate_hz()` accessor so the
  manager can re-init the sample store with the rate the strap actually
  negotiated (fixes the 52-Hz-hardcode latent bug). **Contract:** the
  returned value is meaningful only after the manager has received
  `ON_STREAMING`; the Polar driver latches the rate during PMD settings
  response parsing, before emitting `ON_STREAMING`.
- Emit `ON_STREAMING` / `ON_DISCONNECT` / `ON_SCAN_FAIL` events to the
  manager when `CFG_IMU_DUAL` is defined. Under `CFG_IMU_POLAR` alone, the
  callbacks remain inert (existing behavior preserved).

**Changed: `paddling_pulse_imu_lis3dh.c`.** Axis selection becomes a
runtime read of `pp_imu_lis3dh_params.axis` instead of the compile-time
`#if defined(CFG_IMU_AXIS_*)` blocks at lines 181–187. Under single-IMU
builds, the params block is populated from the existing `CFG_IMU_AXIS_*`
macros — no behavioral change.

**Changed: `paddling_pulse_stroke_rate.{c,h}`.**
- Introduce `pp_stroke_rate_params_t` (see below).
- Change `pp_stroke_rate_init` signature to take
  `const pp_stroke_rate_params_t *params`. Store the pointer in retained
  RAM; every `PP_*` constant read inside `pp_stroke_rate_update` is
  replaced with `s_params->*`.

**Changed: `paddling_pulse_app.c`.** `pipeline_start` / `pipeline_stop`
call into the manager under `CFG_IMU_DUAL`, the existing `pp_imu_*` API
otherwise.

**Changed: `paddling_pulse_console_commands.c` and
`paddling_pulse_console.c`.** Add `PP_CONSOLE_CMD_IMU_SET`; parser accepts
`AT+IMU=AUTO|LIS3DH|POLAR` (case-insensitive) via the existing length-
bounded token-compare pattern. Enhance `AT+IMU` GET output to include
override, source, state, and rate.

**Changed: `da14531_config_basic.h`.** Add `CFG_IMU_DUAL` option and
the three new timeout defaults (overridable via `#ifndef`, matching the
existing `PP_STROKE_RATE_*` pattern).

### Selection state machine

States: `IDLE`, `POLAR_SEEKING`, `POLAR_ACTIVE`, `LIS3DH_ACTIVE`.

| From | Event | Guard | To | Side effects |
|---|---|---|---|---|
| IDLE | `pipeline_start` | override ∈ {AUTO, POLAR} | POLAR_SEEKING | start Polar scan; arm `BOOT_SCAN_TIMEOUT` only if override=AUTO |
| IDLE | `pipeline_start` | override = LIS3DH | LIS3DH_ACTIVE | init store@100 Hz with LIS3DH params, `pp_stroke_rate_init`, start LIS3DH |
| POLAR_SEEKING | `ON_STREAMING` | — | POLAR_ACTIVE | init store@actual-rate with Polar params, `pp_stroke_rate_init` |
| POLAR_SEEKING | timer | override = AUTO | LIS3DH_ACTIVE | stop Polar scan, clear store, init with LIS3DH params, start LIS3DH |
| POLAR_SEEKING | timer | override = POLAR | POLAR_SEEKING | rearm timer, keep scanning |
| POLAR_ACTIVE | `ON_DISCONNECT` | — | POLAR_SEEKING | stop pushing samples, arm `RECONNECT_TIMEOUT` only if override=AUTO, rescan |
| LIS3DH_ACTIVE / POLAR_ACTIVE | `pipeline_stop` | — | IDLE | stop current IMU, clear store |
| POLAR_ACTIVE | `AT+IMU=LIS3DH` | — | LIS3DH_ACTIVE | internal `pipeline_stop` → `pipeline_start` (atomic switch sequence) |
| LIS3DH_ACTIVE | `AT+IMU=POLAR` | — | POLAR_SEEKING | internal `pipeline_stop` → `pipeline_start` (atomic switch sequence) |
| POLAR_SEEKING | `AT+IMU=LIS3DH` | — | LIS3DH_ACTIVE | cancel Polar scan, start LIS3DH |
| any | `AT+IMU=<same-as-current-source>` | — | unchanged | persist override only; do not restart IMU |

Reconnect to the same strap at the same rate does **not** clear the store
or reset the Kalman filter — warm state is preserved for faster cadence
reacquisition.

**Cold-boot behavior:** retained RAM is uninitialized on battery removal
or VBAT drop. On cold boot, the `__SECTION_ZERO` attribute zeros the
override field, which equals `PP_IMU_OVERRIDE_AUTO` (the zero enumerator).
Warm resets preserve the override. Documented so the "persistent across
reset" behavior has clear bounds.

### Per-IMU parameter struct

```c
typedef struct {
    uint8_t  axis;                           /* PP_AXIS_X/Y/Z */
    uint16_t sample_rate_hz;
    uint16_t window_samples;
    uint16_t min_rpm, max_rpm;
    int32_t  kalman_q, kalman_r, kalman_p_max;           /* Q16.16 */
    int32_t  autocorr_confidence_min;                    /* Q16.16 */
    int32_t  autocorr_energy_min;
    uint8_t  autocorr_harmonic_pct;
    uint16_t kalman_confirm_tolerance_rpm, kalman_max_jump_rpm;
    uint8_t  kalman_invalid_max, kalman_confirm_count;
} pp_stroke_rate_params_t;
```

Size ~40 bytes. Each driver owns a `const` instance placed in `.rodata`
(flash, zero RAM cost):

- `pp_imu_lis3dh_params` — defaults: axis Z, 100 Hz, 512-sample window,
  energy floor tuned for boat-scale amplitude.
- `pp_imu_polar_params` — defaults: axis X (fore-aft arm swing, best guess
  pending field data), Polar-negotiated rate, window sized to cover
  ~4–5 stroke cycles at `PP_STROKE_RATE_MIN_RPM`, energy floor tuned for
  arm-scale amplitude (~10× LIS3DH).

Existing `PP_STROKE_RATE_*` / `PP_KALMAN_*` / `PP_AUTOCORR_*` macros
remain as named defaults. Each params block references them where tuning
is identical, overrides where it isn't — one place to change defaults.

`pp_stroke_rate_update` reads every tunable through the stored
`s_params` pointer instead of the `PP_*` macros. `pp_sample_store_get_rate_hz`
still drives the `60 * rate / lag` formula (so whatever the store was
init'd to is what we divide by — matches the store's rate field always).

### Sample-store handoff protocol

**Invariant:** at any moment, exactly one IMU pushes into `pp_sample_store`,
and the store's `sample_rate_hz` matches that IMU's actual rate.

**Switch sequence** (`imu_manager_switch_source(new_source, params, actual_rate_hz)`):

```
1. pp_imu_<old>_stop()                    /* drain pending, block new pushes */
2. pp_sample_store_init(actual_rate_hz)   /* memset + rate (one call)        */
3. pp_stroke_rate_init(params)            /* zero Kalman, store params ptr   */
4. pp_imu_<new>_start()                   /* new source begins pushing       */
```

Ordering matters. Step 1 before step 2 prevents a stale
`GATTC_EVENT_IND` from a pre-stop Polar push landing in a freshly-cleared
store. Both Polar events and LIS3DH polling run in kernel-task context
with no ISR preemption, so no locks are required.

**When the switch runs:** the manager compares `(new_source, actual_rate_hz)`
against the retained `(current_source, current_store_rate_hz)` and
executes the sequence only when either differs. Result:

- Polar reconnect to same strap at same rate → no clear, Kalman warm.
- Polar reconnect at different rate → full clear.
- LIS3DH ↔ Polar → full clear (user's "different IMU" requirement).

CSCP during the switch: `csc_meas_timer_cb` may fire between step 1 and
step 4. `pp_stroke_rate_get_rpm` returns 0 because step 3 just zeroed it;
CSCP emits 0 RPM during the microseconds-long gap. Acceptable.

### RSSI candidate buffer

```c
#define PP_POLAR_MAX_CANDIDATES 4
static struct { struct bd_addr addr; uint8_t addr_type; int8_t rssi; }
    s_candidates[PP_POLAR_MAX_CANDIDATES];  /* non-retained .bss */
static uint8_t s_candidate_count;
```

Lives in `paddling_pulse_imu_polar.c` in non-retained `.bss`
(only meaningful during a scan window — no reason to retain it across
sleep). ~32 B, zero impact on retained-RAM budget. Cleared at
`polar_start_scan`. Overflow drops new candidates rather than best-of-all
(worst case: connect to a not-strongest-but-still-yours strap).

### Console commands

Under the existing `CFG_PADDLING_PULSE_AT_COMMANDS` guard.

**Enhanced `AT+IMU` (GET):**
```
> AT+IMU
+IMU: override=AUTO,source=POLAR,state=STREAMING,rate=52
OK
```

**New `AT+IMU=<target>` (SET):** `AUTO`, `LIS3DH`, `POLAR`
(case-insensitive). Writes retained override. If the change affects the
active source, runs the atomic switch sequence. Unknown arg → `ERROR`.
Parser uses the existing length-bounded `pp_console_token_equals`
pattern — no raw `strcmp` of user input. Target tokens declared as
`static const char *const` constants; no magic strings in the handler.

Not adding `AT+POLAR` (YAGNI) or `AT+POLARBIND` (superseded by
RSSI-best-wins decision).

## Memory Efficiency and Verification

Design choices that actively minimize RAM impact:

- `const pp_stroke_rate_params_t` blocks live in `.rodata` (flash), not
  RAM. Saves ~80 B retained-RAM vs. a copy-per-driver approach.
- Manager holds a **pointer** to the active params, not a copy
  (4 B retained instead of ~40 B).
- RSSI candidate buffer is **non-retained** `.bss` — 32 B stays off the
  tight retained section.
- Single sample store preserved — no dual buffer. Saves 1024 B vs. the
  rejected parallel-drivers approach.
- No dynamic allocation anywhere; no `malloc`, no growth in
  `ke_msg_alloc` counts.

Estimated retained-RAM delta under `CFG_IMU_DUAL`:

| Item | Today | CFG_IMU_DUAL | Delta |
|---|---|---|---|
| `pp_sample_store` | ~1032 B | ~1032 B | 0 |
| `s_polar` + polar timers/flags | ~100 B | ~100 B | 0 |
| LIS3DH `s_running` + `s_addr` | 2 B (LIS3DH build) | 2 B (always) | +2 B |
| Stroke-rate Kalman + counters | ~20 B | ~20 B | 0 |
| New `s_params` pointer | — | 4 B | +4 B |
| Manager state | — | ~8 B | +8 B |

**~14 B new retained RAM.** Numbers are estimates — verified gate
below is authoritative.

**Verification gate — must pass before declaring work complete:**

1. **Retained-RAM section fit.** Diff `.map` for `retention_mem_area0`
   size pre- vs. post-change. Must be strictly below the linker-defined
   ceiling. PR description records before / after / headroom.
2. **Section placement correctness.** Every new static with retained
   semantics must carry `__SECTION_ZERO("retention_mem_area0")`. The
   RSSI candidate buffer must **not** carry it (verify it lands in
   `.bss`). Grep the diff.
3. **Zero linker warnings.** Clean `mingw32-make` build completes with
   no warnings. In particular, no "section overlap" or "region RAM
   overflowed" diagnostics.
4. **Flash-region fit.** `.text` + `.rodata` totals within DA14531's
   code region; numbers captured in PR.
5. **Stack unchanged.** New candidate buffer is `.bss`, not a local
   frame — confirm via `objdump` / map. No function added by this work
   takes a locals frame larger than the existing largest function in
   its driver.
6. **All-configs build.** Four clean builds, one per config:
   `CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`, `CFG_IMU_MPU6050`, `CFG_IMU_DUAL`.
   Scripted as part of the verification step.

Gates 1–5 are linker-produced numbers, not judgement calls — either the
numbers fit or they don't.

## Memory Safety Checklist

- **Bounds checks:**
  - `s_candidate_count < PP_POLAR_MAX_CANDIDATES` before every write to
    the candidate buffer.
  - `AT+IMU=<arg>` parsed with length-bounded token compare, not raw
    `strcmp`.
  - `pp_stroke_rate_init` early-returns on `params == NULL`. Cheap
    defensive contract; never hit in normal flow.
- **Rate-zero guard.** Manager clamps `rate_hz` to
  `[PP_SAMPLE_STORE_MIN_RATE_HZ, PP_SAMPLE_STORE_MAX_RATE_HZ]`
  (defaults 1 Hz and 1000 Hz, declared in
  `paddling_pulse_sample_store.h`) before calling
  `pp_sample_store_init`. Existing soft guard at
  `paddling_pulse_stroke_rate.c:82` catches the rest.
- **Lifecycle pairing.** Every `pp_imu_<x>_start` has a matching `stop`.
  Manager invariant tests assert no source-switch path leaves two
  drivers running simultaneously.
- **Integer overflow.** All new arithmetic (RSSI compares, timer ms
  values) fits in `int32_t` with margin.
- **No dynamic allocation.** All storage is static or retained-RAM-section.
- **Polar hot-path.** Existing `s_polar.conidx` check in
  `polar_handle_gatt_event` plus the step-1-before-step-2 switch
  ordering guarantee no stale `GATTC_EVENT_IND` lands in a fresh store.

## Code Style (rules.md alignment)

The implementation PRs must satisfy these style rules (derived from
`config/local/rules.md`, adapted for the embedded-C context —
C++/RAII items in that document do not apply, this is C).

**Naming and constants:**

- Explicit, descriptive names for every function, variable, enum, struct.
  No abbreviations that obscure intent (prefer
  `PP_IMU_OVERRIDE_AUTO` over `PP_IMU_OVR_AUTO`, `candidate_count` over
  `cand_cnt`).
- Zero magic numbers in any new code. Every timeout, size, threshold, or
  range bound is a named `#define` or `enum` with the
  `PP_<MODULE>_<MEANING>` prefix convention already in use in the
  codebase.
- Named constants declared in the header that owns the concept — e.g.
  sample-rate bounds in `paddling_pulse_sample_store.h`, Polar timeouts
  in `paddling_pulse_imu_manager.h`.

**Function design:**

- One responsibility per function. Target ≤ 50 lines; if the natural
  unit is larger, split into helper statics.
- ≤ 3–4 parameters. Collapse wider parameter lists into a struct
  (the `pp_stroke_rate_params_t` pattern).
- Match every `init` / `start` with a `stop` / `reset`. No half-formed
  lifecycles on the source-switch path.

**Headers:**

- Include guards on every new header (`_PADDLING_PULSE_<NAME>_H_`
  pattern, matching existing files).
- Minimal transitive includes. Forward-declare SDK types where possible;
  push `#include "gapc_task.h"` and similar into the `.c` where they're
  actually used.
- Include order: standard → third-party/SDK → project-local.

**Comments:**

- Explain **why**, not **what** or **how**. The code's names should
  carry the "what"; the comments explain the non-obvious reason a line
  exists (e.g. "ordering note: stop before clear prevents a stale
  GATTC_EVENT_IND from landing in the fresh store").
- No running narration ("now we clear the store, then we call init").
- No references to transient context ("for the Polar backport",
  "added in PR #N") — those belong in commit messages.

**Safety (rules.md §4–5 applied to C):**

- Bounds-check every array index that depends on parsed input or
  message length. Polar PMD parsing already does this at each
  TLV boundary — preserve that discipline in the new candidate buffer
  and in `AT+IMU=X` argument parsing.
- Initialize every stack variable before first use. `__SECTION_ZERO`
  attribute gives zero-init for retained state; `.bss` gives it for
  non-retained static state; locals get explicit initialization.
- No signed/unsigned comparison warnings tolerated. Compile with the
  existing warning flags and fix any warning the new code introduces.
- No integer-overflow hazards in RSSI compares or millisecond
  arithmetic — `int32_t` everywhere with margin.

**Git hygiene (rules.md §1):**

- Branch already follows the feature-branch pattern
  (`feat/stroke-rate-pipeline-app-layer`). New commits stay on this
  branch or on a descendant; no direct commits to `main`.
- No build artifacts, auto-generated files, or IDE config in any
  commit. `.gitignore` already covers these.

**Modifications (rules.md final note):**

- When editing existing blocks, modify in place. Do not delete-and-
  reinsert a modified copy. Keeps diffs minimal and review-friendly.

**No AI-author attribution.** Per `MEMORY.md` feedback, no Claude
co-author line in any commit; no "generated by" references in comments
or files.

Each of the above is trivially checkable by diff grep during review
(e.g. `\b[0-9]+\b` in new `.c` files for magic numbers, or a search for
`Claude` in the commit range).

## Testing Strategy

### Host-side unit tests (pure C, no HW)

**New `tests/test_imu_manager.c`** — drives the manager by calling
`pp_imu_manager_on_polar_event` etc. directly. `pp_imu_<x>_start/stop`
stubbed behind a test-only function-pointer shim so assertions can
inspect call order and count.

Cases:

- Boot AUTO + Polar found → POLAR_ACTIVE, store inited at Polar rate.
- Boot AUTO + Polar timeout → LIS3DH_ACTIVE, store inited at 100 Hz.
- Boot POLAR override + timeout → stays SEEKING, no fallback.
- Boot LIS3DH override + Polar visible → no Polar scan.
- POLAR_ACTIVE + disconnect + reconnect within window → POLAR_ACTIVE,
  store **not** cleared, Kalman preserved.
- POLAR_ACTIVE + disconnect + timeout (AUTO) → LIS3DH_ACTIVE, store
  **cleared**, Kalman reinit.
- POLAR_ACTIVE + AT+IMU=LIS3DH → atomic switch, old IMU stopped before
  store cleared (assert call ordering).
- AT+IMU=BOGUS rejected by parser, state unchanged.
- Rate-change-on-reconnect (Polar returns different rate) → store
  cleared even though source didn't change.

**Extend `tests/test_polar_logic.c`** — RSSI-best-wins selection:

- 0 candidates → no connect attempt.
- 1 candidate → connect regardless of RSSI.
- N candidates (N > 1) → connect to max-RSSI.
- N > `PP_POLAR_MAX_CANDIDATES` → connect to max-RSSI among the first N
  kept. Overflow behavior documented, not "best of all".

**Extend `tests/test_console_commands.c`** — `AT+IMU=AUTO`,
`AT+IMU=LIS3DH`, `AT+IMU=POLAR`, `AT+IMU=BOGUS`, enhanced GET format.

### Build verification

From the verification gate above. Captured as `.map` artifacts in the
PR.

### Hardware integration (manual, documented in PR)

Steps written out so a reviewer can retrace them:

1. Cold-boot + strap on arm → `+IMU: source=POLAR,state=STREAMING`
   within ~5 s.
2. Cold-boot + no strap → `+IMU: source=LIS3DH,state=ACTIVE` after the
   boot-scan timeout.
3. Mid-session, power off strap → CSCP drops to 0 RPM; within 5 s
   falls back to LIS3DH.
4. Mid-session, re-strap before timeout → stays on Polar, no CSCP gap,
   Kalman warm (no confirm-count delay).
5. `AT+IMU=LIS3DH` at runtime → source switches; `AT+IMU` GET reflects
   both override and source.
6. Warm reset (wake from sleep) → override survives.
7. Two Polar straps nearby → connects to the closer one (deferred if
   only one strap available; document as known limit).

Non-testable without exotic setup: strap negotiating a non-52-Hz PMD
rate; battery-loss retained-RAM erasure.

## Risks and Mitigations

- **Per-IMU axis / energy tuning is initial-guess.** Polar-X axis and
  ~10× energy floor are best guesses until field-tested. Mitigation: the
  params struct exists precisely so these can be retuned without touching
  the algorithm; defaults are behind named `PP_*` macros for a single
  place to edit.
- **Two-strap RSSI test is hardware-dependent.** If only one Polar strap
  is available, the candidate-picking path is covered by host tests but
  not by hardware. Mitigation: host tests are the primary correctness
  surface; the `s_candidate_count < MAX` bounds check is validated there.
- **Retained-RAM ceiling was raised recently** (commit `912fb81`).
  Mitigation: verification gate 1 above enforces the ceiling as a
  numeric, linker-produced check, not a judgement call.
- **Latent rate-mismatch bug in sample-store init.** Fixed as part of
  this work via `pp_imu_polar_get_actual_sample_rate_hz`. Tested
  indirectly by "rate-change-on-reconnect" unit case.

## PR Structure for Review

The implementation is split into **two PRs** so each is small enough to be
reviewed cleanly (by `/ultrareview` or a human) without context overload.

### PR 1 — Stroke-rate params refactor (pure refactor, no runtime change)

**Intent:** introduce `pp_stroke_rate_params_t`; make the algorithm read from
a params pointer instead of macro constants. Add per-driver const params
blocks. No behavioral change in any single-IMU build.

**Files touched:**

| File | Change |
|---|---|
| `firmware/app/include/paddling_pulse_stroke_rate.h` | define `pp_stroke_rate_params_t`; change `pp_stroke_rate_init` signature to take `const pp_stroke_rate_params_t *` |
| `firmware/app/src/paddling_pulse_stroke_rate.c` | replace macro reads with `s_params->*`; store pointer in retained RAM |
| `firmware/app/include/paddling_pulse_imu_lis3dh.h` | declare `extern const pp_stroke_rate_params_t pp_imu_lis3dh_params` |
| `firmware/app/src/paddling_pulse_imu_lis3dh.c` | define `pp_imu_lis3dh_params`; replace `#if defined(CFG_IMU_AXIS_*)` with `switch(params->axis)` using compile-time-initialized axis |
| `firmware/app/include/paddling_pulse_imu_polar.h` | declare `extern const pp_stroke_rate_params_t pp_imu_polar_params`; declare `pp_imu_polar_get_actual_sample_rate_hz()` |
| `firmware/app/src/paddling_pulse_imu_polar.c` | define `pp_imu_polar_params`; implement accessor; replace `#if defined(CFG_IMU_AXIS_*)` with runtime read |
| `firmware/app/src/paddling_pulse_app.c` | pass `&pp_imu_<active>_params` to `pp_stroke_rate_init`; call accessor after Polar streams to re-init sample store with actual rate (fixes latent bug) |
| `tests/` | update existing stroke-rate test callers to pass params pointer |

**Acceptance criteria for PR 1** (reviewer can tick each from the diff):

- [ ] No new `CFG_IMU_DUAL` code in PR 1 — strictly a refactor.
- [ ] Every `#if defined(CFG_IMU_AXIS_*)` block in both drivers is removed;
      axis reads go through `params->axis`.
- [ ] `pp_stroke_rate_init` is the only entry point that changes signature;
      no other public API touched.
- [ ] `pp_imu_polar_get_actual_sample_rate_hz` has a comment stating it is
      only valid after `ON_STREAMING` (or after PMD settings are parsed).
- [ ] `pipeline_start` in `paddling_pulse_app.c` calls the accessor when
      `CFG_IMU_POLAR` is defined and re-inits the sample store if the
      actual rate differs from the hardcoded default — fixes the latent bug.
- [ ] `pp_imu_lis3dh_params` and `pp_imu_polar_params` are `const` and placed
      in `.rodata` (no `__SECTION_ZERO` or non-const storage).
- [ ] Default values in each params block reference the existing
      `PP_STROKE_RATE_*` / `PP_KALMAN_*` / `PP_AUTOCORR_*` macros where
      tuning is identical; only axis, sample rate, window, and energy floor
      are overridden.
- [ ] Existing host tests (`test_polar_logic`, `test_console_commands`)
      still pass with no modification to their logic — only call-site fixes
      for the new signature.
- [ ] All four existing build configs (`CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`,
      `CFG_IMU_MPU6050`, each with their respective `CFG_IMU_AXIS_*`) build
      clean via `mingw32-make` with zero linker warnings.
- [ ] Code-style section satisfied: no magic numbers in new code
      (timeouts, sizes, thresholds all named constants); no abbreviated
      identifiers; bounded parsing; include-guard / include-order
      conventions respected; no Claude attribution anywhere.

### PR 2 — Dual-IMU runtime manager

**Intent:** add `CFG_IMU_DUAL` mode with the manager module, state machine,
RSSI-best-wins Polar picker, and `AT+IMU=X` console SET command.

**Files touched:**

| File | Change |
|---|---|
| `firmware/app/include/paddling_pulse_imu_manager.h` | **new:** enum types, public API, named timeouts |
| `firmware/app/src/paddling_pulse_imu_manager.c` | **new:** state machine + retained-RAM state |
| `firmware/app/include/paddling_pulse_imu.h` | under `CFG_IMU_DUAL`, replace inline dispatchers with manager-delegating declarations; extend the mutual-exclusion guard |
| `firmware/app/src/paddling_pulse_imu_polar.c` | add RSSI candidate buffer + window timer; emit manager events under `CFG_IMU_DUAL` |
| `firmware/app/src/paddling_pulse_imu_lis3dh.c` | compile under `CFG_IMU_DUAL` as well as `CFG_IMU_LIS3DH` |
| `firmware/app/src/paddling_pulse_app.c` | call manager API under `CFG_IMU_DUAL`; existing `pp_imu_*` API otherwise |
| `firmware/app/src/paddling_pulse_console_commands.c` | add `PP_CONSOLE_CMD_IMU_SET` parse; token-bounded `AUTO`/`LIS3DH`/`POLAR` |
| `firmware/app/src/paddling_pulse_console.c` | handler for `IMU_SET`; enhanced `IMU_GET` output |
| `firmware/config/da14531_config_basic.h` | add `CFG_IMU_DUAL` option and default timeout defines |
| `tests/test_imu_manager.c` | **new:** state machine cases |
| `tests/test_polar_logic.c` | extend with RSSI picker cases |
| `tests/test_console_commands.c` | extend with `AT+IMU=X` cases |

**Acceptance criteria for PR 2** (reviewer ticks from diff + PR description):

Code-level (diff review):

- [ ] `paddling_pulse_imu.h` mutual-exclusion guard includes `CFG_IMU_DUAL`
      in the "exactly one" count.
- [ ] Every new static variable that needs to survive sleep carries
      `__SECTION_ZERO("retention_mem_area0")`. Grep the diff.
- [ ] `s_candidates` in `paddling_pulse_imu_polar.c` has **no**
      `__SECTION_ZERO` attribute.
- [ ] No magic numbers for timeouts or sizes; every value is a named
      constant (`PP_POLAR_BOOT_SCAN_TIMEOUT_MS`,
      `PP_POLAR_RECONNECT_TIMEOUT_MS`, `PP_POLAR_RSSI_WINDOW_MS`,
      `PP_POLAR_MAX_CANDIDATES`).
- [ ] `AT+IMU=X` parsing uses `pp_console_token_equals` (length-bounded),
      not raw `strcmp`.
- [ ] Every `pp_imu_<x>_start` in the manager is paired with a
      `pp_imu_<x>_stop` on every exit path out of the corresponding state.
- [ ] The switch sequence in the manager calls `stop → sample_store_init →
      stroke_rate_init → start` **in that order**. Reviewer can confirm
      from the single `imu_manager_switch_source` function.
- [ ] Under `CFG_IMU_POLAR` (single-IMU, not DUAL), the Polar event
      callbacks remain inert — no manager calls compile in.
- [ ] No `malloc`, no new `KE_MSG_ALLOC` call sites beyond what existed.
- [ ] No Claude co-author attribution in any commit.
- [ ] Code-style section satisfied: every new `.c` file scanned for bare
      integer literals in expressions (only loop counters, array
      indices into fixed-size compile-time bounds, and zero-init are
      acceptable); any other literal is a named constant. Include
      guards, include-order, one-responsibility-per-function,
      ≤ 4 parameters, and in-place modification of existing blocks all
      verified by diff review.

Test coverage (diff review):

- [ ] `test_imu_manager.c` includes each state-machine case from the
      "Testing Strategy" section (boot AUTO + Polar found, boot AUTO +
      timeout, POLAR override + timeout, LIS3DH override, reconnect within
      window, reconnect after timeout, atomic override switch, BOGUS
      rejection, rate-change-on-reconnect).
- [ ] `test_polar_logic.c` includes the four RSSI cases (0, 1, N > 1,
      N > MAX).
- [ ] `test_console_commands.c` covers `AT+IMU=AUTO`, `AT+IMU=LIS3DH`,
      `AT+IMU=POLAR`, `AT+IMU=BOGUS`, and enhanced GET format.

Verification evidence (must appear in PR description, not the diff):

- [ ] `.map` diff for `retention_mem_area0`: before bytes, after bytes,
      headroom remaining. Numeric, copy-pasted from the linker output.
- [ ] `.map` diff for `.text` + `.rodata`: before, after, headroom.
- [ ] Output of four build configs (`CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`,
      `CFG_IMU_MPU6050`, `CFG_IMU_DUAL`), each showing clean build with
      zero linker warnings.
- [ ] Host test suite output: all tests pass.
- [ ] Hardware integration steps 1–6 from the "Testing Strategy" section
      checked off with observed behavior (text from console serial).
- [ ] Step 7 (two-strap RSSI) either checked off with observed behavior
      or explicitly marked "deferred — single-strap environment" if only
      one strap is available.

## PR Description Template

Every PR in this work must paste this template (populated) into its
description so automated review has the evidence it needs without having
to infer from the diff:

```
## Scope
[one-line summary + link to docs/superpowers/specs/2026-04-19-dual-imu-lis3dh-polar-design.md]

## Acceptance Criteria
[copy the matching checklist from the spec; tick each item]

## Memory Verification
retention_mem_area0: before=XXXX B, after=YYYY B, ceiling=ZZZZ B, headroom=WWWW B
.text + .rodata:     before=XXXX B, after=YYYY B, ceiling=ZZZZ B, headroom=WWWW B

## Build Matrix
CFG_IMU_LIS3DH : PASS / FAIL (link to linker output)
CFG_IMU_POLAR  : PASS / FAIL
CFG_IMU_MPU6050: PASS / FAIL
CFG_IMU_DUAL   : PASS / FAIL (PR 2 only)

## Host Tests
[copy test runner output]

## Hardware Integration (PR 2 only)
Step 1 cold-boot + strap:       [observed behavior]
Step 2 cold-boot + no strap:    [observed behavior]
Step 3 mid-session drop:        [observed behavior]
Step 4 drop + reconnect:        [observed behavior]
Step 5 AT+IMU=LIS3DH runtime:   [observed behavior]
Step 6 warm reset:              [observed behavior]
Step 7 two straps:              [observed behavior | DEFERRED with reason]
```

## Success Criteria

- `CFG_IMU_DUAL` builds cleanly; all three single-IMU builds still build
  cleanly and behave identically to their pre-change behavior.
- The verification-gate numbers (retained-RAM, section placement, flash,
  stack, linker warnings) are captured in the PR and all pass.
- Host unit tests cover the state machine, RSSI picker, and console
  parser; all pass.
- Hardware flows 1–6 above reproduce as documented.
- No Claude co-author attribution in any commit or artifact.
