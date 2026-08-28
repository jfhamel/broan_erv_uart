# Protocole RS-485 Broan/NuTone ERV — résumé complet

Ce document combine ce qui est documenté sur [spitko.net](https://spitko.net/2025/08/08/Reverse-Engineering-an-ERV/) (projet [broan_erv_uart](https://github.com/nspitko/broan_erv_uart)) et les conclusions tirées de l'analyse du protocole sur cette installation.

---

## 1. Bus et adressage

- Bus RS-485, un ERV et un ou plusieurs contrôleurs (têtes murales) partagent la ligne.
- Chaque appareil a un ID (1 octet). L'ERV et le contrôleur ont chacun le leur; jusqu'à 32 adresses seraient permises par le protocole.
- Le contrôleur (mural ou notre `echangeur` qui joue ce rôle) = adresse `10`, l'ERV = adresse `12`.
- **Contrôle de flux (« flow control »)** : un seul appareil à la fois a le droit de parler. L'ERV offre le contrôle (`04`), le demandeur répond `05` pour le prendre, envoie ses messages (chacun suit un aller-retour requête/réponse), puis rend le contrôle en renvoyant `04`, et ainsi de suite. Documenté sur spitko.net.
- **Important** : le composant `broan_erv_uart` (notre `echangeur`) agit lui-même comme contrôleur (adresse `10`, codé en dur dans `broan.h`). Une fois déployé de façon permanente, il n'y a donc **plus de contrôleur mural physique sur le bus** — c'est notre ESP32 qui en tient lieu. Toute donnée que le contrôleur mural fournissait normalement (température/humidité ambiante, voir `04 50`/`05 50` ci-dessous) doit alors venir d'ailleurs : un autre instrument (capteur HA, sonde câblée à l'ESP32) configuré dans le YAML.
- **Écho électrique possible** : sur certains transceivers RS-485 half-duplex, les octets qu'on transmet nous-même peuvent être relus sur notre propre ligne RX. Une implémentation doit donc filtrer les trames selon leur adresse source plutôt que de supposer que tout ce qui est reçu provient de l'autre appareil.

## 2. Structure d'une trame

```
01 <FROM> <TO> 01 <LEN> <PAYLOAD...> <CHECKSUM> 04
```

- Octet 1 : toujours `01`
- Octets 2-3 : adresse source / destination
- Octet 4 : toujours `01`
- Octet 5 : longueur du payload (payload + checksum, mais pas les 4 octets d'en-tête ni le `04` final)
- Payload : dépend de l'opcode (voir section 3)
- Avant-dernier octet : checksum — somme de tous les octets précédents (à partir du premier `01`), moins un, soustraite de zéro (calcul en overflow 8 bits). Documenté sur spitko.net.
- Dernier octet : toujours `04`

## 3. Types de messages (opcodes)

| Opcode | Sens | Description |
|---|---|---|
| `02`/`03` | init | Ping/pong au démarrage (une seule fois, hors flow-control) |
| `04`/`05` | flow control | Offre / prise de contrôle de la ligne |
| `20` | requête | Lecture — liste de registres (2 octets chacun, sans longueur/donnée) |
| `21` | réponse | Lecture — pour chaque registre demandé : `<registre 2o> <longueur 1o> <données>` |
| `40` | requête | Écriture — un ou plusieurs `<registre 2o> <longueur 1o> <données>` |
| `41` | réponse | Accusé de réception d'écriture — `<registre 2o>` (parfois suivi d'un écho partiel) |

Le cycle de lecture normal interroge en boucle un ensemble fixe de registres (température, humidité, filtre, mode, vitesses, etc.), à intervalle régulier (~10 fois/seconde environ selon la charge du bus).

## 4. Registres connus

### Déjà documentés sur spitko.net

