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


## Commandes AT actuelles

La console supporte actuellement :

- `AT+BATT`
- `AT+IOCFG`

`AT+IOCFG` est en lecture seule pour le moment et retourne l'affectation fixe des broches de la carte.

## Périmètre

- cible : `DA14531`
- type de build : release
- artefact firmware principal : `.hex` uniquement

