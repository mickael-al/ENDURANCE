# ENDURANCE

Base de travail du prototype **ENDURANCE**, construite à partir du moteur **ANTAGONIZ** présent sur le `master` de BellGrave.

Le dépôt contient le moteur, ses dépendances nécessaires et un projet d'éditeur vide. Les déplacements, le grappin, la météo et le déroulement narratif d'ENDURANCE restent à développer.

- [Idées retenues pour le prototype](docs/PROTOTYPE.md)
- [Origine du moteur et périmètre de l'import](docs/ENGINE_ORIGIN.md)
- [Steam et réseau : intégration et utilisation](docs/NETWORKING.md)
- [Créer une extension de l'éditeur](ANTAGONIZ/Documentation/EditorExtensions.md)

## Organisation

| Dossier | Contenu |
| --- | --- |
| `ANTAGONIZ/` | Rendu, physique, audio, éditeur et outils procéduraux du moteur. |
| `Asset/` | Shaders, icônes et ressources générales du moteur. |
| `ThirdParty/` | Sources des bibliothèques nécessaires à la compilation et leurs notices présentes dans le dépôt d'origine. |
| `ENDURANCE/Scene/` | Scènes du nouveau projet ; `Main.scene` fournit une scène vide. |
| `ENDURANCE/Ressources/` | Emplacement des futurs modèles, prefabs, textures, shaders et sons du jeu. |
| `Tests/` | Vérifications générales du moteur reprises de BellGrave. |
| `docs/` | Notes du prototype et traçabilité de l'import. |

Le projet ne dépend pas d'une installation de BellGrave. Ses scènes, ses ressources artistiques, ses systèmes de combat et ses menus ne sont pas inclus. Le socle Steam et réseau de Parasite est intégré séparément pour préparer le futur multijoueur. L'éditeur et les connexions par IP directe restent utilisables lorsque le client Steam est indisponible.

L'ancienne génération de terrain est conservée avec le suffixe `_old` : classes et fichiers `NoiseGeneration_old`, `MarchingCubes_old` et `ShapeBufferChunk_old`, type `VertexBiome_old`, shaders de calcul dans `Asset/Shader/Noise_old/` et shaders de terrain `chunk*_old`, `shadowChunk_old` et `noise_opaque_old`. Le code C++ reste compilé, avec ses noms réservés à cette ancienne version. Les utilitaires génériques restent partagés ; la nouvelle génération n'est pas encore implémentée.

## Compiler

Prérequis : compilateur C/C++ avec C++17, **CMake 3.16 ou plus récent dans la série 3.x**, et un pilote compatible **OpenGL 4.6** pour lancer l'éditeur. Les bibliothèques du moteur sont embarquées ; les en-têtes système de la plateforme restent nécessaires.

### Linux

Exemple de dépendances système sous Debian/Ubuntu :

```bash
sudo apt install build-essential cmake ninja-build libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libasound2-dev
bash build.sh Debug
./Bin/ENDURANCE
```

Le script accepte également `Release`, `RelWithDebInfo` et `--no-warnings`. Il place les fichiers de compilation dans `build/<configuration>` et l'exécutable dans `Bin/`.

### Windows

Ouvrir un terminal de développement Visual Studio x64 disposant de CMake et Ninja :

```powershell
cmake -S . -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
.\Bin\ENDURANCE.exe
```

GLEW est compilé depuis les sources embarquées, sans bibliothèque précompilée à ajouter. `CMakeSettings.json` propose également des configurations pour Visual Studio.

L'exécutable ouvre le projet `ENDURANCE` dans l'éditeur, sur la scène du projet, sans lancer de gameplay. Les préférences locales sont générées au premier lancement. Les scènes et les ressources du jeu restent versionnées.

Steam et le réseau sont compilés par défaut. Pour une version sans ces dépendances :

```bash
cmake -S . -B build/Offline -DENDURANCE_ENABLE_NETWORK=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Offline
```

L'AppID de développement est `480` (Spacewar), configurable avec `-DENDURANCE_STEAM_APP_ID=<appid>`. Les salons ENDURANCE disposent de leur propre identification pour ne pas se mélanger avec ceux de Parasite. Le menu multijoueur et la synchronisation du gameplay restent à développer ; voir la [documentation réseau](docs/NETWORKING.md).

## Vérifications sans fenêtre graphique

Les scripts suivants utilisent le compilateur indiqué par `CXX`, ou `g++` par défaut :

```bash
bash Tests/run-cloth-simulation.sh
bash Tests/run-cloth-async.sh
bash Tests/run-editor-input.sh
bash Tests/run-editor-extensions.sh
bash Tests/run-latest-snapshot.sh
```

Ces vérifications portent sur les composants du moteur. Elles ne valident pas encore les sensations de déplacement ni la direction visuelle du futur jeu.