| Registre | Contenu |
|---|---|
| `14 00` | Uptime, en secondes |
| `02 20` | Mode de l'ERV (partiel — voir section 5) : `0A`=MAX, `09`=MIN, `01`=STB (standby) |
| `08 22` / `06 22` | Vitesse en % (0%→32, 100%→175 selon l'échelle observée par spitko.net) — écrits ensemble. **Applicable uniquement en mode continu (Manual/`0x0B`)** : en recirculation, l'ERV ignore complètement une cible personnalisée écrite ici — le débit réel reste figé peu importe la valeur envoyée. La recirculation n'a donc que 3 paliers fixes, jamais de vitesse continue |

### Registres additionnels

| Registre | Type | Contenu |
|---|---|---|
| `00 20` | uint8 | **Mode commandé complet** (voir table section 5) — englobe tous les modes, y compris Turbo/Absence/Deshumidistat |
| `02 20` | uint8 | Mode de **base commandé** — reste sur le dernier mode « normal » pendant qu'un mode superposé (Turbo/Absence/Deshumidistat) est actif. **Ne reflète pas la bascule interne du mode Smart** entre échange et recirculation (voir `07 20` et section 7) — reste figé sur `0x11` tout du long dans ce cas précis. Exposé comme `base_fan_mode` |
| `07 20` | uint8 | **VentilationState.** Code d'état couvrant tous les modes, pas seulement échange/recirculation : `00`=off, `01`=exchange, `02`=deshumidistat, `03`=turbo, `04`=exchange (fonctionnellement identique à `01`, différence non comprise au niveau de la configuration physique de l'appareil), `05`=override (Ovr/boost salle de bain), `06`=recirculation min, `07`=recirculation max, `08`=recirculation medium. Change de façon fiable dans tous les sens, y compris à l'intérieur du mode Smart où ni `00 20` ni `02 20` ne bougent, et pour les deux phases (repos/actif) d'Absence et Intermittent. Exposé comme `ventilation_state` |
| `01 E0` | float32 LE | Température (°C), capteur d'admission de l'ERV. **Non représentative en recirculation** (l'air admis est alors de l'air recyclé, pas extérieur) — le composant HA publie `NAN`/indisponible dans ce cas plutôt que la valeur mesurée |
| `08 E0`, `09 E0` | float32 LE | Deux capteurs additionnels, jamais documentés par ailleurs. Tous deux dérivent lentement et continûment (aucun saut discret observé) — probablement d'autres points de mesure de température, mais rôle exact (admission/évacuation/cœur d'échange?) non confirmé. Déclarés avec un taux de rafraîchissement `NEVER` — jamais interrogés activement par le composant, seulement observables en écoute passive si le contrôleur mural les interroge lui-même |
| `08 30` | uint32 LE (secondes) | **Durée de vie restante du filtre**, en secondes — lu en continu, alimente le capteur `filter_life`. Aussi écrit lors d'un reset (voir section 11) |
| `01 30` | uint8 | **FilterReset** — mettre à `1` déclenche l'application de la valeur préchargée dans `09 30` vers `08 30` |
| `09 30` | uint32 LE (secondes) | **FilterLifeStage.** Le contrôleur mural y écrit la valeur désirée (en secondes) **avant** de déclencher `01 30=1` — agit comme un registre de « préchargement » que `FilterReset` applique ensuite à `08 30`. Voir section 11 pour la séquence complète |
| `0A F0` | bloc complexe (~120 octets) | Structure multi-champs distincte de `08 30` — contient plusieurs sous-valeurs (probablement historique/statistiques du filtre ou d'autres capteurs), pas décortiquée en détail |
| `04 50` | float32 LE | **Humidité ambiante (%)**, écrite par l'appareil qui contrôle le bus (broadcast périodique — ~20.3s d'intervalle). **Provient du contrôleur mural physique s'il est présent sur le bus — sinon, cette valeur doit être fournie par un autre instrument** (capteur HA, sonde câblée), puisqu'un seul contrôleur peut être actif à la fois et que l'ERV n'a pas de capteur d'humidité à lui |
| `05 50` | float32 LE | **Température ambiante (°C)**, transmise avec `04 50` dans le même paquet — même origine que ci-dessus (contrôleur mural, ou instrument de remplacement), distinct du `01 E0` de l'ERV |
| `00 50` | uint8 | Écriture périodique (~10s), valeur toujours `00` — vraisemblablement un heartbeat/keep-alive, sens exact inconnu |
| `04 30` | uint16 LE (secondes) | **Compte à rebours Turbo** — décrémente en temps réel, maintenu et rapporté par l'ERV |
| `00 22` | uint32 LE (secondes) | **Durée du Turbo choisie** (1h=3600, 2h=7200, 4h=14400) — écrit une seule fois par le contrôleur à l'activation, dans la même trame que `00 20=0x0C` |
| `0A 22` / `0C 22` | float32 LE (%) | **Seuil d'humidité cible** du Deshumidistat — les deux registres reçoivent toujours la même valeur |
| `0F 22` | uint8 (bool) | Drapeau d'activation du Deshumidistat (`01`=actif) |
| `10 22` | uint8 (bool) | Second drapeau, toujours `00` observé — rôle encore incertain |
| `02 30`, `08 20`, `03 20` | uint8 | `02 30` reste constant à `01`, `08 20` reste constant à `00`, `03 20` répond systématiquement avec une longueur nulle. Fonction inconnue. |
| `03 30` | uint16 LE (secondes) | **Compte à rebours du Boost salle de bain** — même comportement que `04 30` (Turbo), registre distinct |

