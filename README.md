# Ricardo – IHM jeu de la roue

Jeu de boisson sur LVGL v9 / SDL2 (simulateur PC) et cible embarquee.  
Ecran 800x480 (5 pouces). Persistance SQLite3.

## Ecrans

| Ecran | Role |
|---|---|
| **Accueil** | Boutons Jouer / Classement / Parametres |
| **Profil** | Grille de selection de l'utilisateur actif |
| **Roue principale** | Roue fortune 0/2/4/6 cL, modifiee par bonus/malus |
| **Roue bonus** | Declenchee aleatoirement apres un spin : Bonus / Malus / +Temps / -Temps / Rien |
| **Donner modificateur** | Choisir a qui attribuer le modificateur gagne |
| **Classement** | Tableau tri par cL bus : Joueur / Total cL / Bonus / Malus / A donne a |
| **Parametres** | Acces PIN (2106), configuration complete |

## Mecaniques de jeu

- **Roue principale** : valeurs de base 0 / 2 / 4 / 6 cL, decalees de ±2 cL par niveau de malus/bonus actif.
- **Roue bonus** : poids configurables independamment pour Bonus, Rien, Malus, +Temps, -Temps.
- **Modificateurs** : bonus et malus s'empilent jusqu'a un maximum configurable (defaut 5).
- **Timeout** : +Temps et -Temps ajoutent/retirent N minutes de cooldown a l'utilisateur cible.
- **Cooldown par joueur** : delai configurable entre deux spins ; compteur en direct sur l'ecran roue.
- **Classement** : colonne "A donne a" affiche le dernier modificateur donne avec la cible.

## Parametres configurables (via ecran Parametres)

| Cle | Defaut | Description |
|---|---|---|
| `wheel_trigger_chance` | 20 % | Probabilite de declencher la roue bonus |
| `bonus_wheel_bonus_weight` | 1 | Poids du segment Bonus |
| `bonus_wheel_nothing_weight` | 2 | Poids du segment Rien |
| `bonus_wheel_malus_weight` | 1 | Poids du segment Malus |
| `bonus_wheel_timeout_add_weight` | 1 | Poids du segment +Temps |
| `bonus_wheel_timeout_remove_weight` | 1 | Poids du segment -Temps |
| `timeout_modifier_minutes` | 5 min | Duree d'un modificateur Temps |
| `max_bonus_stack` | 5 | Empilement maxi de bonus |
| `max_malus_stack` | 5 | Empilement maxi de malus |
| `spin_cooldown_seconds` | 0 s | Cooldown entre deux spins |

## Base de données

SQLite3, fichier `game.db` cree automatiquement au premier lancement.  
Script `lv_port_pc_vscode/gen_mock_db.sh` pour regenerer une base de test avec 6 utilisateurs.

## Build (simulateur PC)

```bash
cd lv_port_pc_vscode/build
make -j$(nproc)
./bin/main
```

## Structure

```
ihm/ui/
  screens/   – un fichier .c/.h par ecran
  widgets/   – wheel_fortune (widget roue reutilisable)
  components/ – game_db (SQLite3), game_logic (regles)
  src/       – main, screen_manager
```
