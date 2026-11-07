# ENDURANCE

Base de travail du prototype **ENDURANCE**, construite à partir du moteur **ANTAGONIZ** présent sur le `master` de BellGrave.

Le dépôt contient le moteur, ses dépendances nécessaires et un projet d'éditeur vide. Les déplacements, le grappin, la météo et le déroulement narratif d'ENDURANCE restent à développer.

- [Idées retenues pour le prototype](docs/PROTOTYPE.md)
- [Origine du moteur et périmètre de l'import](docs/ENGINE_ORIGIN.md)
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

Le projet ne dépend pas d'une installation de BellGrave. Ses scènes, ses ressources artistiques, ses systèmes de combat et ses menus ne sont pas inclus. Le bootstrap fonctionne hors ligne, sans Steamworks ni libhv.

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

L'exécutable ouvre le projet `ENDURANCE` dans l'éditeur, sur une scène vide, sans lancer de gameplay. Les préférences locales et `ENDURANCE/Setting.json` sont générés au premier lancement et ignorés par Git. Les scènes et les futures ressources du jeu restent versionnées.

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