## 5. Table des modes (`00 20` / `02 20`)

| Mode | Valeur (`00 20`) | `02 20` pendant que ce mode est actif |
|---|---|---|
| Off (Standby) | `0x01` | `0x01` |
| Smart | `0x11` | `0x11` |
| Recirc min | `0x05` | `0x05` |
| Recirc med | `0x07` | `0x07` |
| Recirc max | `0x06` | `0x06` |
| Int (Intermittent) | `0x08` | `0x08` |
| Cont min | `0x09` | `0x09` |
| Cont med | `0x0B` | `0x0B` |
| Cont max | `0x0A` | `0x0A` |
| Turbo | `0x0C` | *(mode de base précédent, inchangé)* |
| Absence | `0x0F` | *(mode de base précédent, inchangé)* |
| Deshumidistat | `0x0D` | *(mode de base précédent, inchangé)* |
| Boost salle de bain | `0x02` | *(mode de base précédent, inchangé)* |

Les valeurs `0A`=MAX, `09`=MIN, `01`=STB documentées sur spitko.net correspondent exactement à Cont max, Cont min et Off.

## 6. Mode Turbo — fonctionnement détaillé

Le contrôleur mural propose des durées de **1h, 2h ou 4h**.

**Activation** — une seule trame d'écriture combine les deux registres :
```
40 00 22 04 <durée en secondes, uint32 LE> 00 20 01 0C
```
Pour les trois durées :
- 1h → `10 0E 00 00` = 3600 s
- 2h → `20 1C 00 00` = 7200 s
- 4h → `40 38 00 00` = 14400 s

**Qui tient le décompte?** L'ERV. Le contrôleur transmet la durée **une seule fois**, à l'activation. Ensuite, l'ERV décrémente lui-même un compteur interne en temps réel, exposé (registre différent : `04 30`, en lecture) à chaque cycle de polling normal — ça permet au contrôleur de mettre à jour son affichage sans avoir à retransmettre quoi que ce soit, et permet à l'ERV de revenir seul au mode précédent (`02 20`) une fois le temps écoulé, même si le bus est perturbé.

## 7. Mode Smart — bascule interne échange/recirculation

Contrairement aux autres modes, Smart ne se contente pas de rester fixe : il alterne lui-même, en continu et de façon autonome, entre ventilation en **échange** (air frais admis) et **recirculation** (air recyclé), selon des conditions internes (probablement humidité intérieure et température extérieure — non confirmé précisément). Le contrôleur mural affiche cet état (échange vs recirculation) sur son écran même en mode Smart.

Ni `00 20` (mode commandé) ni `02 20` (mode de base) ne bougent pendant cette bascule interne — les deux restent figés sur `0x11` (Smart) en continu, même quand le débit d'air (CFM) montre clairement des changements de palier correspondant à des transitions réelles. L'état réel de l'échangeur est plutôt stocké dans `07 20`.

### Table complète de `07 20`

Le registre est un vrai code d'état par mode/palier, pas un simple binaire échange/recirculation :

