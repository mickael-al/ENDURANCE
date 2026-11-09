# Comparer la génération avant et après optimisation

## Versions et périmètre

- Référence : `feature/biome-accumulation`, commit `132ddf1323a77edd9d0d5bdfaa7db8757dc6f3a8`.
- Version optimisée : `feature/terrain-generation-optimizations`, créée directement depuis cette référence.
- La branche de référence et sa scène restent inchangées.
- Une fonction 2D et une fonction 3D indépendante par biome sont conservées. Le catalogue fixe contient maintenant **61 programmes compute** : 52 programmes de biome, cinq préparations communes et quatre extractions.

Le benchmark utilise le **même fichier C++** contre les sources de chacune des deux versions. Les résultats mesurent le débit de génération isolé, sans rendu de la scène ni physique. Le temps de compilation/chargement des programmes est enregistré séparément.

## Changements implémentés

| Partie | Changement |
| --- | --- |
| Colonnes | Accumulation FP32, puis stockage final de 32 octets au lieu de 48 ; hauteur et climat restent FP32. |
| Contrôles | Épaisseur et décalage relatif sur 16 bits ; contrôle `.w` UNORM16 dans `[0,1]`, interprété uniquement par chaque fonction 3D de biome. |
| Réutilisation 2D | Cache borné par XZ/LOD/seed ; un seul producteur pour les alias d’un même lot. |
| Réutilisation 3D | Cache borné par XYZ/LOD/seed, indépendant du masque de raccord. Les consommateurs en cours empêchent l’éviction. |
| Rejets | Preuve GPU conservatrice des chunks homogènes sur tout le halo ; ils ne matérialisent pas leur volume. |
| Passes de biomes | Masques par tuiles XZ, plages Y réduites dans les dispatchs, rejet de groupe avant chargement, pas de R/W de densité pour un delta exactement nul. |
| Métadonnées | Réductions par groupe des atomiques de classification/préparation. |
| Meshing | Comptes par groupe conservés entre count/emit ; groupes vides ignorés avant chargement ; réutilisation des sommets d’arêtes et lectures climatiques spécialisées. |
| Transitions | Tuiles réduites pour N=4/8, pas d’allocation atomique de zéro sommet. |
| Streaming | Index désirés réutilisés ; première couverture rapide, puis remplissages compatibles jusqu’à 16 ajouts, avec la limite de 32 nouveaux maillages par transaction. |
| Collisions | Staging persistant, décodage sur pool fixe, file de constructions bornée, staging libéré avant la fin du BVH/publication. |
| Shaders | Une lecture par fichier source et une identité du pilote par batch ; le cache chaud évite aussi l’initialisation du compilateur. |
| Mesures | Intervalles GPU disjoints par étape, latence des demandes, convergence de couverture, compteurs de cache et mémoire. |

Les paramètres du cache appartiennent au composant de terrain :

| Paramètre | Défaut | Effet |
| --- | ---: | --- |
| `columnCacheEntries` | 128 | Nombre de tuiles XZ conservables. |
| `densityCacheEntries` | 32 | Nombre de champs 3D conservables. |
| `measureGenerationTimings` | true | Active les queries GPU ; false évite leur allocation, leur soumission et leur lecture. |

Les arènes ont toujours au moins assez de slots pour les travaux simultanés. Une capacité de cache à zéro désactive la rétention après le dernier consommateur, mais conserve le partage sûr à l’intérieur d’un lot. Pour N=64, les caches par défaut occupent environ **54,3 Mio**, plus environ 0,62 Mio de scratch pour trois travaux et les métadonnées de meshing. Les valeurs exactes, alignement inclus, sont affichées dans l’éditeur.

`maxInFlight` peut maintenant aller jusqu’à 16, mais les réglages de la scène n’ont pas été augmentés automatiquement : conserver la même concurrence pendant la première comparaison.

## Signification et précision du contrôle `.w`

