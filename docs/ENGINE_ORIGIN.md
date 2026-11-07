# Origine du moteur

## Source figée

- Dépôt : `mickael-al/BellGrave`.
- Branche examinée : `master`.
- Commit importé : [`ac6dad7d85f53a6a931b1649c3c603e1cfea45f7`](https://github.com/mickael-al/BellGrave/commit/ac6dad7d85f53a6a931b1649c3c603e1cfea45f7), daté du 21 octobre 2026.
- Nature de l'import : copie sélective de cette révision, sans reprise de l'historique Git de BellGrave.

Les fichiers inchangés ont été comparés à leurs empreintes Git dans l'arbre source. Certaines dépendances communes ont été récupérées depuis leur distribution officielle, puis acceptées uniquement lorsque leur contenu reproduisait exactement l'empreinte du fichier dans BellGrave, y compris ses fins de ligne. Les autres fichiers proviennent directement de la révision indiquée.

## Conservé

Le cœur ANTAGONIZ : rendu OpenGL, matériaux, shaders, modèles et animations, effets de post-traitement, physique et tissus, audio, gestion des comportements et des scènes, éditeur et registre d'extensions, outils de bruit, Marching Cubes et autres composants procéduraux présents dans cette version.

Les composants réutilisables de `ProjectUser` sont conservés : `AudioManager`, `ThreadQueue`, `MeshCollider`, `MeshColliderMulti`, `PrefabDefferedLoader`, `PrefabLOD` et `SaveSystem`.

Les dépendances nécessaires sont embarquées : Bullet, OpenFBX, GLEW, GLFW, GLM, Dear ImGui, OpenAL Soft, stb, termcolor et tinyobjloader. Les shaders et ressources générales nécessaires à l'éditeur sont inclus, ainsi que les données HRTF par défaut d'OpenAL.

## Écarté

- Les dossiers `BellGrave/` et `Architect/`, leurs scènes, personnages, modèles de production, musiques et autres ressources artistiques.
- Les classes `BellGrave*`, les classes de gameplay `Game*`, l'éditeur de partitions, l'importeur osu!, les zones de danger et les démonstrations propres à l'ancien projet.
- La localisation, les commandes de discussion et les sessions réseau liées à l'ancien jeu ; leurs dépendances Steamworks et libhv.
- Les documents de gameplay et d'histoire de BellGrave, ses tests de combat et ses workflows associés.
- Les sons de test, les packs de textures de particules non nécessaires au projet vide, les outils et exemples optionnels des bibliothèques.

## Adaptations du bootstrap

Le nom de l'application devient `ENDURANCE`, tout en conservant le nom du moteur et de sa cible CMake `ANTAGONIZ`. L'exécutable est produit dans `Bin/` et utilise les ressources du dépôt. Le dossier du projet est `ENDURANCE/`.

Le démarrage et l'éditeur ne créent plus de session en ligne et ne chargent aucune scène de BellGrave. Une scène `Main.scene` vide est fournie. Le gestionnaire de scènes est sécurisé pour l'absence de scène courante ; les chemins de projet et les préférences de l'éditeur sont séparés de ceux de l'ancien jeu. Les valeurs par défaut des composants réutilisables ne pointent plus vers des ressources de BellGrave.

CMake ne référence plus de sources de gameplay ni de bibliothèques Steam/libhv. GLEW est compilé depuis ses sources sur Linux et Windows ; les bibliothèques ne construisent pas leurs programmes de démonstration. Les anciens réglages globaux du wrapper stb sont retirés et ImGui utilise les composants nécessaires au moteur. Les exceptions C++ restent disponibles, notamment pour le point d'entrée et les composants qui les emploient.

Cette extraction conserve les outils procéduraux existants. Elle n'ajoute pas encore le système de biomes complet, la course, le grappin, les catastrophes ou le passage final décrits dans les notes du prototype.

## Validation de l'import — 7 novembre 2026

- Compilation complète Linux Debug réussie avec GCC 14.2 et CMake 3.31, depuis un répertoire de compilation neuf.
- Les cinq suites de tests du moteur passent : simulation de tissu, tissu asynchrone, entrées de l'éditeur, extensions de l'éditeur et snapshots.
- Le gestionnaire de scènes passe également un contrôle avec AddressSanitizer et UndefinedBehaviorSanitizer : démarrage sans scène, scène absente, transitions et libérations répétées.
- Les 1 311 fichiers de `ThirdParty/` incluent les en-têtes de compatibilité GLFW Windows. Les sources correspondent aux empreintes du commit BellGrave indiqué, sauf les cinq fichiers CMake d'intégration adaptés. Le CMake principal conserve le même runtime MSVC pour le moteur et ses dépendances.
- Toutes les bibliothèques du binaire Linux sont résolues, sans Steamworks ni libhv.

La compilation Windows et l'affichage de l'éditeur restent à vérifier sur une machine équipée : l'environnement de validation ne dispose ni de compilateur Windows ni de serveur d'affichage. Le lancement sans affichage s'arrête proprement à l'initialisation de la fenêtre ; il ne constitue pas une validation graphique.
