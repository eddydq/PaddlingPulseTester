# Polar Backport Design

## Goal

Backport the Polar Verity Sense central-role IMU path and the later stability
fixes onto `feat/stroke-rate-pipeline-app-layer`, while keeping the existing
pre-blocks stroke-rate flow:

`Polar/LIS3DH/MPU6050 IMU -> sample store -> autocorrelation -> Kalman -> CSCP`

The result should preserve the current application-layer stroke-rate design and
avoid introducing the later graph executor, block registry, binary pipeline
protocol, OTA pipeline service, or analysis-platform work.

## In Scope

- Enable the Polar feature set expected after `1625d9d`:
  - Dual-role BLE config required for Polar central mode
  - Polar scan/connect/discovery/ACC streaming path
  - Console commands `AT+CAD` and `AT+IMU`
  - UART/debug behavior needed for single-wire console operation
- Backport the later Polar fixes that make the central path stable:
  - prevent Polar disconnect handling from interfering with phone/peripheral
    disconnects
  - restore correct app state and connection startup parameters
  - handle `GAPM_CANCEL` completion during timeout/stop flows
  - align DA14531 config for dual-link operation
  - complete PMD discovery and accelerometer start flow
- Keep the existing stroke-rate application integration from `1625d9d`

## Out of Scope

- Graph executor / topological execution
- Block manifests / block registry / params schema
- OTA pipeline service and retained binary pipeline storage
- WASM or browser-side pipeline work
- Analysis, mixed-pipeline, and algorithm research commits
- Polar logger scripts

## Source Commits

The backport is based on these later commits on `feature/cscp-service`:

- `28b3114` `feat: conditional BLE dual-role and callbacks for Polar`
- `fc122ea` `feat: add AT+CAD and AT+IMU console commands`
- `2999c33` `fix(uart): restore BLE and Polar debug output on single-wire console`
- `d3588d6` `fix(polar): stop polar client from hijacking phone disconnects`
- `25e5cc4` `fix(polar): restore connection startup parameters and app state`
- `a6badf9` `fix(polar): handle GAPM_CANCEL completion for timeout and stop paths`
- `2aa2ac2` `fix(polar): align DA14531 config for dual-link operation`
- `0bcca6a` `fix(polar): complete PMD discovery and ACC start flow`

Existing functionality already present on this branch and retained as the base:

- `7157e95` Polar BLE central PMD client driver
- `1625d9d` application-layer stroke-rate integration

## Approach

Use a selective backport rather than replaying the entire later history.

1. Apply the Polar feature/config changes that are conceptually independent of
   the later graph/block work.
2. Port the Polar fix chain in dependency order, resolving any differences
   against the pre-blocks application layout.
3. Preserve the current `paddling_pulse_app.c` behavior where stroke-rate is
   still computed by `pp_stroke_rate_update()` rather than any graph executor.
4. Only touch files that directly support Polar operation, console behavior, or
   required BLE/config integration.

## Expected File Areas

- `firmware/app/src/paddling_pulse_app.c`
- `firmware/app/src/paddling_pulse_imu_polar.c`
- `firmware/app/include/paddling_pulse_imu_polar.h`
- `firmware/app/src/paddling_pulse_console.c`
- `firmware/app/src/paddling_pulse_console_io.c`
- `firmware/app/include/paddling_pulse_console_io.h`
- `firmware/config/user_config.h`
- `firmware/config/user_callback_config.h`
- `firmware/config/da14531_config_basic.h`
- `firmware/config/da14531_config_advanced.h`
- `Makefile`

## Design Details

### BLE Role and Callback Wiring

Polar support requires the DA14531 build to run in dual-role mode when
`CFG_IMU_POLAR` is enabled. The backport will carry over the config guards that
switch the device from peripheral-only behavior to the central/peripheral setup
used by the later Polar commits. Callback registration for scan reports and scan
completion will also be restored under the same compile-time guard.

Peripheral-only builds must remain unchanged when `CFG_IMU_POLAR` is not set.

### App-Level Connection Ownership

The app currently supports both the phone connection used for CSCP and the
separate Polar central connection. The backport will preserve the split
ownership model:

- phone/peripheral connection remains owned by `paddling_pulse_app.c`
- Polar central connection remains owned by `paddling_pulse_imu_polar.c`

The later fixes around disconnect routing and app-state restoration will be
ported so the Polar client does not consume or corrupt the phone connection
lifecycle.

### Polar PMD Discovery and ACC Start

The current branch already contains the early PMD client. The later fix set
shows that discovery/startup was incomplete. The backport will align the state
machine, discovery completion handling, CCCD enablement, PMD control-point
handling, and accelerometer start sequence with the later stable version, while
leaving the final consumer as the sample-store based stroke-rate path.

### Console Integration

`AT+CAD` and `AT+IMU` will be added because they are directly useful for the
pre-blocks firmware and do not depend on the later pipeline work.

The UART fix will also be included so BLE and Polar debug output continue to
work in the single-wire console setup used by this firmware line.

## Testing Strategy

Verification will focus on build- and behavior-level confidence appropriate to
this branch:

- targeted compile/build validation for the affected firmware sources
- if available in this branch, existing host-side tests for stroke-rate logic
- manual code-path review for:
  - Polar connection setup and disconnect handling
  - PMD discovery -> notification enable -> ACC start flow
  - app phone connection remaining intact
  - console command parsing for `AT+CAD` and `AT+IMU`

Where an automated regression test is feasible on this branch, add it before
the corresponding fix. If a behavior is tightly coupled to DA14531 SDK message
handling and cannot be tested cheaply in host tests, document that limit and
verify via targeted build evidence.

## Risks and Mitigations

- Later Polar fixes may assume surrounding config changes not present here.
  Mitigation: port in dependency order and diff against the stable later files
  before editing.
- The app already has compile-time branching for multiple IMU backends.
  Mitigation: keep all new behavior behind `CFG_IMU_POLAR`.
- UART/debug changes may affect non-Polar console output.
  Mitigation: preserve existing console behavior for non-Polar builds and limit
  changes to the later single-wire fix.

## Success Criteria

- `CFG_IMU_POLAR` builds include the later dual-role/config wiring
- Polar connection and PMD startup logic matches the later stable behavior
- Phone disconnect handling remains owned by the app and is not hijacked by the
  Polar client
- `AT+CAD` and `AT+IMU` are available on this branch
- No graph/block/OTA pipeline code is introduced