| `07 20` | Signification |
|---|---|
| `00` | Off, et phase de repos d'Absence |
| `01` | Exchange (Smart en échange interne, Intermittent en phase active) |
| `02` | Deshumidistat |
| `03` | Turbo |
| `04` | Exchange (Cont Med direct, phase active d'Absence) — **fonctionnellement identique à `01`, la différence entre les deux n'est pas comprise du point de vue de la configuration physique de l'appareil** |
| `05` | Override (Ovr / boost salle de bain) |
| `06` | Recirculation min (Smart en recirculation interne, Intermittent en phase repos, Recirc Min direct) |
| `07` | Recirculation max |
| `08` | Recirculation medium |

Notes :
- Absence et Intermittent partagent chacun leurs deux phases avec les codes déjà établis (`00`/`06` pour repos-recirc, `01`/`04` pour actif-échange selon le cas) plutôt que d'avoir leurs propres codes dédiés.
- Cont Min/Max (paliers directs de l'échange via les boutons physiques du contrôleur mural) restent non testés — la roulette `fan_speed` de HA ne change jamais `FanMode`, elle ajuste uniquement la cible CFM en restant sur Cont Med (`0x0B`/`07 20=04`).

**Conséquences pour l'implémentation** :
- `ventilation_state` (text_sensor) est entièrement basé sur `07 20`, simplifié à 6 valeurs pour l'usage courant : `off`, `exchange` (englobe `01`/`04`), `deshumidistat`, `turbo`, `override`, `recirculation` (englobe `06`/`07`/`08`). Ne dépend pas de `00 20`.
- La température extérieure (`01 E0`) est publiée comme indisponible (`NAN`) quand `07 20` indique la recirculation — la sonde d'admission ne capte alors que de l'air recyclé, pas l'air extérieur réel.
- Republication immédiate des deux au changement de `07 20`, sans attendre le prochain cycle de lecture de `01 E0`.

## 8. Mode Deshumidistat — fonctionnement détaillé

**Activation/changement de seuil** :
```
40 0A 22 04 <seuil %, float32 LE> 0C 22 04 <même seuil, float32 LE>
40 0F 22 01 01 10 22 01 00
```
Particularités :
- Contrairement au Turbo, **aucune écriture explicite de `00 20=0x0D`** n'est nécessaire — l'ERV bascule lui-même son registre `00 20` sur `0x0D` dès que `0F 22=1` est reçu.
- Changer le seuil en cours d'activation (ex. 45%→55%→60%) ne nécessite pas de réécrire les drapeaux `0F 22`/`10 22` à chaque fois — seuls `0A 22`/`0C 22` sont réécrits.
- Comme pour le Turbo, `02 20` reste figé sur le mode de base (ex. Cont min) pendant toute la durée de l'activation — c'est le mode vers lequel l'ERV revient une fois le seuil d'humidité atteint.

## 9. Boost salle de bain — fonctionnement détaillé

Boutons muraux dédiés (20 / 40 / 60 minutes), câblés séparément du contrôleur RS-485 principal .

**Confirmé pour les trois durées**, via le registre `03 30` :
- 20 min → `B0 04 00 00` = 1200 s
- 40 min → `60 09 00 00` = 2400 s
- 60 min → `10 0E 00 00` = 3600 s
- Annulation → `00 00 00 00`

**Différence fondamentale avec le Turbo : aucune trame d'écriture (`40`) n'apparaît sur le bus** lors des pressions de bouton, ni pour `00 20`, ni pour `03 30`. Seules des lectures (`21`) montrent les registres changer.

**Conclusion : les boutons de la salle de bain sont des contacts secs câblés sur la borne OVR (override) de l'ERV** — une entrée physique dédiée, indépendante du bus RS-485. C'est l'ERV qui détecte la fermeture du contact, démarre son minuteur interne et l'expose en lecture seule — le contrôleur (et notre `echangeur`) ne fait qu'observer via le polling normal, sans jamais rien écrire. Il n'y a donc **aucun moyen de déclencher ce mode par écriture RS-485** — seulement de le lire.

Le composant n'implante pas de déclenchement de ce mode depuis Home Assistant (impossible de toute façon, vu le mécanisme). Il expose plutôt la lecture de l'état : mode actif « Boost salle de bain » (via `00 20=0x02`) et le temps restant (`03 30`), pour distinguer ce cas des autres modes/overrides plutôt que de simplement voir une vitesse élevée sans explication.

Comme pour les autres overrides, `02 20` reste figé sur le mode de base pendant toute la durée du boost, et l'ERV y revient seul une fois le minuteur à zéro (ou sur annulation).

## 10. Mode Absence — deux phases, horaire hebdomadaire non exploré

Comme Intermittent, Absence n'est pas un état fixe : il alterne entre une **phase de repos** (~50 min/h, `07 20=00`, même code que Off) et une **phase active** (~10 min/h, `07 20=04`, même code qu'Exchange/Cont Med direct) — les deux phases utilisent les codes déjà établis plutôt que d'avoir les leurs propres.

`00 20=0x0F` pendant tout le mode (les deux phases), et `02 20` reste sur le mode de base précédent (même comportement que Turbo/Deshumidistat). Le mécanisme de programmation de l'horaire hebdomadaire (registres, format) n'a pas été analysé.

## 11. Réinitialisation du filtre — séquence en trois messages

**Le contrôleur mural offre 1 à 24 mois**, chacun un multiple exact de 30 jours — pas de vrai calendrier (28-31 jours), juste `mois × 2 592 000 s`. Exemples :

```
1 mois = 2 592 000 s = 30 jours pile
2 mois = 5 184 000 s = 60 jours pile
3 mois = 7 776 000 s = 90 jours pile
4 mois = 10 368 000 s = 120 jours pile
```

**La séquence, telle que fait le contrôleur mural et reproduite par le composant :**

```
1) 40 09 30 04 <valeur secondes, uint32 LE>              — seul, "précharge" la valeur désirée
2) 40 01 30 01 01  08 30 04 <même valeur, uint32 LE>      — ensemble : FilterReset=1 + FilterLife
3) 40 08 30 04 <même valeur, uint32 LE>                   — seul, probablement une confirmation d'affichage côté mural
```

Les trois étapes sont nécessaires : `FilterReset` (`01 30=1`) seul ne suffit pas — l'ERV ignore la valeur envoyée dans `FilterLife` (`08 30`) et revient systématiquement à 90 jours pile si `FilterLifeStage` (`09 30`) n'a pas été préchargé au préalable (étape 1).

Fonctionnel pour n'importe quelle valeur (pas seulement les 4 paliers du mural) une fois les trois étapes reproduites. La nécessité stricte de l'étape 3 (réécriture de `08 30` seule) n'est pas confirmée — elle est reproduite par prudence.

## 12. Ce qui reste incertain

- Le rôle exact du registre `10 22` (toujours `00` observé).
- La signification précise de `02 30` (constant à `01`).
- La différence fonctionnelle entre `07 20=01` et `07 20=04` (tous deux « exchange ») — aucune différence observée dans le comportement physique de l'ERV entre les deux.
- Le rôle exact des capteurs `08 E0`/`09 E0`, jamais recoupés avec un affichage du contrôleur mural.
- Pourquoi `03 20` répond systématiquement avec une longueur nulle plutôt qu'une valeur.
- Le bloc `0A F0` (~120 octets), toujours pas décortiqué en détail.
- Si l'écriture de `08 30` (étape 3 de la séquence de reset du filtre, section 11) est réellement nécessaire ou seulement redondante avec `09 30`+`01 30`.
- Le registre `00 50` (heartbeat périodique, valeur toujours `00`).
- Si l'échelle de vitesse `08 22`/`06 22` documentée sur spitko.net (0%→32, 100%→175) s'applique aussi ailleurs dans les données.
- Les conditions exactes (humidité intérieure / température extérieure / autre) qui déclenchent la bascule interne du mode Smart entre échange et recirculation — on sait *où* lire la décision (`07 20`), pas *pourquoi* elle est prise à un moment donné. Broan documente publiquement que Smart utilise l'humidité relative intérieure et la température extérieure (graphique officiel du fabricant), à valider empiriquement avec `ventilation_state`/`indoor_humidity`/une sonde extérieure indépendante.