`fieldRelief(p).w` est un contrôle artistique local. La finalisation le stocke une seule fois en UNORM16. La fonction `fieldVolume(p,column)` le reçoit décodé dans `column.blend.w` et peut l’utiliser pour placer des rochers, former des cavités, modifier une amplitude ou l’ignorer.

Le helper `fieldVolumeRecipeDelta()` n’applique plus de modulation automatique par `.w`. Les recettes actuelles appliquent explicitement leur comportement antérieur dans leur propre fonction : mise à l’échelle des amplitudes puis modulation. Aucune nouvelle forme artistique n’a été imposée pendant cette optimisation.

Les contrôles compactés sont une compression avec perte. Les caches et l’extraction à densité identique ont des contrôles d’égalité exacte ; **la branche optimisée complète n’est pas annoncée identique bit à bit à l’ancienne**, puisque les contrôles sont quantifiés. Les détails de précision et la politique des bandes très minces sont dans [BIOME_ACCUMULATION.md](BIOME_ACCUMULATION.md).

## Benchmark natif Windows avec CMake

Depuis le checkout optimisé, dans PowerShell :

```powershell
git fetch origin
git worktree add --detach ../ENDURANCE-before 132ddf1323a77edd9d0d5bdfaa7db8757dc6f3a8
$terrainBaseline = (Resolve-Path ../ENDURANCE-before).Path

cmake -S Tests/Terrain/Benchmark -B build/terrain-after -DCMAKE_BUILD_TYPE=Release
cmake --build build/terrain-after --config Release

cmake -S Tests/Terrain/Benchmark -B build/terrain-before "-DENDURANCE_SOURCE_ROOT=$terrainBaseline" -DCMAKE_BUILD_TYPE=Release
cmake --build build/terrain-before --config Release

./build/terrain-before/Release/terrain_generation_benchmark.exe --iterations 5 --output before.json
./build/terrain-after/Release/terrain_generation_benchmark.exe --iterations 5 --output after.json
python Tests/Terrain/compare_generation.py before.json after.json
```

Ces chemins d’exécutables correspondent à un générateur Visual Studio. Avec Ninja, l’exécutable est directement dans le dossier de build, sans sous-dossier `Release`. Le benchmark est un petit programme autonome : il construit le backend de génération, GLEW et GLFW, sans le jeu, les assets, Steamworks ou Bullet. Il crée une fenêtre invisible et nécessite OpenGL 4.5 sur le GPU choisi.

Les deux builds utilisent les mêmes options et le même harness. La version précédente n’a pas besoin de contenir le nouveau benchmark : `ENDURANCE_SOURCE_ROOT` désigne ses fichiers moteur et shaders, tandis que le harness vient de la branche optimisée.

Options supplémentaires :

```powershell
./build/terrain-after/Release/terrain_generation_benchmark.exe --biome 0 --jobs 3 --cells 64 --iterations 5 --output polar-after.json
./build/terrain-after/Release/terrain_generation_benchmark.exe --disable-caches --iterations 5 --output no-retention.json
```

`--biome -1` utilise les matrices automatiques ; `0` force PolarIce, `1` FrozenTundra et `17` Savanna. `--disable-caches` est disponible dans le backend optimisé et désactive seulement la rétention des champs terminés. Il ne désactive pas les autres optimisations.

## Variante EGL sous Linux

Le wrapper Python compile le même harness en bibliothèque et crée un contexte EGL sans fenêtre :

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/benchmark_generation.py \
  --glew-object /usr/lib/x86_64-linux-gnu/libGLEW.so.2.2 \
  --engine-root /chemin/ENDURANCE-before --iterations 5 --output before.json

EGL_PLATFORM=surfaceless python3 Tests/Terrain/benchmark_generation.py \
  --glew-object /usr/lib/x86_64-linux-gnu/libGLEW.so.2.2 \
  --iterations 5 --output after.json

