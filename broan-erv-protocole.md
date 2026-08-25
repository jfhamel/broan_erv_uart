# Protocole RS-485 Broan/NuTone ERV — résumé complet

Ce document combine ce qui est documenté sur [spitko.net](https://spitko.net/2025/08/08/Reverse-Engineering-an-ERV/) (projet [broan_erv_uart](https://github.com/nspitko/broan_erv_uart)) et tout ce qu'on a découvert par capture/analyse de logs ESPHome sur ton installation.

---

## 1. Bus et adressage

- Bus RS-485, un ERV et un ou plusieurs contrôleurs (têtes murales) partagent la ligne.
- Chaque appareil a un ID (1 octet). L'ERV et le contrôleur ont chacun le leur; jusqu'à 32 adresses seraient permises par le protocole.
- Dans nos captures : le contrôleur (ou notre `echangeur` qui joue ce rôle) = adresse `10`, l'ERV = adresse `12`.
- **Contrôle de flux (« flow control »)** : un seul appareil à la fois a le droit de parler. L'ERV offre le contrôle (`04`), le demandeur répond `05` pour le prendre, envoie ses messages (chacun suit un aller-retour requête/réponse), puis rend le contrôle en renvoyant `04`, et ainsi de suite. Documenté sur spitko.net.
- **Important** : le composant `broan_erv_uart` (notre `echangeur`) agit lui-même comme contrôleur (adresse `10`, codé en dur dans `broan.h`). Une fois déployé de façon permanente, il n'y a donc **plus de contrôleur mural physique sur le bus** — c'est notre ESP32 qui en tient lieu. Toute donnée que le contrôleur mural fournissait normalement (température/humidité ambiante, voir `04 50`/`05 50` ci-dessous) doit alors venir d'ailleurs : un autre instrument (capteur HA, sonde câblée à l'ESP32) configuré dans le YAML.

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

Dans nos captures, le cycle de lecture normal interroge en boucle un ensemble fixe de registres (température, humidité, filtre, mode, vitesses, etc.), à intervalle régulier (~10 fois/seconde environ selon la charge du bus).

## 4. Registres connus

### Déjà documentés sur spitko.net

