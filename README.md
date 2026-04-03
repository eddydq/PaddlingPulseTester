# PaddlingPulse

Un projet basé sur le **Renesas DA14531MOD** combinant **PCB custom**, **firmware embarqué** et **CAD**.

## Disclaimer

Ce projet a été développé avec l’aide d’outils d’IA. Certaines parties du firmware ainsi que certains éléments de référence s’appuient également sur des exemples fournis par Renesas, puis adaptés aux besoins spécifiques du projet.

## Introduction

Dans le sport moderne, la performance des athlètes repose de plus en plus sur l’analyse de données afin d’optimiser l’entraînement. C’est dans cette optique qu’est né ce projet : mesurer en temps réel la cadence dans les sports tels que le canoë, le kayak, l’aviron, le bateau-dragon, le SUP et autres.

## Objectifs

L’objectif du projet est de concevoir un capteur capable de mesurer la cadence, tout en restant **compact**, **autonome** et **peu coûteux**.

Le concept a d’abord été envisagé autour d’un **ESP32**, mais sa consommation énergétique s’est révélée trop importante, ce qui imposait l’utilisation d’une batterie plus volumineuse. Cette contrainte réduisait fortement la compacité du système. Le choix du **DA14531MOD** s’est donc imposé pour mieux répondre aux exigences de faible consommation et d’intégration.

## Spécifications

- **MCU / Module :** Renesas DA14531MOD
- **Communication :** Bluetooth Low Energy (BLE)
- **Langage de programmation :** C

## Features

### CSCP Service
BLE Cycling Speed & Cadence Profile (0x1816) exposing real-time paddle
stroke rate as crank cadence. Compatible with any CSCP-capable cycling
computer or app (Wahoo, Garmin, Zwift, etc.).

### IMU Sources (compile-time selection)
| Define | Sensor | Interface | Sample Rate |
|--------|--------|-----------|-------------|
| `CFG_IMU_LIS3DH` | STMicro LIS3DH | I2C 400 kHz | 100 Hz |
| `CFG_IMU_MPU6050` | InvenSense MPU6050 | I2C 400 kHz | 100 Hz |
| `CFG_IMU_POLAR` | Polar Verity Sense | BLE central | 52 Hz |

### Axis Selection
`CFG_IMU_AXIS_X`, `CFG_IMU_AXIS_Y`, or `CFG_IMU_AXIS_Z` — selects
which accelerometer axis is used for stroke rate detection.

### Stroke Rate Algorithm
Autocorrelation-based period estimation with:
- Harmonic rejection (prevents half-rate lockout)
- Sub-sample parabolic interpolation
- Kalman filter with cold-start confirmation
- All math in Q16.16 fixed-point (no FPU on Cortex-M0+)

### Console Mode
`CFG_PADDLING_PULSE_CONSOLE_MODE` enables single-wire UART AT commands:
- `AT+CAD` — query/set cadence RPM
- `AT+IMU` — query IMU status
- `AT+BATT` — query battery level