python3 Tests/Terrain/compare_generation.py before.json after.json
```

Le chemin GLEW est à adapter au système. Le rapport indique le fournisseur, le renderer et la version OpenGL. **llvmpipe/softpipe permettent de vérifier le fonctionnement du benchmark, pas de conclure sur un GPU matériel.**

## Ce que le benchmark mesure

Chaque itération contient quatre phases de 24 demandes :

1. **`cold_fields`** : huit tuiles XZ et trois chunks verticaux par tuile, champs absents du cache.
2. **`transition_rebuild`** : mêmes champs avec des masques de raccord différents.
3. **`revisit`** : retour aux demandes originales.
4. **`new_columns`** : déplacement vers huit nouvelles tuiles XZ.

Le workload est synthétique et déterministe. Pour garantir des surfaces réelles et des voisins pleins/vides pour toutes les résolutions, il emploie `baseHeight = N/2`, `seaLevel = baseHeight`, `heightAmplitude = N/8` et `detailScale = 0.05*N/64`. Les autres paramètres de bruit restent ceux du backend. Ce n’est pas une mesure de la scène complète aux réglages artistiques par défaut.

Les programmes, seeds, masques et régions exacts sont exécutés avant chaque itération afin de chauffer les compilations différées du pilote. Le backend optimisé vide ensuite uniquement les caches de champs, à l’arrêt, sans recréer ses programmes. Les phases mesurées commencent après cette préparation. Le vidage refuse les travaux encore en cours et ne modifie pas les maillages publiés.

`wall_ms` mesure la durée de soumission et d’achèvement de la phase. Le benchmark alimente les travaux disponibles et vérifie les fences sans attente bloquante ; il ne reproduit pas une cadence de frames/VSync. La médiane des itérations donne une comparaison de débit. Le rapport conserve aussi les nombres de meshes prêts, vides et de sommets : vérifier ces valeurs lorsque les temps changent.

Les nouvelles durées GPU séparent la 2D, la préparation/initialisation, les contributions 3D, le count et l’emit. Le travail partagé est compté une fois par batch. Les résultats de queries sont récupérés plus tard ; une saturation de la file est signalée par `dropped_timing_samples` et `gpu_timings_complete=false`. Les phases attendent seulement leurs queries déjà suivies avant de lire les totaux, pour ne pas mélanger deux phases.

## Comparaison dans la scène du jeu

Comparer aussi les deux branches dans l’éditeur, avec la même caméra, la même seed, les mêmes réglages de terrain et de concurrence. Répéter une construction immobile, un déplacement de 128 m, un retour et une traversée de frontière LOD. Le cache de shaders doit être chaud pour isoler la génération.

La nouvelle interface affiche le temps écoulé jusqu’à convergence de la couverture, les durées GPU par étape, les hits de cache et les allocations. Le chronomètre de couverture commence lorsque le streaming démarre, après l’initialisation des shaders ; il inclut les frames, le planning CPU et la publication des transactions. Il se fige quand la couverture converge.

L’ancien `lastGenerationMilliseconds` mélangeait un batch partagé et une émission individuelle. Ne pas comparer directement cette ancienne valeur à une somme des nouveaux compteurs. Pour l’avant/après automatique, utiliser `wall_ms` du harness commun.

## Évolutions qui restent à mesurer séparément

Cette branche conserve la densité FP32, les groupes 3D 8×4×4 et la liste de triangles. Elle n’ajoute pas de cache volumique des trois bruits, d’allocateur de sommets GPU, de maillage indexé ou de champ creux. Ces variantes ont des coûts supplémentaires ou des changements de format qui demandent une mesure matérielle et une validation distinctes.

Les tuiles XZ inutiles quittent leurs groupes avant les lectures, mais ne sont pas encore compactées dans des listes indirectes par tuile. Les groupes d’émission vides sont ignorés à partir des comptes conservés, sans liste de groupes compactée. Le bouton de régénération recrée encore les objets GL ; le chemin de cache chaud a été allégé, mais leur durée de vie n’a pas été refondue.

Le benchmark natif Windows est fourni et ses dépendances ont été vérifiées ; son lancement Windows et le jeu complet restent à valider sur la machine cible. Le même code de benchmark, le backend et les shaders ont été exécutés dans les validations EGL.