| Registre | Contenu |
|---|---|
| `14 00` | Uptime, en secondes |
| `02 20` | Mode de l'ERV (partiel — voir section 5) : `0A`=MAX, `09`=MIN, `01`=STB (standby) |
| `08 22` / `06 22` | Vitesse en % (0%→32, 100%→175 selon l'échelle observée par spitko.net) — écrits ensemble. **Confirmé applicable uniquement en mode continu (Manual/`0x0B`)** : testé et infirmé en recirculation à deux reprises (12 et 13 août 2026), l'ERV ignore complètement une cible personnalisée écrite ici pendant la recirculation — le débit réel reste figé peu importe la valeur envoyée, confirmé via les capteurs `supply_fan_cfm`/`exhaust_fan_cfm`. La recirculation n'a donc que 3 paliers fixes, jamais de vitesse continue |

### Confirmés/découverts dans nos captures

| Registre | Type | Contenu |
|---|---|---|
| `00 20` | uint8 | **Mode commandé complet** (voir table section 5) — englobe tous les modes, y compris Turbo/Absence/Deshumidistat |
| `02 20` | uint8 | Mode de **base commandé** — reste sur le dernier mode « normal » pendant qu'un mode superposé (Turbo/Absence/Deshumidistat) est actif. **Ne reflète PAS la bascule interne du mode Smart** entre échange et recirculation (voir `07 20` et section 8bis) — reste figé sur `0x11` tout du long dans ce cas précis, contrairement à ce qu'on pensait initialement |
| `07 20` | uint8 | **VentilationState — table complète confirmée les 13-15 août 2026.** Code d'état couvrant tous les modes, pas seulement échange/recirculation : `00`=off, `01`=exchange, `02`=deshumidistat, `03`=turbo, `04`=exchange (fonctionnellement identique à `01`, différence non comprise au niveau de la configuration physique de l'appareil), `05`=override (Ovr/boost salle de bain), `06`=recirculation min, `07`=recirculation max, `08`=recirculation medium. Change de façon fiable dans tous les sens testés, y compris à l'intérieur du mode Smart où ni `00 20` ni `02 20` ne bougent, et pour les deux phases (repos/actif) d'Absence et Intermittent. **Précède le débit d'air physique de plusieurs secondes** (4.5 à ~10s selon les captures) — cohérent avec le fait que le vrai changement implique un moteur de porte mécanique qui prend lui-même plusieurs minutes à se refermer complètement (confirmé par l'utilisateur) |
| `01 E0` | float32 LE | Température (°C), capteur d'admission de l'ERV. **Non représentative en recirculation** (l'air admis est alors de l'air recyclé, pas extérieur) — le composant HA publie `NAN`/indisponible dans ce cas plutôt que la valeur mesurée |
| `07 E0`, `08 E0`, `09 E0` | float32 LE | **Trois capteurs additionnels découverts par balayage brute-force (13 août 2026), jamais documentés.** Toutes trois dérivent lentement et continûment (aucun saut discret observé) — probablement d'autres points de mesure de température, mais rôle exact (admission/évacuation/cœur d'échange?) non confirmé |
| `08 30` | uint32 LE (secondes) | **Durée de vie restante du filtre**, en secondes — lu en continu, alimente le capteur `filter_life`. Aussi écrit lors d'un reset (voir section 12), mais probablement redondant avec `09 30`/`01 30` (voir cette section) plutôt que strictement nécessaire |
| `01 30` | uint8 | **FilterReset** — mettre à `1` déclenche l'application de la valeur préchargée dans `09 30` vers `08 30`. **Piège découvert le 19 août 2026** : le code a longtemps écrit `0` au lieu de `1` (hérité tel quel du code d'origine, jamais remis en question) — le reset n'a donc probablement jamais fonctionné avant cette date, malgré son nom explicite dans nos propres commentaires |
| `09 30` | uint32 LE (secondes) | **FilterLifeStage — découvert le 19 août 2026, jamais documenté avant.** Le contrôleur mural y écrit la valeur désirée (en secondes) **avant** de déclencher `01 30=1` — semble agir comme un registre de "préchargement" que `FilterReset` applique ensuite à `08 30`. Voir section 12 pour la séquence complète |
| `0A F0` | bloc complexe (~120 octets) | Structure multi-champs distincte de `08 30` — contient plusieurs sous-valeurs (probablement historique/statistiques du filtre ou d'autres capteurs), pas encore décortiquée en détail |
| `04 50` | float32 LE | **Humidité ambiante (%)**, écrite par l'appareil qui contrôle le bus (broadcast périodique — ~20.3s d'intervalle observé). **Provient du contrôleur mural physique s'il est présent sur le bus — sinon, cette valeur doit être fournie par un autre instrument** (capteur HA, sonde câblée), puisqu'un seul contrôleur peut être actif à la fois et que l'ERV n'a pas de capteur d'humidité à lui |
| `05 50` | float32 LE | **Température ambiante (°C)**, transmise avec `04 50` dans le même paquet — même origine que ci-dessus (contrôleur mural, ou instrument de remplacement), distinct du `01 E0` de l'ERV |
| `00 50` | uint8 | Écriture périodique (~10s), valeur toujours `00` — vraisemblablement un heartbeat/keep-alive, sens exact inconnu |
| `04 30` | uint16 LE (secondes) | **Compte à rebours Turbo** — décrémente en temps réel, maintenu et rapporté par l'ERV |
| `00 22` | uint32 LE (secondes) | **Durée du Turbo choisie** (1h=3600, 2h=7200, 4h=14400) — écrit une seule fois par le contrôleur à l'activation, dans la même trame que `00 20=0x0C` |
| `0A 22` / `0C 22` | float32 LE (%) | **Seuil d'humidité cible** du Deshumidistat — les deux registres reçoivent toujours la même valeur (45.0/55.0/60.0 confirmés) |
| `0F 22` | uint8 (bool) | Drapeau d'activation du Deshumidistat (`01`=actif) |
| `10 22` | uint8 (bool) | Second drapeau, toujours `00` observé — rôle encore incertain |
| `02 30`, `08 20`, `03 20` | uint8 | Trois candidats testés pour l'état interne du mode Smart avant la découverte de `07 20`, **tous écartés le 13 août 2026** : `02 30` reste constant à `01`, `08 20` reste constant à `00` (cohérent avec son ancien commentaire « Smart=0/Continu=1 » côté mode *commandé*, mais ne bouge pas *à l'intérieur* de Smart), `03 20` répond systématiquement avec une longueur nulle |
| `03 30` | uint16 LE (secondes) | **Compte à rebours du Boost salle de bain** — même comportement que `04 30` (Turbo), registre distinct |

