# Ricardo - IHM Jeu de la roue

Application IHM de jeu de boisson basee sur LVGL v9.

- Resolution cible: 1024x600
- Persistance: SQLite3 (`game.db`)
- Cibles: simulateur PC et Raspberry Pi (native ou cross-compilation)

## Fonctionnalites

| Ecran | Role |
|---|---|
| Accueil | Boutons Jouer / Classement / Parametres |
| Profil | Selection de l'utilisateur actif |
| Roue principale | Tirage 0/2/4/6 cL avec impact bonus/malus |
| Roue bonus | Bonus / Malus / +Temps / -Temps / Rien |
| Donner modificateur | Attribution du bonus/malus gagne |
| Classement | Joueur / Total cL / Bonus / Malus / A donne a |
| Parametres | Acces PIN (2106), configuration complete + calibration pompes |

## Mecanique de jeu

- Roue principale: base 0 / 2 / 4 / 6 cL, puis decalage selon bonus/malus actifs.
- Roue bonus: probabilites configurables pour chaque segment.
- Bonus/malus: empilement jusqu'au max configure.
- Timeout: +Temps / -Temps ajoutent ou retirent un cooldown en minutes.
- Cooldown joueur: delai configurable entre deux spins.

## Parametres configurables

| Cle | Defaut | Description |
|---|---|---|
| `wheel_trigger_chance` | 20 | Probabilite de declencher la roue bonus (en %) |
| `bonus_wheel_bonus_weight` | 1 | Poids du segment Bonus |
| `bonus_wheel_nothing_weight` | 2 | Poids du segment Rien |
| `bonus_wheel_malus_weight` | 1 | Poids du segment Malus |
| `bonus_wheel_timeout_add_weight` | 1 | Poids du segment +Temps |
| `bonus_wheel_timeout_remove_weight` | 1 | Poids du segment -Temps |
| `timeout_modifier_minutes` | 5 | Duree d'un modificateur Temps (minutes) |
| `max_bonus_stack` | 5 | Empilement max de bonus |
| `max_malus_stack` | 5 | Empilement max de malus |
| `spin_cooldown_seconds` | 0 | Cooldown entre deux spins (secondes) |
| `pump1_pulses_per_cl_x1000` | 540420 | Constante pompe 1 (pulses/cL) en fixe x1000 |
| `pump2_flow_clps_x1000` | 2800 | Constante pompe 2 (cL/s) en fixe x1000 |

## Calibration pompes

Un onglet Calibration est disponible dans Parametres.

Workflow operateur:

1. Selectionner la pompe 1 ou 2.
2. Entrer la quantite cible en cL.
3. Appuyer sur Demarrer.
4. Quand la quantite est atteinte dans le verre, retirer le verre du capteur de presence.
5. Le firmware stoppe la pompe, calcule la constante, puis renvoie la valeur finale.
6. L'IHM enregistre la constante en base et tente de la synchroniser immediatement vers le controleur.

Comportement Reset:

- Le bouton reset de calibration remet uniquement la pompe selectionnee a sa valeur par defaut.
- Les autres reglages restent inchanges.

Formules appliquees:

- Pompe 1: `pulsesPerCl = pulseCount / quantite_cL`
- Pompe 2: `flowClPerSec = quantite_cL / duree_secondes`

Source de verite:

- La base SQLite de l'IHM est la source de verite des constantes.
- Au demarrage, l'IHM envoie les constantes au microcontroleur via `SETFLOW`.
- Le microcontroleur applique ces valeurs en runtime (pas de persistance EEPROM dans cette version).

## Protocole serie: extensions calibration

Commandes IHM vers controleur:

- `CAL:<pump_index>:<quantity_cL>`
- `SETFLOW:<p1_pulses_per_cL>:<p2_cL_per_s>`
- `STOP` (interrompt aussi une calibration en cours)

Reponses controleur vers IHM:

- `CAL:STARTED:<pump_index>:<quantity_cL>`
- `PROGRESS:<p1_mesure_ou_calcule>:<p2_estime_ou_calcule>`
- `CAL:STOPPED_BY_SENSOR:<p1_pulses_per_cL>:<p2_cL_per_s>`
- `CAL:STOPPED:<p1_pulses_per_cL>:<p2_cL_per_s>`
- `SETFLOW:OK:<p1_pulses_per_cL>:<p2_cL_per_s>`
- `ERROR:INVALID_CALIBRATION`, `ERROR:INVALID_CONSTANTS`, `ERROR:NO_OBJECT`, etc.

