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
| Selection verre | Choix du volume de verre (22 / 33 / 50 cL) |
| Selection cocktail | Choix d'une recette (affiche seulement si `pump_count > 2`) |
| Roue principale | Tirage 0/2/4/6 cL avec impact bonus/malus |
| Roue bonus | Bonus / Malus / +Temps / -Temps / Rien |
| Donner modificateur | Attribution du bonus/malus gagne |
| Classement | Joueur / Total cL / Bonus / Malus / A donne a |
| Parametres | Acces PIN (2106), Jeu + Joueurs + Purge + Calibration + Cocktails |

## Mecanique de jeu

- Roue principale: base 0 / 2 / 4 / 6 cL, puis decalage selon bonus/malus actifs.
- Roue bonus: probabilites configurables pour chaque segment.
- Bonus/malus: empilement jusqu'au max configure.
- Timeout: +Temps / -Temps ajoutent ou retirent un cooldown en minutes.
- Cooldown joueur: delai configurable entre deux spins.
- Routage de distribution:
  - si `pump_count == 2`: comportement historique, P1 prend la valeur de roue, P2 complete le verre.
  - si `pump_count > 2`: l'utilisateur choisit un cocktail, puis la roue; chaque pompe est resolue depuis la recette.

## Pompes et modes quantite

- Nombre de pompes configurable dans Parametres > Jeu: `pump_count` (min 2, max 6).
- Pompe 1: mesuree au debitmetre (constante pulses/cL).
- Pompes 2..N: commandees au temps (constante cL/s).
- Dans une recette cocktail, chaque pompe peut etre en mode:
  - `cL` (quantite absolue)
  - `% verre` (pourcentage du verre selectionne)
  - `Roue` (prend la valeur tiree sur la roue)

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
| `pump_count` | 2 | Nombre de pompes declarees |
| `pump1_pulses_per_cl_x1000` | 540420 | Constante pompe 1 (pulses/cL) en fixe x1000 |
| `pump2_flow_clps_x1000` | 2800 | Constante pompe 2 (cL/s) en fixe x1000 |
| `pump3_flow_clps_x1000` ... `pump6_flow_clps_x1000` | 2800 | Constantes pompes 3..6 (cL/s) en fixe x1000 |

## Cocktails (CRUD)

Disponible dans Parametres > Cocktails:

- Ajouter un cocktail (nom unique)
- Supprimer un cocktail
- Editer la recette par pompe
- Enregistrer les lignes de recette

Schema SQLite associe:

- `cocktails(id, name)`
- `cocktail_pumps(cocktail_id, pump_index, mode, value)`

## Calibration pompes

Un onglet Calibration est disponible dans Parametres.

Workflow operateur:

1. Selectionner une pompe (1..N).
2. Entrer la quantite cible en cL.
3. Appuyer sur Demarrer.
4. Quand la quantite est atteinte dans le verre, retirer le verre du capteur de presence.
5. Le firmware stoppe la pompe, calcule la constante, puis renvoie la valeur finale.
6. L'IHM enregistre la constante de la pompe selectionnee en base et tente la sync immediate.

Comportement Reset:

- Le bouton reset de calibration remet uniquement la pompe selectionnee a sa valeur par defaut.
- Les autres reglages restent inchanges.

Formules appliquees:

- Pompe 1: `pulsesPerCl = pulseCount / quantite_cL`
- Pompes 2..N: `flowClPerSec = quantite_cL / duree_secondes`

Source de verite:

- La base SQLite de l'IHM est la source de verite des constantes.
- Au demarrage, l'IHM envoie les constantes au microcontroleur via `SETFLOW`.
- Le microcontroleur applique ces valeurs en runtime (pas de persistance EEPROM dans cette version).

## Protocole serie (version multi-pompes)

Commandes IHM vers controleur:

- `DISPENSE:<p1_cl>:<p2_cl>[:<p3_cl>...]`
- `SETFLOW:<p1_pulses_per_cl>:<p2_clps>[:<p3_clps>...]`
- `CAL:<pump_index>:<quantity_cL>`
- `STOP`

Reponses controleur vers IHM:

- Distribution:
  - `STARTED:<v1>:<v2>...`
  - `PROGRESS:<v1>:<v2>...`
  - `OK:<v1>:<v2>...`
  - `STOPPED:<v1>:<v2>...`
  - `ERROR:...:<v1>:<v2>...` (suivant le contexte)
- Calibration:
  - `CAL:STARTED:<pump_index>:<target_cl>`
  - `PROGRESS:<value>`
  - `CAL:STOPPED_BY_SENSOR:<pump_index>:<constant>`
  - `CAL:STOPPED:<pump_index>:<constant>`
- Flow:
  - `SETFLOW:OK:<p1_pulses_per_cl>:<p2_clps>...`

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
  -DIHM_USE_LINUX_DRM=OFF
cmake --build build -j1
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

## Deployment et service

- Le service systemd reference dans ce repo est `ihm-app.service`.
- Il attend un binaire sur la cible a l'emplacement `/home/ricardo/ihm/ihm.app`.
- Le build CMake produit `ihm.app` dans le dossier de build (`build` ou `build-rpi-mount`).

Si besoin d'installation via CMake:

```bash
cd ihm/ui
cmake --install build-rpi-mount --prefix /home/ricardo/ihm
```

## Base de donnees

- `game.db` est cree automatiquement au premier lancement.
- Script de generation de donnees de test: `lv_port_pc_vscode/gen_mock_db.sh`

## Depannage rapide

- Si vous lancez `clean-third-party`, relancez `cmake -S . -B <build-dir> ...` avant `cmake --build`.
- Si le build cross echoue avec GLIBC_2.38, verifier que:
  - le sysroot est monte en utilisateur normal,
  - le toolchain AArch64 du repo est bien utilise,
  - vous avez fait un rebuild propre de `build-rpi-mount` et `.third_party_build/lvgl`.
