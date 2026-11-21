# Génération du terrain et rendu des arbres : passe de performance

Comparaison avec `b4ca0a1c055f1cb95d9f307cb8a8304364068192`, avant cette passe.
La cible communiquée est une GTX 1660, 6 Go de VRAM, avec `viewDistance = 40096`.
Les objectifs restent **10 secondes de génération et 40 FPS**, soit 25 ms pour
l'ensemble d'une image. Ils ne sont pas certifiés ici : l'environnement de test
dispose de Mesa llvmpipe, sans GPU NVIDIA, et la résolution cible n'est pas connue.

## Réductions de travail vérifiées

| Mesure | Avant | Après | Portée de la mesure |
|---|---:|---:|---|
| Générations initiales à 40 096 m | 4 229 | 2 665 | Planificateur réel, même couverture finale |
| Publications du terrain à 40 096 m | 209 | 85 | Transactions du même remplissage initial |
| Temps CPU médian de planification | 189,11 ms | 60,02 ms | Somme du travail du planificateur, 5 passages ; pas temps GPU |
| Générations initiales à 4 096 m | 2 453 | 1 189 | Même couverture finale |
| Commandes indirectes des arbres | 5 par page | 5 par modèle sélectionné | Page de 64 positions |
| Taille d'un sommet d'arbre | 64 octets | 48 octets | Attribut inutilisé supprimé, attributs visibles conservés |
| Transferts des deux tableaux d'imposteurs | 306 | 18 | 17 vues, 256 px, tous les mipmaps |
| Recalcul des transformations | Tous les arbres publiés | Pages nouvelles ou réallouées | Pages inchangées conservées sur GPU |

Les instantanés finaux du planificateur (origines, LOD, raccords) sont identiques
octet par octet. Un oracle indépendant compare l'extraction optimisée à
l'extraction exhaustive : **20 210 triangles et normales identiques sur 17
champs**, avec cavités, piliers, fond marin, grandes coordonnées et LOD variés.
Ces vérifications établissent l'équivalence, pas un pourcentage de FPS gagné.

## Ce qui change

- Le planificateur anticipe les raccords vers les voisins encore absents. Il
  évite de reconstruire les mêmes chunks lors du remplissage progressif.
- La file autorise au plus deux interrogations non bloquantes par mise à jour,
  partageant les mêmes budgets d'envoi et de mémoire. Les étapes de comptage et
  d'émission peuvent s'enchaîner plus tôt sans attendre activement une fence.
- La scène passe de 3 à 8 travaux simultanés, avec toujours 16 envois maximum
  par image. Le supplément calculé de buffers explicites est d'environ 2,07 Mio
  pour sa grille de 64³ et son catalogue actuel. Les budgets de sommets,
  modèles et caches terrain restent inchangés ; les surcoûts internes du pilote
  sont exclus. Ce plafond réduit la contrainte de cadence sans augmenter la
  qualité ni le nombre de chunks demandés. Son effet sur les temps d'image
  reste à vérifier sur la GTX 1660.
- Les limites verticales conservatrices écartent les groupes de voxels qui ne
  peuvent contenir de surface. Les métadonnées existantes sont réutilisées.
- Les descripteurs des tâches et les paramètres atmosphériques ne réécrivent
  plus un petit buffer potentiellement encore lu par des commandes en attente.
- Les publications d'objets ne changent de révision que si leur contenu change.
  Une identité d'allocation protège le cache lors du recyclage d'une page.
- Le culling des arbres compacte les références par modèle. Les buffers
  grandissent géométriquement et conservent les transformations déjà produites.
- Le calcul des mipmaps alpha utilise un histogramme exact au lieu de multiples
  balayages. Les tests vérifient les mêmes octets qu'avant.
