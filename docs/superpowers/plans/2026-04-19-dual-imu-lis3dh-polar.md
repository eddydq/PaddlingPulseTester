# Dual-IMU (LIS3DH + Polar) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add runtime LIS3DH↔Polar switching (`CFG_IMU_DUAL`) with RSSI-best-wins strap selection, per-IMU tuning, safe sample-store handoff, and `AT+IMU=AUTO|LIS3DH|POLAR` console override — without regressing the three existing single-IMU builds.

**Architecture:** Two PRs. PR 1 is a pure refactor: introduce `pp_stroke_rate_params_t`, route all algorithm tunables through a params pointer, move axis selection from compile-time `#if` to runtime `switch`, and fix the latent 52-Hz-hardcode bug in `pipeline_start`. PR 2 adds the `paddling_pulse_imu_manager` module, the RSSI candidate buffer, the state machine, the console SET command, and the `CFG_IMU_DUAL` wiring — always behind the new config, leaving single-IMU paths byte-identical.

**Tech Stack:** C99 firmware on the Renesas DA14531 SDK (armclang / AC6 via `mingw32-make`). MinGW `gcc` for host-side unit tests. Retained RAM via `__SECTION_ZERO("retention_mem_area0")`. `app_easy_timer` for scheduled callbacks. Kernel-task context for all event handlers (no ISR preemption, no locks).

**Spec:** [2026-04-19-dual-imu-lis3dh-polar-design.md](../specs/2026-04-19-dual-imu-lis3dh-polar-design.md). Read §Selection Mechanism, §Architecture, §Sample-store handoff protocol, and §Memory Efficiency and Verification before starting.

---

## Conventions Used Throughout This Plan

- **Absolute paths.** Every file path is given relative to the repo root `c:\dev\_work\PaddlingPulse\`. Bash commands run from the repo root.
- **TDD.** Every code change is preceded by a failing test (host-side where possible) and followed by running that test.
- **Host tests compile standalone.** The `Makefile` has no test target; host tests are compiled directly with MinGW `gcc`. Example:
  ```
  gcc -std=c99 -Wall -Wextra -I firmware/app/include \
      tests/test_polar_logic.c \
      firmware/app/src/paddling_pulse_imu_polar_logic.c \
      -o build/test_polar_logic.exe
  ```
- **Firmware build.** `mingw32-make` from the repo root. Never `make`. Expected: clean build, `build/PaddlingPulse.hex` produced, zero linker warnings.
- **Build-matrix runs** swap which `CFG_IMU_*` is defined in `firmware/config/da14531_config_basic.h` (exactly one at a time; `CFG_IMU_DUAL` is mutually exclusive with the others). After each config swap, `mingw32-make clean && mingw32-make`.
- **Commits.** One commit per completed step block (test passing + impl done). No Claude co-author line. No `--no-verify`. Branch is `feat/stroke-rate-pipeline-app-layer`.
- **Git hygiene.** Never stage build artifacts. `git add <exact file list>` only — never `-A` or `.`.
- **Style (from spec §Code Style):** no magic numbers in any new code (every timeout / size / threshold is a named `#define` or `enum`), descriptive identifiers, bounded parsing, include-guard convention `_PADDLING_PULSE_<NAME>_H_`, `why` comments only.

---

## File Structure

### PR 1 — Stroke-rate params refactor

| File | Responsibility | Create / Modify |
|---|---|---|
| `firmware/app/include/paddling_pulse_stroke_rate.h` | Declare `pp_stroke_rate_params_t`; change `pp_stroke_rate_init` signature | Modify |
| `firmware/app/src/paddling_pulse_stroke_rate.c` | Read tunables from `s_params` pointer, not macros | Modify |
| `firmware/app/include/paddling_pulse_imu_lis3dh.h` | Declare `pp_imu_lis3dh_params`; declare `PP_AXIS_*` enum | Modify |
| `firmware/app/src/paddling_pulse_imu_lis3dh.c` | Define `pp_imu_lis3dh_params` (const, `.rodata`); replace `#if defined(CFG_IMU_AXIS_*)` with runtime switch on `params->axis` | Modify |
| `firmware/app/include/paddling_pulse_imu_polar.h` | Declare `pp_imu_polar_params`; declare `pp_imu_polar_get_actual_sample_rate_hz` | Modify |
| `firmware/app/src/paddling_pulse_imu_polar.c` | Define `pp_imu_polar_params`; implement accessor; replace `#if defined(CFG_IMU_AXIS_*)` with runtime switch | Modify |
| `firmware/app/src/paddling_pulse_app.c` | Pass `&pp_imu_<active>_params` to `pp_stroke_rate_init`; call Polar rate accessor to fix 52-Hz hardcode | Modify |
| `tests/test_stroke_rate_params.c` | New host test: algorithm reads tunables from params (rate changes the divisor) | Create |

### PR 2 — Dual-IMU runtime manager

| File | Responsibility | Create / Modify |
|---|---|---|
| `firmware/app/include/paddling_pulse_imu_manager.h` | Public API: `pp_imu_source`, `pp_imu_override`, timeouts, manager entry points, events | Create |
| `firmware/app/src/paddling_pulse_imu_manager.c` | State machine, retained-RAM state, switch-source sequence | Create |
| `firmware/app/include/paddling_pulse_imu.h` | Under `CFG_IMU_DUAL`: delegate to manager; extend mutual-exclusion guard | Modify |
| `firmware/app/include/paddling_pulse_sample_store.h` | Declare `PP_SAMPLE_STORE_MIN_RATE_HZ` / `MAX_RATE_HZ` constants | Modify |
| `firmware/app/src/paddling_pulse_imu_polar.c` | Replace first-match-wins adv-report with RSSI candidate buffer + window timer; emit manager events under `CFG_IMU_DUAL` | Modify |
| `firmware/app/src/paddling_pulse_imu_lis3dh.c` | Compile into `CFG_IMU_DUAL` builds too | Modify |
| `firmware/app/src/paddling_pulse_app.c` | Call manager API under `CFG_IMU_DUAL` | Modify |
| `firmware/app/include/paddling_pulse_console_commands.h` | Declare `PP_CONSOLE_CMD_IMU_SET` handler | Modify |
| `firmware/app/src/paddling_pulse_console_commands.c` | Parse `AT+IMU=AUTO\|LIS3DH\|POLAR` with length-bounded token compare | Modify |
| `firmware/app/src/paddling_pulse_console.c` | Dispatch `IMU_SET` to manager; enhance `IMU_GET` output format | Modify |
| `firmware/config/da14531_config_basic.h` | Add `CFG_IMU_DUAL` option (commented); declare timeout defaults | Modify |
| `firmware/config/Makefile` sources list | Add `paddling_pulse_imu_manager.c` to `SOURCES` in root `Makefile` | Modify |
| `tests/test_imu_manager.c` | New host test: state machine transitions, switch-sequence ordering | Create |
| `tests/test_polar_rssi_picker.c` | New host test: candidate-buffer RSSI selection (extracted so it compiles without SDK dependencies) | Create |
| `tests/test_console_commands.c` | Extend with `AT+IMU=AUTO\|LIS3DH\|POLAR\|BOGUS` and enhanced GET | Modify |

---

## PR 1 — Stroke-Rate Params Refactor

**Intent:** pure refactor. Every single-IMU build (`CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`, `CFG_IMU_MPU6050`) must behave byte-identically after this PR. The only observable change is the latent 52-Hz-hardcode bug fix in `pipeline_start`.

### Task 1.1: Declare `pp_stroke_rate_params_t` and `PP_AXIS_*` enum (header-only)

**Files:**
- Modify: `firmware/app/include/paddling_pulse_stroke_rate.h`

- [ ] **Step 1: Add the struct and axis enum to the header**

At the top of `paddling_pulse_stroke_rate.h`, after the existing `#include <stdint.h>`, add:

```c
/* Axis selector — runtime replacement for CFG_IMU_AXIS_* macros. */
typedef enum
{
    PP_AXIS_X = 0,
    PP_AXIS_Y = 1,
    PP_AXIS_Z = 2,
} pp_axis_t;

/*
 * Per-IMU algorithm tuning. Instances live in .rodata (const) — zero
 * retained-RAM cost. The stroke-rate module stores a pointer to the
 * active params and reads tunables through it on every update.
 */
typedef struct
{
    pp_axis_t axis;
    uint16_t  sample_rate_hz;
    uint16_t  window_samples;
    uint16_t  min_rpm;
    uint16_t  max_rpm;
    int32_t   kalman_q;                  /* Q16.16 */
    int32_t   kalman_r;                  /* Q16.16 */
    int32_t   kalman_p_max;              /* Q16.16 */
    int32_t   autocorr_confidence_min;   /* Q16.16 */
    int32_t   autocorr_energy_min;
    uint8_t   autocorr_harmonic_pct;
    uint16_t  kalman_confirm_tolerance_rpm;
    uint16_t  kalman_max_jump_rpm;
    uint8_t   kalman_invalid_max;
    uint8_t   kalman_confirm_count;
} pp_stroke_rate_params_t;
```

Change the `pp_stroke_rate_init` declaration from:
```c
void pp_stroke_rate_init(void);
```
to:
```c
/**
 * @brief Reset Kalman state; latch the pointer to the active params block.
 * @param params  Non-NULL pointer to a params block with static lifetime
 *                (typically &pp_imu_<driver>_params). Stored by pointer,
 *                not copied — caller must keep it alive.
 */
void pp_stroke_rate_init(const pp_stroke_rate_params_t *params);
```

- [ ] **Step 2: Verify firmware still fails to build (signature mismatch is expected)**

Run: `mingw32-make clean && mingw32-make`
Expected: compile error at the call site in `paddling_pulse_app.c` (`pp_stroke_rate_init();` now has wrong arity). This proves the new signature is in effect.

**Do not commit yet** — commit after the whole refactor compiles (task 1.4).

---

### Task 1.2: Refactor `pp_stroke_rate.c` to read from params pointer

**Files:**
- Modify: `firmware/app/src/paddling_pulse_stroke_rate.c`

- [ ] **Step 1: Store a pointer to params in retained RAM**

At the top of the file, near the other retained-RAM statics, add:

```c
static const pp_stroke_rate_params_t *s_params __SECTION_ZERO("retention_mem_area0");
```

(Matches the `__SECTION_ZERO` pattern used throughout `paddling_pulse_app.c`.)

- [ ] **Step 2: Change `pp_stroke_rate_init` to take and store the pointer**

```c
void pp_stroke_rate_init(const pp_stroke_rate_params_t *params)
{
    if (params == NULL)
    {
        return;
    }
    s_params = params;
    /* existing Kalman / counter reset code below, unchanged */
}
```