## Architecture

Le code IHM principal est dans `ihm/ui`.

- `ihm/ui/screens`: ecrans
- `ihm/ui/widgets`: widgets reutilisables
- `ihm/ui/components`: logique metier + persistence SQLite
- `ihm/ui/src`: entree application et orchestration
- `ihm/ui/third_party/lvgl`: LVGL v9 vendore
- `ihm/ui/third_party/lv_drivers`: conserve dans le depot, non lie pour la cible Pi

Important: sur Raspberry Pi, le build utilise les backends Linux integres de LVGL v9 (`fbdev` / `drm` / `evdev`), pas `lv_drivers`.

## Prerequis

- CMake >= 3.15
- Compilateur C/C++
- `pkg-config`
- `sqlite3` + fichiers de dev (`sqlite3.pc`)
- Selon backend Linux:
  - FBDEV: acces a `/dev/fb0` et `/dev/input/eventX`
  - DRM: `libdrm` + acces a `/dev/dri/card0`

## Build et execution

### 1) Simulateur PC (workflow historique)

```bash
cd lv_port_pc_vscode/build
cmake --build . -j$(nproc)
./bin/main
```

### 2) IHM standalone native Linux (dans `ihm/ui`)

```bash
cd ihm/ui
cmake -S . -B build \
  -DIHM_USE_LINUX_FBDEV=ON \
  -DIHM_USE_LINUX_DRM=OFF \
  -DIHM_SCREEN_WIDTH=1024 \
  -DIHM_SCREEN_HEIGHT=600 \
  -DIHM_FBDEV_DEVICE=/dev/fb0 \
  -DIHM_EVDEV_DEVICE=/dev/input/event0
cmake --build build -j$(nproc)
./build/ihm.app
```

### 3) Cross-compilation Raspberry Pi (AArch64)

Monter d'abord le sysroot (sans `sudo`):

```bash
mkdir -p /home/vvicier/sdk/pi-sysroot
sshfs ricardo@192.168.1.2:/ /home/vvicier/sdk/pi-sysroot \
  -o ro,reconnect,follow_symlinks,idmap=user,uid=$(id -u),gid=$(id -g)
```

Puis configurer et compiler:

```bash
cd ihm/ui
cmake -S . -B build-rpi-mount \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/raspberrypi3-linux-aarch64.cmake \
  -DCMAKE_SYSROOT=/home/vvicier/sdk/pi-sysroot \
  -DIHM_USE_LINUX_FBDEV=ON \
  -DIHM_USE_LINUX_DRM=OFF \
  -DIHM_SCREEN_WIDTH=1024 \
  -DIHM_SCREEN_HEIGHT=600 \
  -DIHM_EVDEV_DEVICE=/dev/input/event2
cmake --build build-rpi-mount -j$(nproc)
```

Sortie binaire cross-compilee:

- `ihm/ui/build-rpi-mount/ihm.app`

Note architecture:

- Image Raspberry Pi OS 64 bits: `cmake/toolchains/raspberrypi3-linux-aarch64.cmake`
- Image 32 bits: utiliser le toolchain `gnueabihf` avec un sysroot `armhf` correspondant

## Deployment et service

- Le service systemd reference dans ce repo est `ihm-app.service`.
- Il attend un binaire sur la cible a l'emplacement `/home/ricardo/ihm/ihm.app`.
- Le build CMake produit `ihm.app` dans le dossier de build (`build` ou `build-rpi-mount`).

Si besoin d'installation via CMake:

```bash
cd ihm/ui
cmake --install build-rpi-mount --prefix /home/ricardo/ihm
```

Le binaire sera alors installe dans `/home/ricardo/ihm/bin/ihm.app` (adapter le service ou copier/renommer selon votre convention).

## Base de donnees

- `game.db` est cree automatiquement au premier lancement.
- Script de generation de donnees de test: `lv_port_pc_vscode/gen_mock_db.sh`

## Depannage rapide

- Si vous lancez `clean-third-party`, relancez `cmake -S . -B <build-dir> ...` avant `cmake --build`.
- Si le build cross echoue avec GLIBC_2.38, verifier que:
  - le sysroot est monte en utilisateur normal,
  - le toolchain AArch64 du repo est bien utilise,
  - vous avez fait un rebuild propre de `build-rpi-mount` et `.third_party_build/lvgl`.