## 5. Table des modes (`00 20` / `02 20`)

Établie en capturant chaque mode sélectionné au contrôleur mural, l'un après l'autre :

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

Les valeurs `0A`=MAX, `09`=MIN, `01`=STB documentées sur spitko.net correspondent exactement à Cont max, Cont min et Off — bonne validation croisée.

## 6. Mode Turbo — fonctionnement détaillé

Le contrôleur mural propose des durées de **1h, 2h ou 4h** (pas 1/2/3h).

**Activation** — une seule trame d'écriture combine les deux registres :
```
40 00 22 04 <durée en secondes, uint32 LE> 00 20 01 0C
```
Confirmé pour les trois durées :
- 1h → `10 0E 00 00` = 3600 s
- 2h → `20 1C 00 00` = 7200 s
- 4h → `40 38 00 00` = 14400 s

**Qui tient le décompte?** L'ERV. Le contrôleur transmet la durée **une seule fois**, à l'activation. Ensuite, l'ERV décrémente lui-même un compteur interne en temps réel, exposé (registre différent : `04 30`, en lecture) à chaque cycle de polling normal — ça permet au contrôleur de mettre à jour son affichage sans avoir à retransmettre quoi que ce soit, et permet à l'ERV de revenir seul au mode précédent (`02 20`) une fois le temps écoulé, même si le bus est perturbé.

## 7. Mode Smart — bascule interne échange/recirculation

Contrairement aux autres modes, Smart ne se contente pas de rester fixe : il alterne lui-même, en continu et de façon autonome, entre ventilation en **échange** (air frais admis) et **recirculation** (air recyclé), selon des conditions internes (probablement humidité intérieure et température extérieure — non confirmé précisément). Le contrôleur mural affiche cet état (échange vs recirculation) sur son écran même en mode Smart, ce qui a orienté toute cette investigation.

**Piège découvert (12-13 août 2026)** : ni `00 20` (mode commandé) ni `02 20` (mode de base, jusqu'ici utilisé pour le capteur `current_mode`) ne bougent pendant cette bascule interne — les deux restent figés sur `0x11` (Smart) en continu, y compris pendant des heures où le débit d'air (CFM) montre clairement des changements de palier correspondant à des transitions réelles.

**Registre correct : `07 20`** (voir section 4). Confirmé par deux transitions capturées dans les deux sens :
- Échange → Recirculation : `07 20` passe de `01` à `06` à **20:55:21.709**, le débit d'extraction se stabilise à 48 CFM (signature connue de la recirculation) à **20:55:26.240** — écart de 4.5s.
- Recirculation → Échange : transition symétrique confirmée, avec un écart plus long (~10s) avant stabilisation du CFM.

Ce décalage s'explique par la mécanique : la bascule implique une **porte physique actionnée par un petit moteur**, qui prend plusieurs minutes à se refermer complètement (confirmé par l'utilisateur) — beaucoup plus lent que le simple ajustement de la vitesse des ventilateurs. `07 20` reflète donc la **décision** de l'ERV, pas encore son exécution physique complète.

### Table complète de `07 20`, confirmée sur tous les modes (12-15 août 2026)

Le registre s'est révélé bien plus riche qu'un simple binaire échange/recirculation — c'est un vrai code d'état par mode/palier, testé en isolant chaque état suffisamment longtemps (~20-60s) pour éliminer toute ambiguïté de chevauchement lors de changements rapprochés :

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
- `current_mode` (text_sensor) est entièrement basé sur `07 20`, simplifié à 6 valeurs pour l'usage courant : `off`, `exchange` (englobe `01`/`04`), `deshumidistat`, `turbo`, `override`, `recirculation` (englobe `06`/`07`/`08`). Ne dépend plus de `00 20` du tout — `07 20` encode déjà « off » directement.
- La température extérieure (`01 E0`) est publiée comme indisponible (`NAN`) quand `07 20` indique la recirculation — la sonde d'admission ne capte alors que de l'air recyclé, pas l'air extérieur réel.
- Republication immédiate des deux au changement de `07 20`, sans attendre le prochain cycle de lecture de `01 E0`.

