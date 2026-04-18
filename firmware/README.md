# Firmware

Ce répertoire contient les sources firmware et la configuration DA14531 utilisées par `PaddlingPulse`.

## Structure

- `app/src/` : sources de l'application
- `app/include/` : en-têtes de l'application
- `config/` : configuration firmware DA14531 et fichier scatter

## Toolchain

La compilation utilise :

- Renesas `DA145xx_SDK`
- Arm Compiler 6 (`armclang`, `armlink`, `fromelf`)
- `make` sur Linux/macOS ou `mingw32-make` sur Windows

Le SDK n'est pas stocké dans ce dépôt. Il est référencé via `SDK_ROOT`.

## Configuration locale

Créer `config/local.mk` à la racine du dépôt avec :

```make
SDK_ROOT=/path/to/DA145xx_SDK
AC6_BIN=/path/to/armclang/bin
```

`AC6_BIN` est optionnel si les outils Arm sont déjà dans le `PATH`.

## Compilation

Depuis la racine du dépôt :

- Windows: `mingw32-make build`
- Linux/macOS: `make build`
- Nettoyage : `make clean` ou `mingw32-make clean`

Sortie finale :

- `build/PaddlingPulse.hex`

Les fichiers intermédiaires restent dans :

- `build/.tmp/`

## Mode console

Le firmware dispose d'un mode console activé à la compilation dans :

- `config/da14531_config_basic.h`

Quand `CFG_PADDLING_PULSE_CONSOLE_MODE` est activé, la compilation active :

- l'UART single-wire sur `UART1`
- le support local des commandes AT

Broche UART single-wire actuelle :

- `P0_5` à 115200 de baud rate.


## Compile-Time Configuration

### IMU Source (`da14531_config_basic.h`)
| Define | Sensor | Interface | Rate |
|--------|--------|-----------|------|
| `CFG_IMU_LIS3DH` | LIS3DH | I2C P0_8/P0_9 | 100 Hz |
| `CFG_IMU_MPU6050` | MPU6050 | I2C P0_8/P0_9 | 100 Hz |
| `CFG_IMU_POLAR` | Polar Verity Sense | BLE central | 52 Hz |

### Axis Selection (`da14531_config_basic.h`)
`CFG_IMU_AXIS_X`, `CFG_IMU_AXIS_Y`, or `CFG_IMU_AXIS_Z`

### New Source Files
| File | Purpose |
|------|---------|
| `paddling_pulse_sample_store.c/h` | 512-sample circular buffer |
| `paddling_pulse_stroke_rate.c/h` | Autocorrelation + Kalman filter |
| `paddling_pulse_imu.h` | Compile-time driver dispatch |
| `paddling_pulse_imu_lis3dh.c/h` | LIS3DH I2C driver |
| `paddling_pulse_imu_mpu6050.c/h` | MPU6050 I2C driver |
| `paddling_pulse_imu_polar.c/h` | Polar BLE PMD client |

### AT Console Commands (`CFG_PADDLING_PULSE_CONSOLE_MODE`)
| Command | Description |
|---------|-------------|
| `AT+CAD` | Query current cadence RPM |
| `AT+CAD=<n>` | Set manual cadence override (0-255) |
| `AT+IMU` | Query IMU status |
| `AT+BATT` | Query battery level |
| `AT+IOCFG` | Query GPIO pin assignments |

### Stroke Rate Tuning (`paddling_pulse_stroke_rate.h`)
All parameters are `#ifndef`-guarded and can be overridden in
`da14531_config_basic.h`. See the header file for the full list and
default values.

## Périmètre

- cible : `DA14531`
- type de build : release
- artefact firmware principal : `.hex` uniquement