- [ ] **Step 3: Replace every macro read with `s_params->*`**

In `pp_stroke_rate_update` and any helper statics, systematically replace:

| Before | After |
|---|---|
| `PP_STROKE_RATE_MIN_RPM` | `s_params->min_rpm` |
| `PP_STROKE_RATE_MAX_RPM` | `s_params->max_rpm` |
| `PP_STROKE_RATE_WINDOW` | `s_params->window_samples` |
| `PP_KALMAN_Q` | `s_params->kalman_q` |
| `PP_KALMAN_R` | `s_params->kalman_r` |
| `PP_KALMAN_P_MAX` | `s_params->kalman_p_max` |
| `PP_AUTOCORR_CONFIDENCE_MIN` | `s_params->autocorr_confidence_min` |
| `PP_AUTOCORR_HARMONIC_PCT` | `s_params->autocorr_harmonic_pct` |
| `PP_AUTOCORR_ENERGY_MIN` | `s_params->autocorr_energy_min` |
| `PP_KALMAN_CONFIRM_TOLERANCE_RPM` | `s_params->kalman_confirm_tolerance_rpm` |
| `PP_KALMAN_MAX_JUMP_RPM` | `s_params->kalman_max_jump_rpm` |
| `PP_KALMAN_INVALID_MAX` | `s_params->kalman_invalid_max` |
| `PP_KALMAN_CONFIRM_COUNT` | `s_params->kalman_confirm_count` |

`PP_STROKE_RATE_INTERVAL_MS` stays a `#define` — it drives the `app_easy_timer` callback cadence from `paddling_pulse_app.c`, not the algorithm.

Do **not** delete the `PP_*` macros from the header — they remain as named defaults the params blocks reference.

- [ ] **Step 4: Early-return from `pp_stroke_rate_update` if `s_params == NULL`**

Guard against an update called before init:

```c
uint8_t pp_stroke_rate_update(void)
{
    if (s_params == NULL)
    {
        return 0;
    }
    /* existing body */
}
```

---

### Task 1.3: Define per-driver params blocks

**Files:**
- Modify: `firmware/app/include/paddling_pulse_imu_lis3dh.h`
- Modify: `firmware/app/src/paddling_pulse_imu_lis3dh.c`
- Modify: `firmware/app/include/paddling_pulse_imu_polar.h`
- Modify: `firmware/app/src/paddling_pulse_imu_polar.c`

- [ ] **Step 1: Declare extern params in LIS3DH header**

Add to `paddling_pulse_imu_lis3dh.h` (above the function declarations, after includes):

```c
#include "paddling_pulse_stroke_rate.h"

extern const pp_stroke_rate_params_t pp_imu_lis3dh_params;
```

- [ ] **Step 2: Define LIS3DH params in `paddling_pulse_imu_lis3dh.c`**

Near the top of the file, after includes:

```c
/*
 * Boat-mounted accelerometer tuning. Axis Z is up/down with the
 * paddler's cadence signature dominating the low-frequency band.
 * Energy floor reflects boat-scale amplitudes (≈ 1/10 of arm-swing).
 */
const pp_stroke_rate_params_t pp_imu_lis3dh_params = {
#if defined(CFG_IMU_AXIS_X)
    .axis = PP_AXIS_X,
#elif defined(CFG_IMU_AXIS_Y)
    .axis = PP_AXIS_Y,
#else
    .axis = PP_AXIS_Z,
#endif
    .sample_rate_hz                 = 100,
    .window_samples                 = PP_STROKE_RATE_WINDOW,
    .min_rpm                        = PP_STROKE_RATE_MIN_RPM,
    .max_rpm                        = PP_STROKE_RATE_MAX_RPM,
    .kalman_q                       = PP_KALMAN_Q,
    .kalman_r                       = PP_KALMAN_R,
    .kalman_p_max                   = PP_KALMAN_P_MAX,
    .autocorr_confidence_min        = PP_AUTOCORR_CONFIDENCE_MIN,
    .autocorr_energy_min            = PP_AUTOCORR_ENERGY_MIN,
    .autocorr_harmonic_pct          = PP_AUTOCORR_HARMONIC_PCT,
    .kalman_confirm_tolerance_rpm   = PP_KALMAN_CONFIRM_TOLERANCE_RPM,
    .kalman_max_jump_rpm            = PP_KALMAN_MAX_JUMP_RPM,
    .kalman_invalid_max             = PP_KALMAN_INVALID_MAX,
    .kalman_confirm_count           = PP_KALMAN_CONFIRM_COUNT,
};
```

The `#if defined(CFG_IMU_AXIS_*)` block here is compile-time only — it seeds the **initializer**. It's the same constant every build. The runtime switch in step 4 replaces the runtime read of these macros, which is what the spec calls out.

- [ ] **Step 3: Replace the runtime axis `#if` block at `paddling_pulse_imu_lis3dh.c:181-187`**

Find the existing block that dispatches which axis to push into the sample store. Replace the compile-time `#if defined(CFG_IMU_AXIS_*)` with a runtime switch on `pp_imu_lis3dh_params.axis`:

```c
int16_t sample;
switch (pp_imu_lis3dh_params.axis)
{
    case PP_AXIS_X: sample = x; break;
    case PP_AXIS_Y: sample = y; break;
    case PP_AXIS_Z: /* fallthrough */
    default:        sample = z; break;
}
pp_sample_store_push(sample);
```

Exact variable names (`x`, `y`, `z`) match whatever the existing code uses at that line range — read the surrounding context before editing.

- [ ] **Step 4: Declare extern Polar params + actual-rate accessor in Polar header**

Add to `paddling_pulse_imu_polar.h`:

```c
#include "paddling_pulse_stroke_rate.h"

extern const pp_stroke_rate_params_t pp_imu_polar_params;

/**
 * @brief Sample rate the Polar strap actually negotiated via PMD settings.
 *
 * Meaningful only after the strap has answered the PMD settings query and
 * begun streaming. Before that, returns PP_STROKE_RATE_DEFAULT_POLAR_HZ (52).
 * The Polar driver latches the rate when parsing the settings response,
 * before it emits ON_STREAMING to the manager.
 */
uint16_t pp_imu_polar_get_actual_sample_rate_hz(void);
```

- [ ] **Step 5: Define Polar params and implement accessor in `paddling_pulse_imu_polar.c`**

Add the default-rate constant to `paddling_pulse_stroke_rate.h` alongside the other defaults (so both drivers can reference it):

```c
#ifndef PP_STROKE_RATE_DEFAULT_POLAR_HZ
#define PP_STROKE_RATE_DEFAULT_POLAR_HZ     52
#endif
#ifndef PP_STROKE_RATE_DEFAULT_LIS3DH_HZ
#define PP_STROKE_RATE_DEFAULT_LIS3DH_HZ    100
#endif
```

(Update the LIS3DH params block to use `PP_STROKE_RATE_DEFAULT_LIS3DH_HZ` instead of the bare `100` literal — zero magic numbers.)

Then in `paddling_pulse_imu_polar.c`:

```c
const pp_stroke_rate_params_t pp_imu_polar_params = {
#if defined(CFG_IMU_AXIS_X)
    .axis = PP_AXIS_X,
#elif defined(CFG_IMU_AXIS_Y)
    .axis = PP_AXIS_Y,
#else
    .axis = PP_AXIS_Z,
#endif
    .sample_rate_hz                 = PP_STROKE_RATE_DEFAULT_POLAR_HZ,
    /* Arm-swing signature is larger than boat-swing. Window covers
     * ≈ 4–5 strokes at PP_STROKE_RATE_MIN_RPM at 52 Hz (256 samples). */
    .window_samples                 = 256,
    .min_rpm                        = PP_STROKE_RATE_MIN_RPM,
    .max_rpm                        = PP_STROKE_RATE_MAX_RPM,
    .kalman_q                       = PP_KALMAN_Q,
    .kalman_r                       = PP_KALMAN_R,
    .kalman_p_max                   = PP_KALMAN_P_MAX,
    .autocorr_confidence_min        = PP_AUTOCORR_CONFIDENCE_MIN,
    /* Arm-scale amplitude ≈ 10× boat-scale; energy floor scales accordingly. */
    .autocorr_energy_min            = PP_AUTOCORR_ENERGY_MIN * 10,
    .autocorr_harmonic_pct          = PP_AUTOCORR_HARMONIC_PCT,
    .kalman_confirm_tolerance_rpm   = PP_KALMAN_CONFIRM_TOLERANCE_RPM,
    .kalman_max_jump_rpm            = PP_KALMAN_MAX_JUMP_RPM,
    .kalman_invalid_max             = PP_KALMAN_INVALID_MAX,
    .kalman_confirm_count           = PP_KALMAN_CONFIRM_COUNT,
};
```

Replace the bare-literal 256 with a named constant declared alongside the defaults:

```c
#ifndef PP_STROKE_RATE_WINDOW_POLAR
#define PP_STROKE_RATE_WINDOW_POLAR         256
#endif
```

Then `.window_samples = PP_STROKE_RATE_WINDOW_POLAR`.

For the accessor, locate `s_polar.acc_sample_rate_hz` (roughly line 413 per the spec) and add:

```c
uint16_t pp_imu_polar_get_actual_sample_rate_hz(void)
{
    if (s_polar.acc_sample_rate_hz == 0u)
    {
        return PP_STROKE_RATE_DEFAULT_POLAR_HZ;
    }
    return s_polar.acc_sample_rate_hz;
}
```

- [ ] **Step 6: Replace the runtime axis `#if` block at `paddling_pulse_imu_polar.c:307-313`**

Same pattern as LIS3DH — find the existing block that selects which axis of the PMD ACC payload to push into the sample store, replace with:

```c
int16_t sample;
switch (pp_imu_polar_params.axis)
{
    case PP_AXIS_X: sample = acc_x; break;
    case PP_AXIS_Y: sample = acc_y; break;
    case PP_AXIS_Z: /* fallthrough */
    default:        sample = acc_z; break;
}
pp_sample_store_push(sample);
```

Match the surrounding variable names (they may be `acc_x`/`ax`/etc.) by reading the existing code before editing.

---

### Task 1.4: Wire params into `pipeline_start` and fix the 52-Hz hardcode

**Files:**
- Modify: `firmware/app/src/paddling_pulse_app.c`

- [ ] **Step 1: Pass the correct params pointer and actual rate into `pipeline_start`**

Locate `pipeline_start` at roughly `firmware/app/src/paddling_pulse_app.c:285`. Replace:

```c
static void pipeline_start(void)
{
#ifdef CFG_IMU_POLAR
    pp_sample_store_init(52);
#else
    pp_sample_store_init(100);
#endif
    pp_stroke_rate_init();

    imu_active = pp_imu_init();
    if (imu_active)
    {
        pp_imu_start();
    }
    /* ... */
}
```

with (single-IMU paths only in PR 1 — the `CFG_IMU_DUAL` branch lands in PR 2):

```c
static void pipeline_start(void)
{
#if defined(CFG_IMU_LIS3DH)
    const pp_stroke_rate_params_t *params = &pp_imu_lis3dh_params;
    uint16_t rate_hz = params->sample_rate_hz;
#elif defined(CFG_IMU_POLAR)
    const pp_stroke_rate_params_t *params = &pp_imu_polar_params;
    /* Actual rate is known only after the strap answers the PMD
     * settings query. pp_imu_polar_start queues that query; we'll
     * re-init the sample store from the Polar driver when the
     * negotiated rate lands (see pp_imu_polar_on_streaming_start). */
    uint16_t rate_hz = params->sample_rate_hz;  /* provisional */
#elif defined(CFG_IMU_MPU6050)
    const pp_stroke_rate_params_t *params = &pp_imu_lis3dh_params;  /* MPU6050 shares LIS3DH tuning today */
    uint16_t rate_hz = params->sample_rate_hz;
#endif

    pp_sample_store_init(rate_hz);
    pp_stroke_rate_init(params);

    imu_active = pp_imu_init();
    if (imu_active)
    {
        pp_imu_start();
    }
    /* ... rest of existing body unchanged ... */
}
```

- [ ] **Step 2: Fix the latent 52-Hz hardcode bug**

The Polar strap may negotiate a rate other than 52 Hz. In `paddling_pulse_imu_polar.c`, locate the point where the driver first successfully parses the PMD settings response and begins streaming (it already emits whatever equivalent of `on_streaming` exists). At that point, call:

```c
uint16_t actual_rate_hz = pp_imu_polar_get_actual_sample_rate_hz();
if (actual_rate_hz != pp_sample_store_get_rate_hz())
{
    pp_sample_store_init(actual_rate_hz);
    pp_stroke_rate_init(&pp_imu_polar_params);
}
```

This is safe because Polar events run in kernel-task context — no sample push can interleave. It runs at most once per streaming-start.

Guard the whole hook under `#ifndef CFG_IMU_DUAL` so PR 2 can replace it with a manager-driven path. Mark the block with a one-line comment:

```c
/* Single-IMU Polar path: reconcile sample store with negotiated rate.
 * Under CFG_IMU_DUAL the manager owns this handoff instead. */
```

- [ ] **Step 3: Build all three single-IMU configs and verify zero linker warnings**

For each of `CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`, `CFG_IMU_MPU6050`:

1. Edit `firmware/config/da14531_config_basic.h` so that exactly that one is `#define`d (and the matching `CFG_IMU_AXIS_*`).
2. Run: `mingw32-make clean && mingw32-make`
3. Expected: clean build, `build/PaddlingPulse.hex` produced, zero linker warnings, zero `section overlap`, zero `region RAM overflowed`.

- [ ] **Step 4: Capture `retention_mem_area0` size for the PR description**

From the `.map` produced by armlink (look in `build/.tmp/`), record the `retention_mem_area0` size for the `CFG_IMU_POLAR` build (the tightest config post-commit `912fb81`). Save as "before" and "after" numbers for the PR 1 template — though PR 1 should show essentially no delta (params are `.rodata`, not RAM).

- [ ] **Step 5: Commit PR 1 refactor**

Restore the repo to `CFG_IMU_POLAR` (the branch default), then:

```bash
git add firmware/app/include/paddling_pulse_stroke_rate.h \
        firmware/app/src/paddling_pulse_stroke_rate.c \
        firmware/app/include/paddling_pulse_imu_lis3dh.h \
        firmware/app/src/paddling_pulse_imu_lis3dh.c \
        firmware/app/include/paddling_pulse_imu_polar.h \
        firmware/app/src/paddling_pulse_imu_polar.c \
        firmware/app/src/paddling_pulse_app.c
git commit -m "refactor(stroke-rate): route tunables through pp_stroke_rate_params_t"
```

Commit message body (second `-m` or via HEREDOC) should mention the latent 52-Hz bug fix and reference the spec at `docs/superpowers/specs/2026-04-19-dual-imu-lis3dh-polar-design.md`.

---

### Task 1.5: Host test — algorithm honors params->sample_rate_hz

**Files:**
- Create: `tests/test_stroke_rate_params.c`

**Intent:** prove the refactor actually routes a tunable through the params pointer. Exercise the smallest observable effect: the `60 * rate / lag` formula in the cadence conversion, which now reads rate from the sample store (which the manager initializes to match params).

The sample store is standalone (no SDK deps) and `pp_cadence_to_crank` reads the store rate — that's the test surface.

- [ ] **Step 1: Write the failing test**

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_stroke_rate.h"

/* Two fabricated params blocks at different rates. */
static const pp_stroke_rate_params_t params_52hz = {
    .axis = PP_AXIS_Z,
    .sample_rate_hz = 52,
    .window_samples = 256,
    .min_rpm = 30, .max_rpm = 200,
    .kalman_q = 262144, .kalman_r = 131072, .kalman_p_max = 655360000,
    .autocorr_confidence_min = 19661, .autocorr_energy_min = 5000,
    .autocorr_harmonic_pct = 80,
    .kalman_confirm_tolerance_rpm = 15, .kalman_max_jump_rpm = 20,
    .kalman_invalid_max = 3, .kalman_confirm_count = 3,
};

static const pp_stroke_rate_params_t params_100hz = {
    .axis = PP_AXIS_Z, .sample_rate_hz = 100,
    .window_samples = 512, .min_rpm = 30, .max_rpm = 200,
    .kalman_q = 262144, .kalman_r = 131072, .kalman_p_max = 655360000,
    .autocorr_confidence_min = 19661, .autocorr_energy_min = 5000,
    .autocorr_harmonic_pct = 80, .kalman_confirm_tolerance_rpm = 15,
    .kalman_max_jump_rpm = 20, .kalman_invalid_max = 3, .kalman_confirm_count = 3,
};

static void test_init_latches_params_without_crashing(void)
{
    pp_sample_store_init(params_52hz.sample_rate_hz);
    pp_stroke_rate_init(&params_52hz);
    assert(pp_sample_store_get_rate_hz() == 52);

    pp_sample_store_init(params_100hz.sample_rate_hz);
    pp_stroke_rate_init(&params_100hz);
    assert(pp_sample_store_get_rate_hz() == 100);
}

static void test_init_ignores_null_params(void)
{
    pp_stroke_rate_init(&params_52hz);
    pp_stroke_rate_init(NULL);
    /* No crash, no state change is observable here; the guarantee is
     * that a NULL pointer never takes effect — verified by the no-crash
     * and by the subsequent update returning 0 / caller's invariant. */
    assert(pp_stroke_rate_get_rpm() == 0);
}