**Méthode de découverte** : le scanner brute-force déjà présent dans le code d'origine (mais désactivé, `#define SCAN_UNKNOWN`) a été temporairement réactivé pour interroger systématiquement tous les registres possibles (groupes `20/21/22/30/40/50/60/E0`, 0x00-0xFF chacun) en boucle, en cherchant ce qui changeait de façon corrélée aux transitions de CFM observées dans Home Assistant. `07 20` avait déjà été repéré (mais mal interprété) par l'auteur d'origine dans un scan antérieur, commenté dans le code comme *"looks like an override-type indicator"*.

## 8. Mode Deshumidistat — fonctionnement détaillé

**Activation/changement de seuil** :
```
40 0A 22 04 <seuil %, float32 LE> 0C 22 04 <même seuil, float32 LE>
40 0F 22 01 01 10 22 01 00
```
Confirmé pour trois seuils différents : 45.0%, 55.0%, 60.0%.

Particularités observées :
- Contrairement au Turbo, **aucune écriture explicite de `00 20=0x0D`** n'est nécessaire — l'ERV bascule lui-même son registre `00 20` sur `0x0D` dès que `0F 22=1` est reçu.
- Changer le seuil en cours d'activation (ex. 45%→55%→60%) ne nécessite pas de réécrire les drapeaux `0F 22`/`10 22` à chaque fois — seuls `0A 22`/`0C 22` sont réécrits, sauf apparemment au tout dernier ajustement dans notre capture, où les drapeaux ont été retransmis aussi (peut-être lié à un « confirmer » sur l'interface du contrôleur).
- Comme pour le Turbo, `02 20` reste figé sur le mode de base (ex. Cont min) pendant toute la durée de l'activation — c'est le mode vers lequel l'ERV revient une fois le seuil d'humidité atteint.

## 9. Boost salle de bain — fonctionnement détaillé

Boutons muraux dédiés (20 / 40 / 60 minutes), câblés séparément du contrôleur RS-485 principal.

**Confirmé pour les trois durées**, via le registre `03 30` :
- 20 min → `B0 04 00 00` = 1200 s
- 40 min → `60 09 00 00` = 2400 s
- 60 min → `10 0E 00 00` = 3600 s
- Annulation → `00 00 00 00`

**Différence fondamentale avec le Turbo : aucune trame d'écriture (`40`) n'apparaît sur le bus** lors des pressions de bouton, ni pour `00 20`, ni pour `03 30`. Seules des lectures (`21`) montrent les registres changer.

**Conclusion : les boutons de la salle de bain sont des contacts secs câblés sur la borne OVR (override) de l'ERV** — une entrée physique dédiée, indépendante du bus RS-485. C'est l'ERV qui détecte la fermeture du contact, démarre son minuteur interne et l'expose en lecture seule — le contrôleur (et notre `echangeur`) ne fait qu'observer via le polling normal, sans jamais rien écrire. Contrairement à Turbo/Deshumidistat, il n'y a donc **aucun moyen de déclencher ce mode par écriture RS-485** — seulement de le lire.

**Décision : pas d'intérêt à implanter le déclenchement** depuis Home Assistant (impossible de toute façon, vu le mécanisme). **La lecture de l'état est par contre intéressante** — afficher dans HA que le mode actif est « Boost salle de bain » (via `00 20=0x02`) et le temps restant (`03 30`), pour distinguer ce cas des autres modes/overrides plutôt que de simplement voir une vitesse élevée sans explication. Le composant `broan_erv_uart` de spitko.net implante déjà quelque chose dans ce sens.

Comme pour les autres overrides, `02 20` reste figé sur le mode de base pendant toute la durée du boost, et l'ERV y revient seul une fois le minuteur à zéro (ou sur annulation).

## 10. Mode Absence — deux phases, horaire hebdomadaire non exploré

Comme Intermittent, Absence n'est pas un état fixe : il alterne entre une **phase de repos** (~50 min/h, `07 20=00`, même code que Off) et une **phase active** (~10 min/h, `07 20=04`, même code qu'Exchange/Cont Med direct) — confirmé par capture le 13 août 2026, les deux phases utilisent les codes déjà établis plutôt que d'avoir les leurs propres.

`00 20=0x0F` pendant tout le mode (les deux phases), et `02 20` reste sur le mode de base précédent (même comportement que Turbo/Deshumidistat). Le mécanisme de programmation de l'horaire hebdomadaire (registres, format) n'a pas été capturé/analysé — décision de l'utilisateur de ne pas creuser cette partie pour l'instant.

