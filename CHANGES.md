# Modifications apportées au composant `broan`

Résumé des changements par rapport au dépôt original de nspitko, basé sur le
reverse-engineering documenté dans `broan-erv-protocole.md`.

## Interface Home Assistant (état actuel)

| Entité | Type | Description |
|---|---|---|
| `fan_mode` | select | **11 options**: `off`, `smart`, `intermittent`, `exchange_min`, `exchange_med`, `exchange_max`, `exchange_adjustable`, `recirculation_min`, `recirculation_med`, `recirculation_max`, `absence`. `turbo` n'y est pas — voir le switch `turbo`. |
| `turbo` | switch | ON démarre le Turbo (durée lue depuis `turbo_duration`), OFF l'annule. Se remet à OFF tout seul quand le Turbo se termine (minuterie à zéro ou changement de mode) — reflète l'état réel, ce n'est pas une simple bascule. |
| `turbo_duration` | number | 0 à 240 minutes, pas de 15 (0-4h). Ajuster pendant que le Turbo tourne met à jour le compte à rebours en direct. À 0, démarrer le Turbo est refusé. |
| `fan_speed` | number | Vitesse continue (0-100% → CFM min-max). **Applicable uniquement quand `fan_mode=exchange_adjustable`** — confirmé par capture qu'`exchange_min`/`exchange_max`/`exchange_med` (et la recirculation, tous paliers) n'acceptent aucune cible personnalisée. |
| `intermittent_period` | number | 10 à 50 minutes, pas de 5 (registre en secondes, converti en interne). |
| `humidity_control` (switch) + `humidity_setpoint` (number) | — | Inchangés, déjà corrects dans le dépôt d'origine. |
| `listen_only` | switch *(nouveau)* | ON: observe passivement tout le trafic du bus (utile avec un vrai contrôleur mural encore branché) sans jamais rien transmettre — remplace l'ancien `#define LISTEN_ONLY` figé à la compilation. OFF (défaut): mode commande normal. Pensé pour être combiné avec un relais qui coupe l'alimentation du contrôleur mural physique, pour ne jamais avoir deux maîtres actifs sur le bus en même temps. |
| `current_mode` | text_sensor | `off` / `exchange` / `deshumidistat` / `turbo` / `override` / `recirculation` — basé sur `07:20` (VentilationState), pas `02:20`. Publie `"unknown"` si le bus ERV est déconnecté depuis plus de 60s. |
| `turbo_remaining` | sensor | Minutes restantes sur le Turbo actif (`04:30`). Lecture seule, 0 si Turbo inactif. |
| `override_remaining` | sensor | Minutes restantes sur l'Ovr actif (`03:30`, boost salle de bain). Lecture seule, 0 si Ovr inactif. |
| `indoor_temperature` / `indoor_humidity` | sensor | Republient les valeurs fournies via `setCurrentTemperature()`/`setCurrentHumidity()` (à toi de les appeler avec un capteur externe — voir section suivante). `indoor_temperature`-source (`01:E0`, capteur d'admission de l'ERV) publie `NAN` en recirculation. |
| `power`, `filter_life`, `supply/exhaust_cfm`, `supply/exhaust_rpm`, `temperature` (ERV) | — | Inchangés. |

**Changements notables de comportement en cours de route :**
- `recirculation_speed` (number, 3 paliers) a existé brièvement puis a été **retiré** — remplacé par `recirculation_min`/`med`/`max` directement dans `fan_mode`, plus simple et sans ambiguïté.
- `exchange` (plain) a été scindé en 4: `exchange_min`/`exchange_med`/`exchange_max` (aucune vitesse ajustable, confirmé par capture) et `exchange_adjustable` (seul à accepter `fan_speed`).

## Pourquoi le contrôleur mural ne peut pas fournir température/humidité

