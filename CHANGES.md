# Modifications apportées au composant `broan`

Résumé des changements par rapport au dépôt original de nspitko, basé sur le
reverse-engineering documenté dans `broan-erv-protocole.md`.

## Nouvelle interface Home Assistant

| Entité | Type | Description |
|---|---|---|
| `fan_mode` | select | **Changé.** Options: `off`, `smart`, `intermittent`, `exchange`, `recirculation`, `absence`. `turbo` retiré d'ici (voir `turbo_duration`). `min`/`max`/`manual`/`recirculate`/`humidity`/`ovr` retirés de la liste sélectionnable.|
| `recirculation_speed` | select *(nouveau)* | Options `min`/`med`/`max`. Choisir une valeur active la recirculation à ce palier — confirmé par capture qu'il n'y a que ces trois paliers fixes, aucune vitesse continue possible (voir note ci-dessous). |
| `turbo_duration` | select *(nouveau)* | Options `1h`/`2h`/`4h`. Sélectionner une durée active le Turbo — reproduit exactement la trame combinée (durée + mode) observée depuis le contrôleur mural. |
| `fan_speed` | number | Vitesse continue (0-100% → CFM min-max), **applicable uniquement en mode `exchange`**. Ne s'applique plus à `recirculation` — voir `recirculation_speed`. |
| `current_mode` | text_sensor *(nouveau)* | `exchange` / `recirculation` / `off` — ce que l'ERV fait réellement, indépendamment d'un override (Turbo/Absence/Deshumidistat) actif par-dessus. |
| `indoor_temperature` | sensor *(nouveau)* | Republie la température envoyée via `setCurrentTemperature()`. |
| `indoor_humidity` | sensor *(nouveau)* | Republie l'humidité envoyée via `setCurrentHumidity()`. |
| `turbo_remaining` | sensor *(nouveau)* | Minutes restantes au compte à rebours du Turbo (registre `04:30`). |
| `humidity_control` (switch) + `humidity_setpoint` (number) | — | **Inchangés**, déjà corrects dans le dépôt d'origine. |
| `power`, `filter_life`, `supply/exhaust_cfm`, `supply/exhaust_rpm`, `temperature` (ERV) | — | **Inchangés**. |

## Changements côté code

- **`broan.h`**
  - `BroanFanMode`: ajout de `RecirculateMin=0x05` et `RecirculateMed=0x07` (seul `Recirculate=0x06`/Max existait).
  - `BroanField`: ajout de `TurboDuration` (`00:22`, écriture), `BaseMode` (`02:20`, lecture), `TurboRemaining` (`04:30`, lecture).
  - Nouvelles méthodes publiques : `setCurrentTemperature()`, `setTurboDuration()`.
  - Nouveau membre privé `m_eSpeedFamily` pour que `setFanSpeed()` sache s'il doit ajuster le CFM continu (`exchange`) ou choisir un palier (`recirculation`).
- **`broan_control.cpp`**
  - `setFanMode()` : nouvelle liste de chaînes (`smart`/`exchange`/`recirculation`/`absence`/`off`). `turbo` retiré (voir `setTurboDuration`).
  - `setFanSpeed()` : branche maintenant sur `m_eSpeedFamily`.
  - `setCurrentHumidity()` : publie maintenant directement vers `indoor_humidity` (pas besoin de lecture retour, le registre est écriture seule).
  - **Nouveau** `setCurrentTemperature()` : symétrique à `setCurrentHumidity()`, écrit `05:50` et publie vers `indoor_temperature`.
  - **Nouveau** `setTurboDuration()` : écrit `TurboDuration` + `FanMode=Turbo` dans un seul message, exactement comme la trame capturée du contrôleur mural.
- **`broan.cpp`**
  - `fanModeToString()` : helper partagé pour l'affichage du `fan_mode` select.
  - `baseModeToString()` *(nouveau)* : mappe `02:20` vers exactement 3 états pour `current_mode`.
  - Nouveaux cas dans `parseBroanFields()` pour `BaseMode` et `TurboRemaining`.
- **`select/`** : nouveau `TurboDurationSelect`. `select/__init__.py` mis à jour (nouvelle liste d'options + enregistrement du nouveau select).
- **`sensor.py`** : ajout de `indoor_temperature`, `indoor_humidity`, `turbo_remaining`.
- **`text_sensor.py`** *(nouveau fichier)* : `current_mode`.

## Correction (après retour terrain)

- **Régression corrigée : le mode `intermittent` avait disparu du `fan_mode` select.** Il avait été retiré par erreur en simplifiant la liste des modes. Le `intermittent_period` number (durée du cycle marche/arrêt) existait toujours mais était devenu inaccessible sans pouvoir sélectionner le mode lui-même. Réintégré.

## Points à valider / limitations connues

1. **Vitesse variable en recirculation — testé et infirmé.** Écrire une cible CFM personnalisée dans `06:22`/`08:22` pendant la recirculation n'a **aucun effet** sur le débit d'air réel (confirmé par capture : `supply_fan_cfm` restait figé à 65 CFM peu importe la valeur envoyée). Contrairement à `exchange`/Manual (`0x0B`), ce n'est donc pas une cible générique réutilisable. Recirculation n'a que 3 paliers fixes, exposés via le nouveau select `recirculation_speed`.
2. **Annulation du Turbo/Absence par le `fan_mode` select non testée.** On sait que le Turbo se termine seul (minuterie à zéro) et qu'on peut le déclencher, mais on n'a jamais capturé une annulation manuelle explicite depuis le contrôleur mural. Le code suppose qu'écrire un nouveau `FanMode` interrompt proprement un override en cours — logique, mais pas vérifié par capture.
3. **`Ovr` (bouton salle de bain) reste volontairement en lecture seule** — jamais exposé dans `fan_mode`, conformément à ta décision (bornes OVR à contacts secs, pas de déclenchement possible ni voulu depuis HA).
4. Le second drapeau du Deshumidistat (`10:22`, toujours vu à `00`) n'est pas exposé — rôle encore inconnu.
5. **`indoor_temperature`/`indoor_humidity` ne viennent pas du contrôleur mural.** Une fois l'ESP32 seul contrôleur sur le bus, il n'y a plus de contrôleur mural physique pour fournir ces valeurs à l'ERV — c'est pourquoi `setCurrentTemperature()`/`setCurrentHumidity()` existent : à toi de les appeler avec la valeur d'un autre instrument (capteur HA, sonde câblée à l'ESP32, etc), configuré dans ton YAML. Voir `full_ha_interface_example.yaml`.

## Fichiers non modifiés

`__init__.py`, `button/`, `number/fan_speed_number.*`, `number/humidity_setpoint_number.*`, `number/intermittent_period_number.*`, `number/__init__.py`, `switch/`, `select/fan_mode_select.*` — inchangés (la logique existante convenait déjà).