## 11. Réinitialisation du filtre — séquence en deux messages

Découvert le 19 août 2026 en comparant nos écritures (sans effet) à une vraie réinitialisation faite via le contrôleur mural physique.

**Le contrôleur mural offre 1 à 24 mois** (confirmé par l'utilisateur — seuls 1/2/3/4 ont été effectivement capturés/testés), chacun un multiple exact de 30 jours — pas de vrai calendrier (28-31 jours), juste `mois × 2 592 000 s`. Exemples capturés :

```
1 mois = 2 592 000 s = 30 jours pile
2 mois = 5 184 000 s = 60 jours pile
3 mois = 7 776 000 s = 90 jours pile
4 mois = 10 368 000 s = 120 jours pile
```

**La vraie séquence, capturée du contrôleur mural (test à 1 mois) :**

```
1) 40 09 30 04 <valeur secondes, uint32 LE>              — seul, "précharge" la valeur désirée
2) 40 01 30 01 01  08 30 04 <même valeur, uint32 LE>      — ensemble : FilterReset=1 + FilterLife
3) 40 08 30 04 <même valeur, uint32 LE>                   — seul, probablement une confirmation d'affichage côté mural
```

**Piège découvert en cours de route** : le code (hérité tel quel de spitko, jamais remis en question) écrivait `01 30=0` au lieu de `01 30=1` — le reset n'a donc probablement **jamais fonctionné** avant cette correction, malgré son nom de registre explicite. Une fois corrigé pour écrire `1`, le déclencheur fonctionnait, mais l'ERV ignorait quand même la valeur envoyée pour `FilterLife` et revenait systématiquement à 90 jours pile — jusqu'à ce qu'on découvre qu'il fallait **aussi** précharger `09 30` en premier (étape 1 ci-dessus, jamais reproduite avant cette date).

**Confirmé fonctionnel pour n'importe quelle valeur** (pas seulement les 4 paliers du mural) une fois les étapes 1 et 2 reproduites — testé avec succès à 60, 45 et 27 jours (ce dernier n'étant même pas un multiple de 30). L'étape 3 (réécriture de `08 30` seule) n'a jamais été testée comme potentiellement inutile — elle est reproduite par prudence, sans certitude qu'elle soit strictement nécessaire.

## 12. Ce qui reste incertain

- Le rôle exact du registre `10 22` (toujours `00` observé).
- La signification précise de `02 30` (constant à `01` dans toutes nos captures — écarté comme indicateur de mode Smart, rôle réel non déterminé).
- La différence fonctionnelle entre `07 20=01` et `07 20=04` (tous deux « exchange ») — aucune différence observée dans le comportement physique de l'ERV entre les deux.
- Le rôle exact des trois capteurs `07 E0`/`08 E0`/`09 E0`, découverts par balayage brute-force mais jamais recoupés avec un affichage du contrôleur mural.
- Pourquoi `03 20` répond systématiquement avec une longueur nulle plutôt qu'une valeur.
- Le bloc `0A F0` (~120 octets), toujours pas décortiqué en détail.
- Si l'écriture de `08 30` (étape 2 de la séquence de reset du filtre, section 11) est réellement nécessaire ou seulement redondante avec `09 30`+`01 30`.
- Le registre `00 50` (heartbeat périodique, valeur toujours `00`).
- Si l'échelle de vitesse `08 22`/`06 22` documentée sur spitko.net (0%→32, 100%→175) s'applique aussi ailleurs dans nos données.
- Les conditions exactes (humidité intérieure / température extérieure / autre) qui déclenchent la bascule interne du mode Smart entre échange et recirculation — on sait maintenant *où* lire la décision (`07 20`), pas encore *pourquoi* elle est prise à un moment donné. Broan documente publiquement que Smart utilise l'humidité relative intérieure et la température extérieure (graphique officiel du fabricant), à valider empiriquement avec `current_mode`/`indoor_humidity`/une sonde extérieure indépendante.
- Cont Min/Max (paliers directs de l'échange, `FanMode=0x09`/`0x0A`) — jamais déclenchés depuis HA (la roulette `fan_speed` ne change pas `FanMode`), nécessiteraient les boutons physiques du contrôleur mural pour confirmer leurs valeurs `07 20` respectives.