int main(void)
{
    test_init_latches_params_without_crashing();
    test_init_ignores_null_params();
    printf("stroke-rate params tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Build the failing test and confirm it fails (no `__SECTION_ZERO` on host)**

The firmware uses `__SECTION_ZERO("retention_mem_area0")` — on the host this symbol must expand to nothing. Confirm the pattern already used by other host tests: if `paddling_pulse_stroke_rate.c` uses `__SECTION_ZERO`, add a host-side guard at the top of the file:

```c
#ifndef __SECTION_ZERO
#define __SECTION_ZERO(name)  /* empty on host builds */
#endif
```

Then build:

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include \
    tests/test_stroke_rate_params.c \
    firmware/app/src/paddling_pulse_stroke_rate.c \
    firmware/app/src/paddling_pulse_sample_store.c \
    -o build/test_stroke_rate_params.exe
```

Expected: either a link failure ("undefined reference to `__SECTION_ZERO`") before the host guard is in, **or** the test compiles and runs. If it fails, the guard is missing — add it. If it runs, proceed.

- [ ] **Step 3: Run the test and confirm it passes**

Run: `./build/test_stroke_rate_params.exe`
Expected: `stroke-rate params tests passed` and exit 0.

- [ ] **Step 4: Commit the test**

```bash
git add tests/test_stroke_rate_params.c firmware/app/src/paddling_pulse_stroke_rate.c
git commit -m "test(stroke-rate): host test for params-pointer latching"
```

---

### Task 1.6: PR 1 verification gate

- [ ] **Step 1: Run the full build matrix for PR 1**

For each of `CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`, `CFG_IMU_MPU6050`:
1. Edit `firmware/config/da14531_config_basic.h` so exactly that one is defined.
2. `mingw32-make clean && mingw32-make` → expect clean build, zero linker warnings.
3. Record the `retention_mem_area0` size from the `.map` file.

- [ ] **Step 2: Run all host tests**

Run the three host test builds (from the existing pattern):

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_polar_logic.c firmware/app/src/paddling_pulse_imu_polar_logic.c -o build/test_polar_logic.exe && ./build/test_polar_logic.exe
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_console_commands.c firmware/app/src/paddling_pulse_console_commands.c -o build/test_console_commands.exe && ./build/test_console_commands.exe
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_stroke_rate_params.c firmware/app/src/paddling_pulse_stroke_rate.c firmware/app/src/paddling_pulse_sample_store.c -o build/test_stroke_rate_params.exe && ./build/test_stroke_rate_params.exe
```

Expected: all three exit 0 with no assertion failures.

- [ ] **Step 3: Confirm PR 1 acceptance criteria from the spec**

Walk through the "Acceptance criteria for PR 1" checklist in `docs/superpowers/specs/2026-04-19-dual-imu-lis3dh-polar-design.md` (lines 542–569). Every box must tick from the diff and build logs.

- [ ] **Step 4: Open the PR using the template from the spec**

Populate the PR Description Template (spec lines 659–687) with the before/after numbers, build-matrix results, and host-test output. No Claude co-author. No "generated by" references.

---

## PR 2 — Dual-IMU Runtime Manager

**Intent:** add `CFG_IMU_DUAL` mode. Single-IMU builds must stay byte-identical to their post-PR-1 state — this is a strict additive change gated by the new config.

### Task 2.1: Declare sample-store rate bounds

**Files:**
- Modify: `firmware/app/include/paddling_pulse_sample_store.h`

- [ ] **Step 1: Add the rate-bound constants to the header**

After the existing `PP_SAMPLE_STORE_CAPACITY` block:

```c
#ifndef PP_SAMPLE_STORE_MIN_RATE_HZ
#define PP_SAMPLE_STORE_MIN_RATE_HZ    1
#endif
#ifndef PP_SAMPLE_STORE_MAX_RATE_HZ
#define PP_SAMPLE_STORE_MAX_RATE_HZ    1000
#endif
```

No behavioral change — the manager will consume these in task 2.7.

- [ ] **Step 2: Commit**

```bash
git add firmware/app/include/paddling_pulse_sample_store.h
git commit -m "feat(sample-store): declare rate bounds used by IMU manager"
```

---

### Task 2.2: Create the manager header (API only, no logic)

**Files:**
- Create: `firmware/app/include/paddling_pulse_imu_manager.h`

- [ ] **Step 1: Write the header with full API surface**

```c
#ifndef _PADDLING_PULSE_IMU_MANAGER_H_
#define _PADDLING_PULSE_IMU_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#include "paddling_pulse_stroke_rate.h"

/* Scan/reconnect timeouts — overridable in da14531_config_basic.h. */
#ifndef PP_POLAR_BOOT_SCAN_TIMEOUT_MS
#define PP_POLAR_BOOT_SCAN_TIMEOUT_MS      10000
#endif
#ifndef PP_POLAR_RECONNECT_TIMEOUT_MS
#define PP_POLAR_RECONNECT_TIMEOUT_MS      5000
#endif
#ifndef PP_POLAR_RSSI_WINDOW_MS
#define PP_POLAR_RSSI_WINDOW_MS            2000
#endif

typedef enum
{
    PP_IMU_NONE   = 0,
    PP_IMU_LIS3DH = 1,
    PP_IMU_POLAR  = 2,
} pp_imu_source_t;

/* AUTO is the zero enumerator so cold-boot __SECTION_ZERO gives the
 * right default (see spec §Cold-boot behavior). */
typedef enum
{
    PP_IMU_OVERRIDE_AUTO   = 0,
    PP_IMU_OVERRIDE_LIS3DH = 1,
    PP_IMU_OVERRIDE_POLAR  = 2,
} pp_imu_override_t;

typedef enum
{
    PP_IMU_STATE_IDLE           = 0,
    PP_IMU_STATE_POLAR_SEEKING  = 1,
    PP_IMU_STATE_POLAR_ACTIVE   = 2,
    PP_IMU_STATE_LIS3DH_ACTIVE  = 3,
} pp_imu_state_t;

typedef enum
{
    PP_IMU_EV_POLAR_STREAMING   = 0,
    PP_IMU_EV_POLAR_DISCONNECT  = 1,
    PP_IMU_EV_POLAR_SCAN_FAIL   = 2,
    PP_IMU_EV_RECONNECT_TIMEOUT = 3,
    PP_IMU_EV_BOOT_SCAN_TIMEOUT = 4,
} pp_imu_event_t;

/* Lifecycle — called from paddling_pulse_app.c. */
void            pp_imu_manager_init(void);
void            pp_imu_manager_start(void);
void            pp_imu_manager_stop(void);
void            pp_imu_manager_process(void);   /* tick while LIS3DH-active */

/* Driver → manager event notifications (Polar driver emits these). */
void            pp_imu_manager_on_event(pp_imu_event_t ev);

/* Console override. Returns false if target invalid. */
bool            pp_imu_manager_set_override(pp_imu_override_t target);
pp_imu_override_t pp_imu_manager_get_override(void);
pp_imu_source_t pp_imu_manager_get_source(void);
pp_imu_state_t  pp_imu_manager_get_state(void);
uint16_t        pp_imu_manager_get_rate_hz(void);

#endif /* _PADDLING_PULSE_IMU_MANAGER_H_ */
```

- [ ] **Step 2: Confirm it compiles as a translation unit**

Build just the header by including it from a stub `.c`:

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include -c -x c \
    firmware/app/include/paddling_pulse_imu_manager.h -o /dev/null
```

Expected: clean compile (header is standalone).

- [ ] **Step 3: Commit**

```bash
git add firmware/app/include/paddling_pulse_imu_manager.h
git commit -m "feat(imu-manager): declare public API and event/state enums"
```

---

### Task 2.3: Extract the manager state machine into pure-C helpers (TDD)

**Intent:** follow the `paddling_pulse_imu_polar_logic.c` pattern — put the pure-logic helpers in a host-testable module, and keep SDK-touching code in the main manager `.c`.

**Files:**
- Create: `firmware/app/include/paddling_pulse_imu_manager_logic.h`
- Create: `firmware/app/src/paddling_pulse_imu_manager_logic.c`
- Create: `tests/test_imu_manager.c`

- [ ] **Step 1: Write the failing test first**

```c
#include <assert.h>
#include <stdio.h>

#include "paddling_pulse_imu_manager.h"
#include "paddling_pulse_imu_manager_logic.h"

/* Transition table helper: given (current state, override, event),
 * return next state. No side effects. */

static void test_boot_auto_finds_polar(void)
{
    pp_imu_state_t next;
    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING, PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_POLAR_STREAMING);
    assert(next == PP_IMU_STATE_POLAR_ACTIVE);
}

static void test_boot_auto_times_out_to_lis3dh(void)
{
    pp_imu_state_t next;
    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING, PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_BOOT_SCAN_TIMEOUT);
    assert(next == PP_IMU_STATE_LIS3DH_ACTIVE);
}

static void test_polar_override_never_falls_back(void)
{
    pp_imu_state_t next;
    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING, PP_IMU_OVERRIDE_POLAR,
        PP_IMU_EV_BOOT_SCAN_TIMEOUT);
    assert(next == PP_IMU_STATE_POLAR_SEEKING);

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING, PP_IMU_OVERRIDE_POLAR,
        PP_IMU_EV_RECONNECT_TIMEOUT);
    assert(next == PP_IMU_STATE_POLAR_SEEKING);
}

static void test_polar_active_reconnect_within_window(void)
{
    pp_imu_state_t next;
    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_ACTIVE, PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_POLAR_DISCONNECT);
    assert(next == PP_IMU_STATE_POLAR_SEEKING);

    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING, PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_POLAR_STREAMING);
    assert(next == PP_IMU_STATE_POLAR_ACTIVE);
}

static void test_polar_active_reconnect_timeout_auto(void)
{
    pp_imu_state_t next;
    next = pp_imu_manager_logic_next_state(
        PP_IMU_STATE_POLAR_SEEKING, PP_IMU_OVERRIDE_AUTO,
        PP_IMU_EV_RECONNECT_TIMEOUT);
    assert(next == PP_IMU_STATE_LIS3DH_ACTIVE);
}

static void test_switch_needed_on_source_change(void)
{
    assert(pp_imu_manager_logic_switch_needed(
        PP_IMU_LIS3DH, 100, PP_IMU_POLAR, 52));
}

static void test_switch_not_needed_on_same_source_and_rate(void)
{
    assert(!pp_imu_manager_logic_switch_needed(
        PP_IMU_POLAR, 52, PP_IMU_POLAR, 52));
}

static void test_switch_needed_on_rate_change_same_source(void)
{
    assert(pp_imu_manager_logic_switch_needed(
        PP_IMU_POLAR, 52, PP_IMU_POLAR, 104));
}

static void test_rate_clamped_to_bounds(void)
{
    assert(pp_imu_manager_logic_clamp_rate(0) == 1);
    assert(pp_imu_manager_logic_clamp_rate(52) == 52);
    assert(pp_imu_manager_logic_clamp_rate(5000) == 1000);
}

