# Modifications apportées au composant `broan`

Résumé des changements par rapport au dépôt original de nspitko, basé sur le
reverse-engineering documenté dans `broan-erv-protocole.md`.

## Interface Home Assistant (état actuel)

| Entité | Type | Description |
|---|---|---|
| `fan_mode` | select | Options: `off`, `smart`, `intermittent`, `exchange`, `recirculation`, `absence`. `turbo` n'y est pas — voir le switch `turbo`. `recirculation` choisi ici = Max par défaut; voir `recirculation_speed` pour min/med. |
| `turbo` | switch *(nouveau)* | ON démarre le Turbo (durée lue depuis `turbo_duration`), OFF l'annule. Se remet à OFF tout seul quand le Turbo se termine (minuterie à zéro ou changement de mode) — reflète l'état réel, ce n'est pas une simple bascule. |
| `turbo_duration` | number *(nouveau, remplace le select)* | 0 à 240 minutes, pas de 15 (0-4h). Ajuster pendant que le Turbo tourne met à jour le compte à rebours en direct. À 0, démarrer le Turbo est refusé. |
| `fan_speed` | number | Vitesse continue (0-100% → CFM min-max), **applicable uniquement en mode `exchange`**. |
| `recirculation_speed` | number *(nouveau, remplace le select)* | 0-100%, pas de 50 (3 paliers fixes: 0/50/100% → min/med/max). Confirmé par capture : pas de vitesse continue possible en recirculation. |
| `intermittent_period` | number | **Changé.** 10 à 50 minutes, pas de 5 (était 10-50000 secondes). Conversion minutes↔secondes gérée en interne. |
| `humidity_control` (switch) + `humidity_setpoint` (number) | — | **Inchangés**, déjà corrects dans le dépôt d'origine. |
| `current_mode` | text_sensor | `exchange` / `recirculation` / `off` — ce que l'ERV fait réellement (mode de base, `02:20`), indépendamment d'un override actif par-dessus. |
| `override_state` | text_sensor *(nouveau)* | `turbo` / `absence` / `humidity` / `ovr` / `none` — quel override est actif, le cas échéant. Lecture seule. |
| `override_remaining` | sensor *(nouveau, remplace `turbo_remaining`)* | Minutes restantes sur l'override actif (Turbo `04:30` ou Ovr `03:30`, unifié). Lecture seule, 0 si aucun override actif. |
| `indoor_temperature` / `indoor_humidity` | sensor | Republient les valeurs fournies via `setCurrentTemperature()`/`setCurrentHumidity()` (à toi de les appeler avec un capteur externe — voir section suivante). |
| `cancel_override` | button | Sort de Turbo/Absence/Deshumidistat en un clic (utile surtout pour Absence/Deshumidistat, qui n'ont pas de switch dédié comme Turbo). |
| `power`, `filter_life`, `supply/exhaust_cfm`, `supply/exhaust_rpm`, `temperature` (ERV) | — | **Inchangés**. |

## Pourquoi le contrôleur mural ne peut pas fournir température/humidité

Une fois l'ESP32 seul contrôleur sur le bus RS-485 (adresse `0x10`, codé en dur dans `broan.h`), il n'y a plus de contrôleur mural physique pour mesurer et transmettre ces valeurs à l'ERV. `setCurrentTemperature()`/`setCurrentHumidity()` existent pour ça : à appeler depuis ton YAML avec la valeur d'un autre instrument (capteur HA, sonde câblée à l'ESP32). Voir `full_ha_interface_example.yaml`.

## Détails techniques

- **`broan.h`**
  - `BroanFanMode`: `RecirculateMin=0x05`, `RecirculateMed=0x07` ajoutés (seul `Recirculate=0x06`/Max existait).
  - `BroanField`: `TurboDuration` (`00:22`, écriture), `BaseMode` (`02:20`, lecture), `TurboRemaining` (`04:30`, lecture), `OvrRemaining` (`03:30`, lecture, réactivé depuis le bloc de champs inconnus commentés).
  - Nouveau membre `m_eSpeedFamily` : suit si `exchange` est actif (`fan_speed` ne s'applique qu'à ce cas).
- **`broan_control.cpp`**
  - `setFanMode()`: chaînes `off`/`smart`/`intermittent`/`exchange`/`recirculation`/`absence`.
  - `setRecirculationSpeed(float)`: 0-100% → l'un des 3 paliers fixes.
  - `setCurrentTemperature()`/`setCurrentHumidity()`: publient directement vers `indoor_temperature`/`indoor_humidity` (pas de lecture retour nécessaire, registres écriture seule).
  - `setTurboDuration()`: écrit durée + `FanMode=Turbo` en un seul message, comme la trame capturée du contrôleur mural.
  - `startTurbo()` *(nouveau)*: lit `turbo_duration_number_->state`, refuse si 0.
  - `cancelOverride()`: lit le mode de base réel (`02:20`) et le réécrit dans `FanMode`.
- **`broan.cpp`**
  - `fanModeToString()`/`baseModeToString()`: mappages distincts pour `fan_mode`/`override_state` vs `current_mode` (3 états seulement pour ce dernier).
  - Cas `FanMode` regroupe maintenant 4 publications différentes (select, switch turbo, text_sensor override_state, sensor override_remaining) dans un seul bloc avec des `#ifdef` internes — un `switch` C++ ne permet pas plusieurs `case` avec la même étiquette.
- **`select/`**: uniquement `fan_mode` désormais. `turbo_duration_select.*` et `recirculation_speed_select.*` supprimés (remplacés par des `number`).
- **`number/`**: nouveaux `turbo_duration_number.*` et `recirculation_speed_number.*`. `intermittent_period_number.cpp` fait la conversion minutes→secondes.
- **`switch/`**: nouveau `turbo_switch.*`.
- **`sensor.py`**: `turbo_remaining` renommé `override_remaining`, ajout `indoor_temperature`/`indoor_humidity`.
- **`text_sensor.py`**: ajout `override_state` (en plus de `current_mode`).

## Points à valider / limitations connues

1. **Vitesse variable en recirculation — testé et infirmé.** Écrire une cible CFM personnalisée dans `06:22`/`08:22` pendant la recirculation n'a **aucun effet** sur le débit d'air réel (confirmé par capture : `supply_fan_cfm` restait figé à 65 CFM peu importe la valeur envoyée). C'est pourquoi `recirculation_speed` n'a que 3 paliers fixes plutôt qu'une vraie plage continue.
2. **Annulation d'un override par `cancelOverride()`/`startTurbo()` non testée en conditions réelles.** La logique suppose qu'écrire un nouveau `FanMode` interrompt proprement un override en cours — cohérent avec le protocole, mais jamais confirmé par capture d'une annulation manuelle depuis le contrôleur mural.
3. **`Ovr` (bouton salle de bain) reste en lecture seule** — jamais exposé dans `fan_mode` ni déclenchable (bornes OVR à contacts secs sur l'ERV). Son compte à rebours (`03:30`) est lu et alimente `override_remaining`/`override_state` quand actif, comme demandé.
4. Le second drapeau du Deshumidistat (`10:22`, toujours vu à `00`) n'est pas exposé — rôle encore inconnu.

## Fichiers non modifiés

`__init__.py`, `button/filter_reset_button.*`, `number/fan_speed_number.h`, `number/humidity_setpoint_number.*`, `switch/humidity_control_switch.*`, `select/fan_mode_select.*` — inchangés (la logique existante convenait déjà).