- Le cube de référence `HumainSize` utilise le cache explicite de contenu
  d'ombre immuable. Il conserve son ombre et ses transformations restent
  suivies. Avant, il forçait le recalcul des quatre cartes même à l'arrêt.
  Les objets animés ou inconnus restent conservateurs. Voir le
  [contrat d'invalidation](../ANTAGONIZ/Documentation/ImmutableShadowContent.md).

Le test LightManager vérifie la réutilisation des quatre couches sur 64 images
immobiles, puis leur invalidation lors d'une modification. Les publications de
terrain ou d'arbres peuvent encore invalider l'ensemble des couches : aucun
gain chiffré sur le coût total des ombres en déplacement n'est revendiqué.

## Répartition : changement artistique intentionnel

Le pas des candidats passe de 16 à 32 m et l'exclusion minimale de 8 à 20 m.
Les tailles sont adaptées à chaque famille : la plage uniforme allant jusqu'à
5 fois la taille rendait notamment certains géants excessivement grands.
Le masque existant est resserré, sans nouvelle passe sur le terrain ni nouveau
bruit dans le shader de collecte.

Mesure des shaders de production, seed 1337, sur **268,435456 km² de sol plat
entièrement admissible** ; la pente et la mer diminuent ces nombres en jeu :

| Biome | Arbres/km² avant | Après | Surface où le masque interdit les arbres |
|---|---:|---:|---:|
| Taïga | 1 459,86 | 246,99 | 47,01 % |
| Forêt boréale | 1 636,27 | 285,80 | 44,47 % |
| Forêt tempérée | 1 524,91 | 266,55 | 44,47 % |
| Forêt tempérée humide | 1 672,00 | 283,47 | 47,01 % |
| Jungle tropicale | 1 546,41 | 249,95 | 52,44 % |

Les zones de bosquet dense subsistent ; 32,57 à 42,53 % des carrés de 256 m sont
entièrement vides dans ces profils. La réduction du nombre d'arbres est un
changement demandé de densité, distinct des optimisations à rendu identique.
Les limites de collecte/rétention et la portée des arbres restent inchangées :
40 km de terrain n'impliquent pas 40 km d'arbres détaillés.

## Mesures et reproduction

Le backend a aussi terminé **les 2 665 chunks à la résolution réelle de 64³**,
avant et après, avec huit travaux simultanés, le budget réel de 1 024 Mio et les
objets activés. Il conserve les maillages produits : 769 chunks avec surface,
1 896 vides, **27 228 894 sommets / 326 759 088 octets de maillage** dans les deux
versions. Ce test soumet directement la couverture finale ; il exclut les
transactions du streaming progressif, le chargement/rendu des arbres et le
reste du rendu du jeu.

| Test logiciel | Avant | Après | Échantillons et limites |
|---|---:|---:|---|
| Backend complet avec placement | 13,861 / 13,545 s | 10,961 / 11,042 s | Deux passages par version, initialisation séparée |
| Backend complet sans placement | 11,069 s | 10,035 s | Une paire de contrôle, pas une estimation statistique |
| 1 024 arbres, caméra fixe | 286,00 ms | 190,83 ms | Médiane de deux moyennes de 5 images, rendu logiciel |
| 1 024 arbres, caméra mobile | 338,40 ms | 212,17 ms | Même nombre de primitives ; cinq images par passage |
| 1 024 arbres, publications inchangées | 290,21 ms | 201,17 ms | Deux passages par version, cache de pages conservé |

Pour le backend avec placement, cela représente environ 20 % de temps en moins
dans ce test, avec aussi un changement intentionnel de densité : **1 913 → 312
positions**. Le contrôle sans placement isole une réduction d'environ 9 % sur
une seule paire. Les 37 % de générations supprimées dans le planificateur sont
une mesure distincte, qui n'est pas incluse dans ce test de couverture finale.
Le test d'arbres soumet 2 149 344 primitives par image en vue fixe, avec des
comptages identiques entre versions pour chaque image, y compris en mouvement ;
ses temps ne sont ni les FPS du jeu, ni une prédiction pour la GTX 1660.
La variance entre les passages justifie de conserver tous les rapports.

Sur un petit sous-ensemble de huit chunks principalement vides, le temps total
reste voisin de 100 ms : aucun gain général n'est revendiqué pour ce cas.
Un diagnostic au budget volontairement insuffisant de 1 Mio renvoie bien un
échec et une couverture incomplète. Les
[rapports bruts et leur synthèse](../Tests/Performance/results/2026-11-21-llvmpipe/README.md)
conservent les réglages et empreintes des sources compilées.

Les rapports [planification](performance/streaming-before-after.json) et
[répartition](performance/tree-distribution-before-after.json) enregistrent les
sources et leurs empreintes. Les rapports de file avec
[travaux immédiatement prêts](performance/scheduler-ready-gpu.json) et
[latence simulée](performance/scheduler-delayed-gpu.json) isolent la deuxième
interrogation non bloquante. Sur la première simulation, le même travail initial
passe de 102 à 52 mises à jour ; avec une latence de six mises à jour, le débit
reste inchangé. Ce sont des tests contrôlés, pas des chronomètres NVIDIA.

```sh
python Tests/Terrain/benchmark_streaming.py --reference-ref b4ca0a1 --runs 5
python Tests/Trees/check_distribution.py --help
python Tests/Atmosphere/check_render_integration.py
```

Le [banc de performance](../Tests/Performance/README.md) permet de mesurer les
vrais shaders et le backend, séparément du moteur complet, avec un exécutable
CMake/GLFW prévu pour Windows. Les rapports distinguent initialisation, travail
effectué, temps CPU/GPU, mémoire et couverture incomplète. Une petite sélection
de chunks ou une grille réduite ne peut pas être présentée comme une génération
complète à 40 km. Le chemin Windows/MSVC nécessite encore une exécution native.

Dans l'éditeur, le temps de couverture est maintenant accompagné de l'état
**terminée**, **en cours** ou **bloquée/incomplète**. Il exclut la compilation
initiale des shaders et le chargement progressif des modèles. La case
**Afficher les arbres** agit immédiatement pour comparer le rendu avec/sans
arbres ; elle conserve les assets et n'annule pas la collecte des positions.
Les compteurs de pages recalculées/réutilisées et de chargement/upload CPU
permettent de distinguer ces coûts du rendu GPU.

## Validation

Suites passées : backend terrain, extraction GPU et raccords Transvoxel,
ordonnanceur et récupération après erreur, streaming de la mer, placement,
rendu des arbres, mipmaps, caches d'ombres, atmosphère, sérialisation de la scène
et compilation syntaxique des interfaces réelles du moteur. Les suites arbres,
placement et ombres ont aussi été exécutées avec les options de calcul rapide.
Les captures d'arbres sont majoritairement identiques au baseline ; quelques
angles diffèrent de 1 à 2 niveaux sur 255 sur 1 à 3 pixels.

Il reste à mesurer la scène Ultra complète sur la GTX 1660 : GPU, CPU, VRAM,
temps de génération à froid et à chaud, image immobile puis déplacement.
Les résultats logiciels ne permettent pas d'affirmer que les 10 secondes ou
les 40 FPS sont déjà atteints.