int main(void)
{
    test_boot_auto_finds_polar();
    test_boot_auto_times_out_to_lis3dh();
    test_polar_override_never_falls_back();
    test_polar_active_reconnect_within_window();
    test_polar_active_reconnect_timeout_auto();
    test_switch_needed_on_source_change();
    test_switch_not_needed_on_same_source_and_rate();
    test_switch_needed_on_rate_change_same_source();
    test_rate_clamped_to_bounds();
    printf("imu manager logic tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Write the logic header**

`firmware/app/include/paddling_pulse_imu_manager_logic.h`:

```c
#ifndef _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_
#define _PADDLING_PULSE_IMU_MANAGER_LOGIC_H_

#include <stdbool.h>
#include <stdint.h>

#include "paddling_pulse_imu_manager.h"

/* Pure-logic helpers — no SDK dependencies. Host-testable. */

pp_imu_state_t pp_imu_manager_logic_next_state(pp_imu_state_t current,
                                               pp_imu_override_t override,
                                               pp_imu_event_t ev);

bool pp_imu_manager_logic_switch_needed(pp_imu_source_t current_source,
                                        uint16_t current_rate_hz,
                                        pp_imu_source_t new_source,
                                        uint16_t new_rate_hz);

uint16_t pp_imu_manager_logic_clamp_rate(uint16_t rate_hz);

#endif
```

- [ ] **Step 3: Implement the logic**

`firmware/app/src/paddling_pulse_imu_manager_logic.c`:

```c
#include "paddling_pulse_imu_manager_logic.h"
#include "paddling_pulse_sample_store.h"

pp_imu_state_t pp_imu_manager_logic_next_state(pp_imu_state_t current,
                                               pp_imu_override_t override,
                                               pp_imu_event_t ev)
{
    switch (current)
    {
        case PP_IMU_STATE_POLAR_SEEKING:
            switch (ev)
            {
                case PP_IMU_EV_POLAR_STREAMING:
                    return PP_IMU_STATE_POLAR_ACTIVE;

                case PP_IMU_EV_BOOT_SCAN_TIMEOUT:
                case PP_IMU_EV_RECONNECT_TIMEOUT:
                case PP_IMU_EV_POLAR_SCAN_FAIL:
                    /* AUTO falls back; POLAR keeps trying. */
                    return (override == PP_IMU_OVERRIDE_AUTO)
                           ? PP_IMU_STATE_LIS3DH_ACTIVE
                           : PP_IMU_STATE_POLAR_SEEKING;

                default:
                    return current;
            }

        case PP_IMU_STATE_POLAR_ACTIVE:
            if (ev == PP_IMU_EV_POLAR_DISCONNECT)
            {
                return PP_IMU_STATE_POLAR_SEEKING;
            }
            return current;

        case PP_IMU_STATE_LIS3DH_ACTIVE:
        case PP_IMU_STATE_IDLE:
        default:
            return current;
    }
}

bool pp_imu_manager_logic_switch_needed(pp_imu_source_t current_source,
                                        uint16_t current_rate_hz,
                                        pp_imu_source_t new_source,
                                        uint16_t new_rate_hz)
{
    return (current_source != new_source) ||
           (current_rate_hz != new_rate_hz);
}

uint16_t pp_imu_manager_logic_clamp_rate(uint16_t rate_hz)
{
    if (rate_hz < PP_SAMPLE_STORE_MIN_RATE_HZ)
    {
        return PP_SAMPLE_STORE_MIN_RATE_HZ;
    }
    if (rate_hz > PP_SAMPLE_STORE_MAX_RATE_HZ)
    {
        return PP_SAMPLE_STORE_MAX_RATE_HZ;
    }
    return rate_hz;
}
```

- [ ] **Step 4: Build and run the failing test, confirm it now passes**

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include \
    tests/test_imu_manager.c \
    firmware/app/src/paddling_pulse_imu_manager_logic.c \
    -o build/test_imu_manager.exe
./build/test_imu_manager.exe
```
Expected: `imu manager logic tests passed` and exit 0.

- [ ] **Step 5: Commit the pure-logic module**

```bash
git add firmware/app/include/paddling_pulse_imu_manager_logic.h \
        firmware/app/src/paddling_pulse_imu_manager_logic.c \
        tests/test_imu_manager.c
git commit -m "feat(imu-manager): host-testable state-transition and rate-clamp helpers"
```

---

### Task 2.4: Implement the manager driver (`paddling_pulse_imu_manager.c`)

**Files:**
- Create: `firmware/app/src/paddling_pulse_imu_manager.c`

This is the SDK-touching side: timers, direct calls into `pp_imu_lis3dh_*` / `pp_imu_polar_*`, retained-RAM state. Decisions come from the logic helpers tested in 2.3.

- [ ] **Step 1: Write the manager `.c`**

```c
#include <stdbool.h>
#include <stdint.h>

#include "arch.h"                /* __SECTION_ZERO */
#include "app_easy_timer.h"

#include "paddling_pulse_imu_manager.h"
#include "paddling_pulse_imu_manager_logic.h"
#include "paddling_pulse_imu_lis3dh.h"
#include "paddling_pulse_imu_polar.h"
#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_stroke_rate.h"

/* --- retained state --- */

typedef struct
{
    pp_imu_state_t     state;
    pp_imu_source_t    source;
    pp_imu_override_t  override;
    uint16_t           rate_hz;
    timer_hnd          scan_timer;
} pp_imu_manager_ctx_t;

static pp_imu_manager_ctx_t s_ctx __SECTION_ZERO("retention_mem_area0");

/* --- internal helpers --- */

static const pp_stroke_rate_params_t *params_for(pp_imu_source_t src)
{
    if (src == PP_IMU_POLAR)
    {
        return &pp_imu_polar_params;
    }
    return &pp_imu_lis3dh_params;
}

static void stop_current_driver(void)
{
    if (s_ctx.source == PP_IMU_LIS3DH)
    {
        pp_imu_lis3dh_stop();
    }
    else if (s_ctx.source == PP_IMU_POLAR)
    {
        pp_imu_polar_stop();
    }
}

static void start_driver(pp_imu_source_t src)
{
    if (src == PP_IMU_LIS3DH)
    {
        pp_imu_lis3dh_start();
    }
    else if (src == PP_IMU_POLAR)
    {
        pp_imu_polar_start();
    }
}

static void cancel_scan_timer(void)
{
    if (s_ctx.scan_timer != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(s_ctx.scan_timer);
        s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;
    }
}

static void boot_scan_timeout_cb(void)
{
    s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;
    pp_imu_manager_on_event(PP_IMU_EV_BOOT_SCAN_TIMEOUT);
}

static void reconnect_timeout_cb(void)
{
    s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;
    pp_imu_manager_on_event(PP_IMU_EV_RECONNECT_TIMEOUT);
}

/* Ordered switch sequence — stop, re-init store, re-init algo, start.
 * Spec §Sample-store handoff protocol. Ordering matters. */
static void switch_source(pp_imu_source_t new_source, uint16_t new_rate_hz)
{
    uint16_t clamped = pp_imu_manager_logic_clamp_rate(new_rate_hz);

    if (!pp_imu_manager_logic_switch_needed(s_ctx.source, s_ctx.rate_hz,
                                            new_source, clamped))
    {
        return;
    }

    stop_current_driver();
    pp_sample_store_init(clamped);
    pp_stroke_rate_init(params_for(new_source));
    start_driver(new_source);

    s_ctx.source  = new_source;
    s_ctx.rate_hz = clamped;
}

static void enter_state(pp_imu_state_t next)
{
    pp_imu_state_t prev = s_ctx.state;
    s_ctx.state = next;

    switch (next)
    {
        case PP_IMU_STATE_POLAR_SEEKING:
            cancel_scan_timer();
            if (s_ctx.override == PP_IMU_OVERRIDE_AUTO)
            {
                uint32_t timeout_ms = (prev == PP_IMU_STATE_POLAR_ACTIVE)
                                      ? PP_POLAR_RECONNECT_TIMEOUT_MS
                                      : PP_POLAR_BOOT_SCAN_TIMEOUT_MS;
                timer_callback cb = (prev == PP_IMU_STATE_POLAR_ACTIVE)
                                    ? reconnect_timeout_cb
                                    : boot_scan_timeout_cb;
                s_ctx.scan_timer = app_easy_timer(timeout_ms, cb);
            }
            /* The Polar driver's own scan machinery runs independently;
             * we just arm our fallback deadline. */
            break;

        case PP_IMU_STATE_POLAR_ACTIVE:
            cancel_scan_timer();
            switch_source(PP_IMU_POLAR, pp_imu_polar_get_actual_sample_rate_hz());
            break;

        case PP_IMU_STATE_LIS3DH_ACTIVE:
            cancel_scan_timer();
            switch_source(PP_IMU_LIS3DH, pp_imu_lis3dh_params.sample_rate_hz);
            break;

        case PP_IMU_STATE_IDLE:
            cancel_scan_timer();
            stop_current_driver();
            pp_sample_store_reset();
            s_ctx.source  = PP_IMU_NONE;
            s_ctx.rate_hz = 0;
            break;
    }
}

/* --- public API --- */

void pp_imu_manager_init(void)
{
    /* __SECTION_ZERO already zeroed s_ctx at cold boot. Re-init the
     * live fields; preserve s_ctx.override across warm resets. */
    s_ctx.state      = PP_IMU_STATE_IDLE;
    s_ctx.source     = PP_IMU_NONE;
    s_ctx.rate_hz    = 0;
    s_ctx.scan_timer = EASY_TIMER_INVALID_TIMER;

    (void)pp_imu_lis3dh_init();
    (void)pp_imu_polar_init();
}

void pp_imu_manager_start(void)
{
    if (s_ctx.override == PP_IMU_OVERRIDE_LIS3DH)
    {
        enter_state(PP_IMU_STATE_LIS3DH_ACTIVE);
        return;
    }
    enter_state(PP_IMU_STATE_POLAR_SEEKING);
}

void pp_imu_manager_stop(void)
{
    enter_state(PP_IMU_STATE_IDLE);
}

void pp_imu_manager_process(void)
{
    if (s_ctx.source == PP_IMU_LIS3DH)
    {
        pp_imu_lis3dh_process();
    }
}

void pp_imu_manager_on_event(pp_imu_event_t ev)
{
    pp_imu_state_t next = pp_imu_manager_logic_next_state(s_ctx.state,
                                                          s_ctx.override, ev);
    if (next != s_ctx.state)
    {
        enter_state(next);
    }
}

bool pp_imu_manager_set_override(pp_imu_override_t target)
{
    if (target != PP_IMU_OVERRIDE_AUTO &&
        target != PP_IMU_OVERRIDE_LIS3DH &&
        target != PP_IMU_OVERRIDE_POLAR)
    {
        return false;
    }

    s_ctx.override = target;

    /* Atomic switch sequence if the override changes the active source. */
    if (target == PP_IMU_OVERRIDE_LIS3DH && s_ctx.source != PP_IMU_LIS3DH)
    {
        enter_state(PP_IMU_STATE_LIS3DH_ACTIVE);
    }
    else if (target == PP_IMU_OVERRIDE_POLAR && s_ctx.source != PP_IMU_POLAR)
    {
        enter_state(PP_IMU_STATE_POLAR_SEEKING);
    }
    return true;
}

pp_imu_override_t pp_imu_manager_get_override(void) { return s_ctx.override; }
pp_imu_source_t   pp_imu_manager_get_source(void)   { return s_ctx.source;   }
pp_imu_state_t    pp_imu_manager_get_state(void)    { return s_ctx.state;    }
uint16_t          pp_imu_manager_get_rate_hz(void)  { return s_ctx.rate_hz;  }
```

- [ ] **Step 2: Register the new `.c` in the Makefile**

In `Makefile`, add `firmware/app/src/paddling_pulse_imu_manager.c` and `firmware/app/src/paddling_pulse_imu_manager_logic.c` to `SOURCES` alongside the other `firmware/app/src/paddling_pulse_*.c` entries.

- [ ] **Step 3: Commit**

```bash
git add firmware/app/src/paddling_pulse_imu_manager.c Makefile
git commit -m "feat(imu-manager): state machine + retained-RAM context"
```

(Firmware won't build cleanly yet — `CFG_IMU_DUAL` isn't wired in. The manager `.c` compiles as a standalone translation unit; the dispatcher in task 2.6 wires it into the build.)

---

### Task 2.5: RSSI candidate buffer in the Polar driver

**Files:**
- Create: `firmware/app/include/paddling_pulse_imu_polar_rssi.h`
- Create: `firmware/app/src/paddling_pulse_imu_polar_rssi.c`
- Create: `tests/test_polar_rssi_picker.c`
- Modify: `firmware/app/src/paddling_pulse_imu_polar.c`

The picker logic is extracted into its own file for host testability, same pattern as `paddling_pulse_imu_polar_logic.c`.

- [ ] **Step 1: Write the failing test**

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "paddling_pulse_imu_polar_rssi.h"

static void test_no_candidates_returns_none(void)
{
    pp_polar_rssi_picker_t p;
    pp_polar_rssi_picker_init(&p);
    assert(pp_polar_rssi_picker_count(&p) == 0);
    assert(!pp_polar_rssi_picker_best(&p, NULL));
}

static void test_single_candidate_always_wins(void)
{
    pp_polar_rssi_picker_t p;
    pp_polar_rssi_picker_init(&p);

    uint8_t addr[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    pp_polar_rssi_picker_add(&p, addr, /*addr_type*/ 0, /*rssi*/ -80);

    pp_polar_candidate_t best;
    assert(pp_polar_rssi_picker_best(&p, &best));
    assert(memcmp(best.addr, addr, 6) == 0);
    assert(best.rssi == -80);
}

static void test_highest_rssi_wins(void)
{
    pp_polar_rssi_picker_t p;
    pp_polar_rssi_picker_init(&p);

    uint8_t a[6] = { 1, 1, 1, 1, 1, 1 };
    uint8_t b[6] = { 2, 2, 2, 2, 2, 2 };
    uint8_t c[6] = { 3, 3, 3, 3, 3, 3 };

    pp_polar_rssi_picker_add(&p, a, 0, -85);
    pp_polar_rssi_picker_add(&p, b, 0, -60); /* strongest */
    pp_polar_rssi_picker_add(&p, c, 0, -75);

    pp_polar_candidate_t best;
    assert(pp_polar_rssi_picker_best(&p, &best));
    assert(best.rssi == -60);
    assert(memcmp(best.addr, b, 6) == 0);
}

static void test_duplicate_address_updates_rssi(void)
{
    pp_polar_rssi_picker_t p;
    pp_polar_rssi_picker_init(&p);

    uint8_t a[6] = { 1, 1, 1, 1, 1, 1 };
    pp_polar_rssi_picker_add(&p, a, 0, -90);
    pp_polar_rssi_picker_add(&p, a, 0, -70); /* better reading same strap */

    assert(pp_polar_rssi_picker_count(&p) == 1);
    pp_polar_candidate_t best;
    pp_polar_rssi_picker_best(&p, &best);
    assert(best.rssi == -70);
}

static void test_overflow_drops_new(void)
{
    pp_polar_rssi_picker_t p;
    pp_polar_rssi_picker_init(&p);

    /* Fill exactly MAX with increasing RSSI. */
    for (int i = 0; i < PP_POLAR_MAX_CANDIDATES; ++i)
    {
        uint8_t addr[6] = { (uint8_t)i, 0, 0, 0, 0, 0 };
        pp_polar_rssi_picker_add(&p, addr, 0, (int8_t)(-90 + i));
    }
    assert(pp_polar_rssi_picker_count(&p) == PP_POLAR_MAX_CANDIDATES);

    /* Overflow candidate with a very strong RSSI — spec says drop it. */
    uint8_t overflow[6] = { 0xFE, 0, 0, 0, 0, 0 };
    pp_polar_rssi_picker_add(&p, overflow, 0, -30);

    assert(pp_polar_rssi_picker_count(&p) == PP_POLAR_MAX_CANDIDATES);

    pp_polar_candidate_t best;
    pp_polar_rssi_picker_best(&p, &best);
    assert(best.addr[0] != 0xFE);  /* overflow was dropped */
}

int main(void)
{
    test_no_candidates_returns_none();
    test_single_candidate_always_wins();
    test_highest_rssi_wins();
    test_duplicate_address_updates_rssi();
    test_overflow_drops_new();
    printf("polar rssi picker tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Write the picker header**

```c
#ifndef _PADDLING_PULSE_IMU_POLAR_RSSI_H_
#define _PADDLING_PULSE_IMU_POLAR_RSSI_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef PP_POLAR_MAX_CANDIDATES
#define PP_POLAR_MAX_CANDIDATES   4
#endif

#define PP_POLAR_ADDR_LEN         6

typedef struct
{
    uint8_t addr[PP_POLAR_ADDR_LEN];
    uint8_t addr_type;
    int8_t  rssi;
} pp_polar_candidate_t;

typedef struct
{
    pp_polar_candidate_t items[PP_POLAR_MAX_CANDIDATES];
    uint8_t              count;
} pp_polar_rssi_picker_t;

void    pp_polar_rssi_picker_init(pp_polar_rssi_picker_t *p);
bool    pp_polar_rssi_picker_add(pp_polar_rssi_picker_t *p,
                                 const uint8_t addr[PP_POLAR_ADDR_LEN],
                                 uint8_t addr_type,
                                 int8_t  rssi);
uint8_t pp_polar_rssi_picker_count(const pp_polar_rssi_picker_t *p);
bool    pp_polar_rssi_picker_best(const pp_polar_rssi_picker_t *p,
                                  pp_polar_candidate_t *out);

#endif
```

- [ ] **Step 3: Implement the picker**

```c
#include <string.h>

#include "paddling_pulse_imu_polar_rssi.h"

void pp_polar_rssi_picker_init(pp_polar_rssi_picker_t *p)
{
    if (p == NULL) { return; }
    memset(p, 0, sizeof(*p));
}

bool pp_polar_rssi_picker_add(pp_polar_rssi_picker_t *p,
                              const uint8_t addr[PP_POLAR_ADDR_LEN],
                              uint8_t addr_type,
                              int8_t  rssi)
{
    if (p == NULL || addr == NULL) { return false; }

    /* Update existing entry if address already known. */
    for (uint8_t i = 0; i < p->count; ++i)
    {
        if (memcmp(p->items[i].addr, addr, PP_POLAR_ADDR_LEN) == 0)
        {
            p->items[i].rssi      = rssi;
            p->items[i].addr_type = addr_type;
            return true;
        }
    }

    /* Overflow: drop new candidate (spec decision: worst case is picking
     * a not-strongest-but-still-yours strap, not a stranger's). */
    if (p->count >= PP_POLAR_MAX_CANDIDATES)
    {
        return false;
    }

    memcpy(p->items[p->count].addr, addr, PP_POLAR_ADDR_LEN);
    p->items[p->count].addr_type = addr_type;
    p->items[p->count].rssi      = rssi;
    p->count++;
    return true;
}

uint8_t pp_polar_rssi_picker_count(const pp_polar_rssi_picker_t *p)
{
    return (p == NULL) ? 0u : p->count;
}

bool pp_polar_rssi_picker_best(const pp_polar_rssi_picker_t *p,
                               pp_polar_candidate_t *out)
{
    if (p == NULL || p->count == 0u) { return false; }

    uint8_t best = 0;
    for (uint8_t i = 1; i < p->count; ++i)
    {
        if (p->items[i].rssi > p->items[best].rssi)
        {
            best = i;
        }
    }
    if (out != NULL)
    {
        *out = p->items[best];
    }
    return true;
}
```

- [ ] **Step 4: Build and run the test**

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include \
    tests/test_polar_rssi_picker.c \
    firmware/app/src/paddling_pulse_imu_polar_rssi.c \
    -o build/test_polar_rssi_picker.exe
./build/test_polar_rssi_picker.exe
```
Expected: `polar rssi picker tests passed` and exit 0.

- [ ] **Step 5: Integrate the picker into `paddling_pulse_imu_polar.c`**

Under `#ifdef CFG_IMU_DUAL`, replace the first-match-wins logic in `pp_imu_polar_on_adv_report` (spec notes lines 729–746):

1. Add a static `pp_polar_rssi_picker_t s_picker;` (non-retained `.bss` — spec §RSSI candidate buffer).
2. Add a static `timer_hnd s_rssi_window_timer;` (**non-retained** plain `.bss` — scan-window state is short-lived and the `timer_hnd` is invalidated by sleep anyway; matches the spec's "non-retained" rule for all scan-window state).
3. On `polar_start_scan`, call `pp_polar_rssi_picker_init(&s_picker)` and arm `s_rssi_window_timer = app_easy_timer(PP_POLAR_RSSI_WINDOW_MS, rssi_window_cb);`.
4. Every adv report under `CFG_IMU_DUAL` calls `pp_polar_rssi_picker_add(...)` instead of the current first-match-connect.
5. `rssi_window_cb` calls `pp_polar_rssi_picker_best` and issues the `gapm_start_connection_cmd` with the winner's address. If no candidates, emit `pp_imu_manager_on_event(PP_IMU_EV_POLAR_SCAN_FAIL)`.

Guard the entire candidate-buffer code path with `#ifdef CFG_IMU_DUAL` — single-IMU Polar keeps first-match-wins.

- [ ] **Step 6: Add `.c` to Makefile SOURCES**

Add `firmware/app/src/paddling_pulse_imu_polar_rssi.c` to `SOURCES` in the root `Makefile`.

- [ ] **Step 7: Commit**

```bash
git add firmware/app/include/paddling_pulse_imu_polar_rssi.h \
        firmware/app/src/paddling_pulse_imu_polar_rssi.c \
        firmware/app/src/paddling_pulse_imu_polar.c \
        tests/test_polar_rssi_picker.c \
        Makefile
git commit -m "feat(polar): RSSI-best-wins candidate picker behind CFG_IMU_DUAL"
```

---

### Task 2.6: Wire `CFG_IMU_DUAL` into `paddling_pulse_imu.h` and `paddling_pulse_app.c`

**Files:**
- Modify: `firmware/app/include/paddling_pulse_imu.h`
- Modify: `firmware/app/src/paddling_pulse_app.c`
- Modify: `firmware/config/da14531_config_basic.h`

- [ ] **Step 1: Extend the mutual-exclusion guard**

In `paddling_pulse_imu.h`, change:

```c
#if defined(CFG_IMU_POLAR) + defined(CFG_IMU_MPU6050) + defined(CFG_IMU_LIS3DH) != 1
    #error "Exactly one CFG_IMU_* must be defined"
#endif
```

to:

```c
#if defined(CFG_IMU_POLAR) + defined(CFG_IMU_MPU6050) + \
    defined(CFG_IMU_LIS3DH) + defined(CFG_IMU_DUAL) != 1
    #error "Exactly one CFG_IMU_* must be defined"
#endif
```

- [ ] **Step 2: Route the unified API to the manager under `CFG_IMU_DUAL`**

First, extend the driver-header include ladder at the top of `paddling_pulse_imu.h`:

```c
#if defined(CFG_IMU_LIS3DH)
    #include "paddling_pulse_imu_lis3dh.h"
#elif defined(CFG_IMU_MPU6050)
    #include "paddling_pulse_imu_mpu6050.h"
#elif defined(CFG_IMU_POLAR)
    #include "paddling_pulse_imu_polar.h"
#elif defined(CFG_IMU_DUAL)
    #include "paddling_pulse_imu_manager.h"
    #include "paddling_pulse_imu_lis3dh.h"
    #include "paddling_pulse_imu_polar.h"
#endif
```

Then rewrite **every** `static __inline` dispatcher in full, following the same 4-arm `#if / #elif / #elif / #elif / #endif` ladder. Do not leave any wrapper as a fragment — each one below is shown complete:

```c
static __inline bool pp_imu_init(void)
{
#if defined(CFG_IMU_LIS3DH)
    return pp_imu_lis3dh_init();
#elif defined(CFG_IMU_MPU6050)
    return pp_imu_mpu6050_init();
#elif defined(CFG_IMU_POLAR)
    return pp_imu_polar_init();
#elif defined(CFG_IMU_DUAL)
    pp_imu_manager_init();
    return true;
#endif
}

static __inline void pp_imu_start(void)
{
#if defined(CFG_IMU_LIS3DH)
    pp_imu_lis3dh_start();
#elif defined(CFG_IMU_MPU6050)
    pp_imu_mpu6050_start();
#elif defined(CFG_IMU_POLAR)
    pp_imu_polar_start();
#elif defined(CFG_IMU_DUAL)
    pp_imu_manager_start();
#endif
}

static __inline void pp_imu_stop(void)
{
#if defined(CFG_IMU_LIS3DH)
    pp_imu_lis3dh_stop();
#elif defined(CFG_IMU_MPU6050)
    pp_imu_mpu6050_stop();
#elif defined(CFG_IMU_POLAR)
    pp_imu_polar_stop();
#elif defined(CFG_IMU_DUAL)
    pp_imu_manager_stop();
#endif
}

static __inline void pp_imu_process(void)
{
#if defined(CFG_IMU_LIS3DH)
    pp_imu_lis3dh_process();
#elif defined(CFG_IMU_MPU6050)
    pp_imu_mpu6050_process();
#elif defined(CFG_IMU_POLAR)
    /* Polar data arrives via GATTC_EVENT_IND — nothing to poll */
#elif defined(CFG_IMU_DUAL)
    pp_imu_manager_process();
#endif
}

static __inline bool pp_imu_is_running(void)
{
#if defined(CFG_IMU_LIS3DH)
    return pp_imu_lis3dh_is_running();
#elif defined(CFG_IMU_MPU6050)
    return pp_imu_mpu6050_is_running();
#elif defined(CFG_IMU_POLAR)
    return pp_imu_polar_is_running();
#elif defined(CFG_IMU_DUAL)
    return pp_imu_manager_get_source() != PP_IMU_NONE;
#endif
}

static __inline const char *pp_imu_get_name(void)
{
#if defined(CFG_IMU_LIS3DH)
    return "LIS3DH";
#elif defined(CFG_IMU_MPU6050)
    return "MPU6050";
#elif defined(CFG_IMU_POLAR)
    return "Polar";
#elif defined(CFG_IMU_DUAL)
    switch (pp_imu_manager_get_source())
    {
        case PP_IMU_POLAR:  return "Polar";
        case PP_IMU_LIS3DH: return "LIS3DH";
        default:            return "none";
    }
#endif
}
```

- [ ] **Step 3: Update `pipeline_start` in `paddling_pulse_app.c` for `CFG_IMU_DUAL`**

Add a third branch to the `#if defined(...)` ladder from task 1.4:

```c
#elif defined(CFG_IMU_DUAL)
    /* Manager picks the params and rate after it decides the source.
     * Bootstrap with placeholder values; the first enter_state() call
     * inside pp_imu_manager_start() will immediately re-init correctly. */
    pp_sample_store_init(PP_STROKE_RATE_DEFAULT_LIS3DH_HZ);
    pp_stroke_rate_init(&pp_imu_lis3dh_params);
#endif
```

Then the `pp_imu_init` / `pp_imu_start` calls (which now dispatch to the manager) proceed as in the single-IMU case.

- [ ] **Step 4: Remove the single-IMU Polar rate reconciliation under `CFG_IMU_DUAL`**

The `#ifndef CFG_IMU_DUAL` guard placed in PR 1, task 1.4 step 2 now takes effect: in `CFG_IMU_DUAL` builds, the manager owns `switch_source`, so the Polar driver no longer calls `pp_sample_store_init` directly. Confirm this guard is in place and correctly excludes the dual-mode build.

- [ ] **Step 5: Add the `CFG_IMU_DUAL` option to `da14531_config_basic.h`**

After the existing `CFG_IMU_*` block:

```c
/****************************************************************************************************************/
/* IMU source selection — exactly one must be defined.                                                          */
/*   CFG_IMU_DUAL: both LIS3DH and Polar drivers compiled in; manager selects at runtime.                        */
/****************************************************************************************************************/
// #define CFG_IMU_LIS3DH
// #define CFG_IMU_MPU6050
#define CFG_IMU_POLAR
// #define CFG_IMU_DUAL
```

Leave `CFG_IMU_POLAR` as the default (branch state) so the PR doesn't flip the default config.

- [ ] **Step 6: Build all four configs**

For each of `CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`, `CFG_IMU_MPU6050`, `CFG_IMU_DUAL`:
1. Edit `da14531_config_basic.h` so exactly that one is defined.
2. `mingw32-make clean && mingw32-make` → clean build, zero linker warnings.
3. Record `retention_mem_area0` size from `.map`.

Expected delta for `CFG_IMU_DUAL`: +14 B retained RAM vs. `CFG_IMU_POLAR`, per the spec estimate. If the ceiling is exceeded, stop and investigate.

- [ ] **Step 7: Commit**

```bash
git add firmware/app/include/paddling_pulse_imu.h \
        firmware/app/src/paddling_pulse_app.c \
        firmware/config/da14531_config_basic.h
git commit -m "feat(imu): wire CFG_IMU_DUAL through the unified dispatcher"
```

---

### Task 2.7: Polar driver emits manager events under `CFG_IMU_DUAL`

**Files:**
- Modify: `firmware/app/src/paddling_pulse_imu_polar.c`

Previous work added the RSSI picker (2.5) and the actual-rate accessor (1.3). Now wire events up.

- [ ] **Step 1: Emit `POLAR_STREAMING` when the strap begins sending ACC samples**

Under `#ifdef CFG_IMU_DUAL`, at the point in `paddling_pulse_imu_polar.c` where the driver transitions to streaming (after PMD settings have been latched into `s_polar.acc_sample_rate_hz` and the subscription is active):

```c
#ifdef CFG_IMU_DUAL
    pp_imu_manager_on_event(PP_IMU_EV_POLAR_STREAMING);
#endif
```

- [ ] **Step 2: Emit `POLAR_DISCONNECT` in the existing disconnect callback**

Locate `pp_imu_polar_on_disconnect`. Under `#ifdef CFG_IMU_DUAL`, after the existing ownership check:

```c
#ifdef CFG_IMU_DUAL
    pp_imu_manager_on_event(PP_IMU_EV_POLAR_DISCONNECT);
#endif
```

(The existing `pp_polar_disconnect_is_owned` gate is the right place — only emit if the disconnect is ours.)

- [ ] **Step 3: Emit `POLAR_SCAN_FAIL` when the RSSI window closes with zero candidates**

From 2.5 step 5, the `rssi_window_cb` already had this call. Confirm it's present and wired.

- [ ] **Step 4: Verify no events emitted in single-IMU Polar build**

`grep -n "pp_imu_manager_on_event" firmware/app/src/paddling_pulse_imu_polar.c` — every hit must be inside `#ifdef CFG_IMU_DUAL` … `#endif`. Rebuild `CFG_IMU_POLAR` and confirm it still compiles and link-check shows no reference to `pp_imu_manager_on_event`.

- [ ] **Step 5: Commit**

```bash
git add firmware/app/src/paddling_pulse_imu_polar.c
git commit -m "feat(polar): emit manager events under CFG_IMU_DUAL"
```

---

### Task 2.8: Console `AT+IMU=X` SET and enhanced GET

**Files:**
- Modify: `firmware/app/include/paddling_pulse_console_commands.h`
- Modify: `firmware/app/src/paddling_pulse_console_commands.c`
- Modify: `firmware/app/src/paddling_pulse_console.c`
- Modify: `tests/test_console_commands.c`

- [ ] **Step 1: Extend the host test first — write the failing cases**

Add to `tests/test_console_commands.c`:

```c
static void test_at_imu_set_auto_parsed(void)
{
    pp_console_cmd_t cmd;
    assert(pp_console_parse("AT+IMU=AUTO", &cmd));
    assert(cmd.type == PP_CONSOLE_CMD_IMU_SET);
    assert(cmd.imu_target == PP_IMU_OVERRIDE_AUTO);
}

static void test_at_imu_set_lis3dh_parsed(void)
{
    pp_console_cmd_t cmd;
    assert(pp_console_parse("AT+IMU=LIS3DH", &cmd));
    assert(cmd.imu_target == PP_IMU_OVERRIDE_LIS3DH);
}

static void test_at_imu_set_polar_parsed(void)
{
    pp_console_cmd_t cmd;
    assert(pp_console_parse("AT+IMU=POLAR", &cmd));
    assert(cmd.imu_target == PP_IMU_OVERRIDE_POLAR);
}

static void test_at_imu_set_case_insensitive(void)
{
    pp_console_cmd_t cmd;
    assert(pp_console_parse("AT+IMU=auto", &cmd));
    assert(cmd.imu_target == PP_IMU_OVERRIDE_AUTO);
    assert(pp_console_parse("AT+IMU=Polar", &cmd));
    assert(cmd.imu_target == PP_IMU_OVERRIDE_POLAR);
}

static void test_at_imu_set_bogus_rejected(void)
{
    pp_console_cmd_t cmd;
    assert(!pp_console_parse("AT+IMU=BOGUS", &cmd));
    assert(!pp_console_parse("AT+IMU=", &cmd));
    assert(!pp_console_parse("AT+IMU=123", &cmd));
}

/* AT+IMU without '=' is the GET form — must still parse. */
static void test_at_imu_get_still_parses(void)
{
    pp_console_cmd_t cmd;
    assert(pp_console_parse("AT+IMU", &cmd));
    assert(cmd.type == PP_CONSOLE_CMD_IMU_GET);
}
```

Register each new test in `main()`.

- [ ] **Step 2: Build the test — confirm it fails on `PP_CONSOLE_CMD_IMU_SET`**

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include \
    tests/test_console_commands.c \
    firmware/app/src/paddling_pulse_console_commands.c \
    -o build/test_console_commands.exe
```

Expected: compile error — `PP_CONSOLE_CMD_IMU_SET`, `imu_target`, `PP_IMU_OVERRIDE_*` not declared. This is the failing test.

- [ ] **Step 3: Extend `pp_console_cmd_t` and the enum**

In `paddling_pulse_console_commands.h`:

```c
#include "paddling_pulse_imu_manager.h"  /* for pp_imu_override_t */

typedef enum
{
    /* ...existing commands... */
    PP_CONSOLE_CMD_IMU_GET,
    PP_CONSOLE_CMD_IMU_SET,
} pp_console_cmd_type_t;

typedef struct
{
    pp_console_cmd_type_t type;
    union
    {
        pp_imu_override_t imu_target;
        /* ...existing payloads... */
    };
} pp_console_cmd_t;
```

- [ ] **Step 4: Implement the parser**

In `paddling_pulse_console_commands.c`, extend the existing `pp_console_parse`:

```c
static const char *const IMU_TARGET_AUTO   = "AUTO";
static const char *const IMU_TARGET_LIS3DH = "LIS3DH";
static const char *const IMU_TARGET_POLAR  = "POLAR";

static bool parse_imu_target(const char *arg, size_t len, pp_imu_override_t *out)
{
    if (pp_console_token_equals_ci(arg, len, IMU_TARGET_AUTO,   4))
    { *out = PP_IMU_OVERRIDE_AUTO;   return true; }
    if (pp_console_token_equals_ci(arg, len, IMU_TARGET_LIS3DH, 6))
    { *out = PP_IMU_OVERRIDE_LIS3DH; return true; }
    if (pp_console_token_equals_ci(arg, len, IMU_TARGET_POLAR,  5))
    { *out = PP_IMU_OVERRIDE_POLAR;  return true; }
    return false;
}
```

Wire it into the top-level parser so `AT+IMU` with no `=` is `IMU_GET` and `AT+IMU=<arg>` is `IMU_SET`. If `pp_console_token_equals_ci` doesn't exist yet, add it alongside `pp_console_token_equals` (same length-bounded pattern, `tolower()`-folding).

- [ ] **Step 5: Run the test, confirm it now passes**

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include \
    tests/test_console_commands.c \
    firmware/app/src/paddling_pulse_console_commands.c \
    -o build/test_console_commands.exe
./build/test_console_commands.exe
```
Expected: all tests pass, exit 0.

- [ ] **Step 6: Dispatch from `paddling_pulse_console.c`**

Route `IMU_SET` to `pp_imu_manager_set_override(cmd.imu_target)` — guard the call with `#ifdef CFG_IMU_DUAL`. On single-IMU builds, reply `ERROR` (no manager to target).

Enhance the `IMU_GET` handler to print the spec-required format:

```c
paddling_pulse_console_printf(
    "+IMU: override=%s,source=%s,state=%s,rate=%u\r\n",
    override_name(pp_imu_manager_get_override()),
    source_name(pp_imu_manager_get_source()),
    state_name(pp_imu_manager_get_state()),
    pp_imu_manager_get_rate_hz());
```

The `*_name` helpers are static `const char *` lookup functions — no magic strings inline. Under single-IMU builds, fall back to the pre-existing `AT+IMU` output (show the compile-time name via `pp_imu_get_name()`).

- [ ] **Step 7: Commit**

```bash
git add firmware/app/include/paddling_pulse_console_commands.h \
        firmware/app/src/paddling_pulse_console_commands.c \
        firmware/app/src/paddling_pulse_console.c \
        tests/test_console_commands.c
git commit -m "feat(console): AT+IMU=AUTO|LIS3DH|POLAR SET and enhanced GET"
```

---

### Task 2.9: Extend `test_imu_manager.c` with switch-sequence ordering test

The logic helpers already cover state transitions (task 2.3). This task adds coverage for the ordering invariant in `switch_source`, using a test shim for the driver calls.

**Files:**
- Modify: `tests/test_imu_manager.c`

- [ ] **Step 1: Decide the scope**

Testing `switch_source` ordering requires either linking the full manager `.c` (which pulls in SDK headers — not host-buildable) or extracting the ordering into a host-testable helper. Use the helper approach.

Add to `paddling_pulse_imu_manager_logic.h`:

```c
typedef enum
{
    PP_IMU_LIFECYCLE_STOP_OLD          = 0,
    PP_IMU_LIFECYCLE_SAMPLE_STORE_INIT = 1,
    PP_IMU_LIFECYCLE_STROKE_RATE_INIT  = 2,
    PP_IMU_LIFECYCLE_START_NEW         = 3,
    PP_IMU_LIFECYCLE_STEPS             = 4,
} pp_imu_lifecycle_step_t;

/* Writes PP_IMU_LIFECYCLE_STEPS entries in call order. Test-only helper,
 * used by the host tests to prove the switch sequence follows the spec. */
void pp_imu_manager_logic_fill_switch_sequence(
    pp_imu_lifecycle_step_t out[PP_IMU_LIFECYCLE_STEPS]);
```

Implementation is trivial — it returns the known-correct sequence. The real benefit is that the manager's `switch_source` is reviewed against this canonical order (reviewer reads both side-by-side).

Add to the logic `.c`:

```c
void pp_imu_manager_logic_fill_switch_sequence(
    pp_imu_lifecycle_step_t out[PP_IMU_LIFECYCLE_STEPS])
{
    out[0] = PP_IMU_LIFECYCLE_STOP_OLD;
    out[1] = PP_IMU_LIFECYCLE_SAMPLE_STORE_INIT;
    out[2] = PP_IMU_LIFECYCLE_STROKE_RATE_INIT;
    out[3] = PP_IMU_LIFECYCLE_START_NEW;
}
```

- [ ] **Step 2: Add the test**

```c
static void test_switch_sequence_stop_before_store_init(void)
{
    pp_imu_lifecycle_step_t seq[PP_IMU_LIFECYCLE_STEPS];
    pp_imu_manager_logic_fill_switch_sequence(seq);

    assert(seq[0] == PP_IMU_LIFECYCLE_STOP_OLD);
    assert(seq[1] == PP_IMU_LIFECYCLE_SAMPLE_STORE_INIT);
    assert(seq[2] == PP_IMU_LIFECYCLE_STROKE_RATE_INIT);
    assert(seq[3] == PP_IMU_LIFECYCLE_START_NEW);
}
```

Wire into `main()`.

- [ ] **Step 3: Run all manager tests**

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include \
    tests/test_imu_manager.c \
    firmware/app/src/paddling_pulse_imu_manager_logic.c \
    -o build/test_imu_manager.exe
./build/test_imu_manager.exe
```
Expected: all pass, exit 0.

- [ ] **Step 4: Commit**

```bash
git add tests/test_imu_manager.c \
        firmware/app/include/paddling_pulse_imu_manager_logic.h \
        firmware/app/src/paddling_pulse_imu_manager_logic.c
git commit -m "test(imu-manager): lock in stop-before-store-init switch ordering"
```

---

### Task 2.10: PR 2 verification gate

- [ ] **Step 1: Run the full four-config build matrix**

For each of `CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`, `CFG_IMU_MPU6050`, `CFG_IMU_DUAL`:
1. Edit `firmware/config/da14531_config_basic.h` so exactly that one is defined.
2. `mingw32-make clean && mingw32-make`
3. Confirm clean build, zero linker warnings, zero `section overlap`, zero `region RAM overflowed`.
4. Capture `retention_mem_area0` size and `.text + .rodata` size from `build/.tmp/*.map`.

Restore `CFG_IMU_POLAR` as the default before committing.

- [ ] **Step 2: Run every host test**

```
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_polar_logic.c firmware/app/src/paddling_pulse_imu_polar_logic.c -o build/test_polar_logic.exe && ./build/test_polar_logic.exe
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_console_commands.c firmware/app/src/paddling_pulse_console_commands.c -o build/test_console_commands.exe && ./build/test_console_commands.exe
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_stroke_rate_params.c firmware/app/src/paddling_pulse_stroke_rate.c firmware/app/src/paddling_pulse_sample_store.c -o build/test_stroke_rate_params.exe && ./build/test_stroke_rate_params.exe
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_polar_rssi_picker.c firmware/app/src/paddling_pulse_imu_polar_rssi.c -o build/test_polar_rssi_picker.exe && ./build/test_polar_rssi_picker.exe
gcc -std=c99 -Wall -Wextra -I firmware/app/include tests/test_imu_manager.c firmware/app/src/paddling_pulse_imu_manager_logic.c -o build/test_imu_manager.exe && ./build/test_imu_manager.exe
```
Expected: every binary exits 0 with no assertion failures.

- [ ] **Step 3: Style-rule guard — grep new `.c` files for bare integer literals**

From `config/local/rules.md` via spec §Code Style. For every new or heavily-modified `.c` file in this PR, scan for bare integer literals used in expressions (other than `0`, array indices into fixed-size compile-time bounds, and loop-counter bounds that come from `sizeof` / named constants):

```
grep -nE '[^A-Za-z0-9_.][0-9]+[^A-Za-z0-9_.]' \
    firmware/app/src/paddling_pulse_imu_manager.c \
    firmware/app/src/paddling_pulse_imu_manager_logic.c \
    firmware/app/src/paddling_pulse_imu_polar_rssi.c
```

Every hit must be either (a) `0` / `1` in a loop or init context, or (b) inside a comment. Any other literal must be a named `#define` or `enum`. Fix violations by introducing a `PP_<MODULE>_<MEANING>` constant.

Also grep the diff for `Claude` / `Anthropic` / `generated by` / `co-author` to confirm no AI attribution slipped in:

```
git log origin/main..HEAD --format='%B' | grep -iE 'claude|anthropic|generated by|co-author' || echo "clean"
```

Expected: `clean`.

- [ ] **Step 4: Walk through the six memory verification gates from the spec**

Spec §Verification gate (lines 305–328):
1. `retention_mem_area0` stays below the ceiling — numeric comparison from the map file.
2. Every new retained static carries `__SECTION_ZERO("retention_mem_area0")`; candidate buffer does **not**. Grep the diff.
3. Zero linker warnings in all four configs.
4. `.text + .rodata` fits.
5. No new function has a larger locals frame than the existing largest function in its driver (check objdump / map).
6. Four-config build matrix passes.

- [ ] **Step 5: Walk through PR 2 acceptance criteria from the spec**

Spec §Acceptance criteria for PR 2 (lines 594–651). Each checkbox ticks from diff, build logs, or hardware observations.

- [ ] **Step 6: Hardware integration (manual)**

Spec §Testing Strategy / Hardware integration (lines 479–498). Flash `CFG_IMU_DUAL` build to the board; run through steps 1–7. Paste observed console output into the PR description for each step. Step 7 (two-strap) may be marked "DEFERRED — single-strap environment" if only one Polar is available.

- [ ] **Step 7: Populate the PR 2 template and open the PR**

Spec §PR Description Template (lines 659–687). No Claude co-author. No generated-by references.

---

## Done criteria (applies to the whole feature)

- All four configs (`CFG_IMU_LIS3DH`, `CFG_IMU_POLAR`, `CFG_IMU_MPU6050`, `CFG_IMU_DUAL`) build clean via `mingw32-make`, zero linker warnings.
- All five host test binaries exit 0.
- `retention_mem_area0` fits with recorded headroom; `.text + .rodata` fits with recorded headroom.
- Hardware integration flows 1–6 reproduce; flow 7 either reproduced or explicitly deferred.
- Two PRs merged: one pure refactor, one dual-IMU runtime, both passing `/ultrareview`.
