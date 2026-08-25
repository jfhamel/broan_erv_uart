# CLAUDE.md

Ce fichier fournit des instructions à Claude Code (claude.ai/code) lorsqu'il travaille sur le code de ce dépôt.

## Règle critique — usage de l'IA dans ce projet

Voir `CONTRIBUTING.md`. L'IA peut écrire du **code**, mais **jamais** les messages de commit, les descriptions de PR, ou les commentaires de code — ceux-ci doivent être écrits par un humain qui comprend pleinement ce qui est soumis. Ne propose donc pas de rédiger un message de commit, une description de PR, ou d'ajouter des commentaires explicatifs dans le code, même si on te le demande implicitement — laisse ça à l'utilisateur.

Les commentaires déjà présents dans le code doivent rester en **anglais**, même si les échanges avec l'utilisateur et les messages de commit sont en français.

## Nature du projet

Composant externe ESPHome (C++ + Python) pour communiquer avec des ERV Broan/Nutone/Venmar/VanEE via leur interface série RS-485. Ce n'est pas un projet qui se build ou se teste de façon autonome : il est consommé par ESPHome via `external_components` (git ou chemin local) pointant vers ce dépôt, et n'est réellement validé qu'en compilant/flashant sur du vrai matériel (ESPHome n'est pas installé dans cet environnement de développement). Il n'y a ni CI, ni suite de tests, ni linter/formateur configuré.

## Documentation du protocole

`broan-erv-protocole.md` est la référence faisant autorité sur le protocole RS-485 (adressage, structure des trames, registres connus, table des modes, séquences comme le reset du filtre ou l'activation du Turbo). À consulter avant de modifier `components/broan/broan.h` ou `broan_control.cpp` — les registres et leurs comportements y sont documentés avec les dates de découverte/confirmation par capture.

`CHANGES.md` documente les changements de ce fork par rapport au dépôt d'origine (nspitko/broan_erv_uart).

## Configs locales (`local/`)

Le fichier YAML de production de l'utilisateur ne vit **pas** dans ce dépôt — il vit sur son Mac à `~/esphome/echangeur.yaml`, édité via l'app ESPHome Device Builder. `local/echangeur.yaml` (suivi par git, contrairement au reste de `local/`) en est une **copie manuelle** synchronisée à la demande pour garder un historique — pas de lien symbolique (bloqué par le sandbox de l'app macOS) ni de script de build (ignoré par le toolchain ESP-IDF natif). Avant de committer un changement à cette config, vérifier qu'elle a été resynchronisée depuis `~/esphome/echangeur.yaml`.

Ce dossier est distinct de `examples/`, qui contient des gabarits génériques destinés à d'autres utilisateurs du composant.

## Conventions de code (C++)

- Préfixes hongrois sur les membres : `m_n`/`m_un` (entiers), `m_fl` (float), `m_b` (bool).
- Méthodes de l'API publique en camelCase (`setFanMode`, `setHumiditySetpoint`, `applyFilterLifeReset`, etc.).
- Structure : `components/broan/` (cœur du composant) + un sous-dossier par plateforme ESPHome (`button/`, `number/`, `select/`, `switch/`), à la convention standard des external_components ESPHome.