Une fois l'ESP32 seul contrôleur sur le bus RS-485 (adresse `0x10`, codé en dur dans `broan.h`), il n'y a plus de contrôleur mural physique pour mesurer et transmettre ces valeurs à l'ERV. `setCurrentTemperature()`/`setCurrentHumidity()` existent pour ça : à appeler depuis ton YAML avec la valeur d'un autre instrument (capteur HA, sonde câblée à l'ESP32). Voir `full_ha_interface_example.yaml`.

Les deux sont aussi rediffusées automatiquement toutes les ~20.3s (même si la valeur ne change pas), pour reproduire la cadence du contrôleur mural physique confirmée par capture — voir `runTasks()`.

## Détails techniques

- **`broan.h`**
  - `BroanFanMode`: `RecirculateMin=0x05`/`RecirculateMed=0x07` ajoutés. `Min`/`Max`/`Manual` renommés `ExchangeMin`/`ExchangeMax`/`ExchangeMedManual` (0x09/0x0A/0x0B) pour plus de clarté.
  - `BroanField`: `TurboDuration` (`00:22`, écriture), `BaseMode` (`02:20`, lecture), `TurboRemaining` (`04:30`), `OvrRemaining` (`03:30`), `VentilationState` (`07:20`, lecture — voir `broan-erv-protocole.md` section 7 pour la table complète confirmée).
  - `m_bAdjustableSpeed` (remplace l'ancien `m_eSpeedFamily`) : vrai seulement quand `exchange_adjustable` a été explicitement choisi. Comme `exchange_med` et `exchange_adjustable` écrivent le même octet brut (`0x0B`) sur le fil, ce drapeau interne est le seul moyen de les distinguer — impossible à déduire d'une simple lecture du bus.
  - `BUS_TIMEOUT`/`m_unLastValidResponse`/`m_bBusTimedOut` : détection de bus déconnecté (voir plus bas).
- **`broan_control.cpp`**
  - `setFanMode()`: 11 chaînes correspondant aux options du select.
  - `setFanSpeed()`: vérifie `m_bAdjustableSpeed` plutôt qu'un ancien `m_eSpeedFamily`.
  - `setRecirculationSpeed()` **retirée** — plus nécessaire, la recirculation n'a plus de vitesse ajustable, seulement 3 paliers directs.
  - `setCurrentTemperature()`/`setCurrentHumidity()`: publient directement vers `indoor_temperature`/`indoor_humidity`, mémorisent la valeur pour la rediffusion périodique.
  - `setTurboDuration()`: écrit durée + `FanMode=Turbo` en un seul message, comme la trame capturée du contrôleur mural.
  - `startTurbo()`: lit `turbo_duration_number_->state`, refuse si 0.
  - `cancelOverride()`: lit le mode de base réel (`02:20`) et le réécrit dans `FanMode`.
  - `publishBusDisconnected()`: publie `NAN` sur les capteurs numériques et `"unknown"` sur `current_mode` quand le bus ERV est considéré déconnecté.
- **`broan.cpp`**
  - `fanModeToString()`: mappage complet des 11 valeurs de mode, y compris la distinction `exchange_med`/`exchange_adjustable` via `m_bAdjustableSpeed`.
  - `ventilationStateToString()`: table complète confirmée de `07:20` (9 valeurs), simplifiée à 6 catégories pour `current_mode`.
  - Cas `FanMode`/`VentilationState` regroupent plusieurs publications différentes dans un seul bloc avec des `#ifdef` internes — un `switch` C++ ne permet pas plusieurs `case` avec la même étiquette.
  - Watchdog de bus déconnecté dans `runTasks()` (voir plus bas).
  - Tous les commentaires traduits en anglais (le code lui-même était déjà en anglais).
- **`select/`**: uniquement `fan_mode`, avec les 11 options.
- **`number/`**: `turbo_duration_number.*` (remplace l'ancien select). `recirculation_speed_number.*` retiré. `intermittent_period_number.cpp` fait la conversion minutes↔secondes. `fan_speed_number.cpp`/`humidity_setpoint_number.cpp`/`fan_mode_select.cpp` ont maintenant un appel `publish_state()` immédiat dans leur `control()` (corrige un bug de rebond d'affichage rapporté par l'utilisateur).
- **`switch/`**: `turbo_switch.*`, `listen_only_switch.*` (nouveau — voir section dédiée plus bas).
- **`sensor.py`**: `turbo_remaining`, `override_remaining`, `indoor_temperature`/`indoor_humidity`.
- **`text_sensor.py`**: `current_mode`.
- **`TURBO_DURATION_1H/2H/4H`** retirées de `broan.h` — devenues inutilisées depuis le passage du select au number pour `turbo_duration`.

## Détection de bus déconnecté (watchdog)

Si aucune réponse valide de l'ERV (opcode `21`/`41`) depuis `BUS_TIMEOUT` (60s), `publishBusDisconnected()` publie `NAN` sur les capteurs numériques (power, temperatures, filter_life, CFM, RPM, turbo_remaining, override_remaining) et `"unknown"` sur `current_mode`. `indoor_temperature`/`indoor_humidity` sont explicitement épargnés — leur source est un capteur HA externe via `setCurrentTemperature()`/`setCurrentHumidity()`, pas l'ERV, donc ils restent valides même bus coupé.

Limitation connue : `fan_mode`, les switches `turbo`/`humidity_control` gardent leur dernier état connu — `TextSensor`/`Select`/`Switch` n'ont pas d'équivalent de `NAN` dans ESPHome (`set_has_state(false)` seul ne notifie pas HA en temps réel, il faut un vrai `publish_state()`, et ces types n'ont pas de valeur "vide" à publier).

## `listen_only` : observer un contrôleur mural physique encore branché

Runtime équivalent de l'ancien `#define LISTEN_ONLY` (figé à la compilation), utilisé pour les toutes premières captures de ce projet en observant le vrai contrôleur mural sur le bus. Basculer ce switch sur ON, à tout moment, sans reflasher :
- Traite tout message vu sur le bus, peu importe le destinataire (pas seulement ceux adressés à nous) — permet de voir le trafic entre un contrôleur mural physique et l'ERV.
- `send()` devient un no-op complet : rien n'est jamais transmis tant que c'est actif.

Pensé pour être combiné avec un relais (ex: Waveshare) qui coupe l'alimentation 12V du contrôleur mural physique — bascule les deux ensemble (idéalement via une automatisation HA) pour ne jamais avoir deux maîtres actifs sur le bus simultanément.

## Points à valider / limitations connues

1. **Annulation d'un override par `cancelOverride()`/`startTurbo()` non testée en conditions réelles.** La logique suppose qu'écrire un nouveau `FanMode` interrompt proprement un override en cours — cohérent avec le protocole, mais jamais confirmé par capture d'une annulation manuelle depuis le contrôleur mural.
2. **`Ovr` (bouton salle de bain) reste en lecture seule** — jamais exposé dans `fan_mode` ni déclenchable (bornes OVR à contacts secs sur l'ERV).
3. Le second drapeau du Deshumidistat (`10:22`, toujours vu à `00`) n'est pas exposé — rôle encore inconnu.
4. Cont Min/Max n'avaient jamais été testés avec une roulette de vitesse avant qu'on découvre (confirmé par l'utilisateur) que la vitesse n'y est de toute façon pas ajustable — cohérent avec la recirculation, qui a le même comportement.

## Fichiers non modifiés

`__init__.py`, `button/filter_reset_button.*`, `number/fan_speed_number.h`, `number/humidity_setpoint_number.h`, `switch/humidity_control_switch.*`, `select/fan_mode_select.h` — inchangés (la logique/structure existante convenait déjà).
