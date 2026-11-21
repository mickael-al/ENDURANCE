# Placement des objets

Le terrain produit des **positions GPU regroupées par modèle** pendant son
émission. Cette étape ne charge pas les maillages, les PNG ni `tree.bin`, et
n'affiche pas elle-même les arbres. Le laboratoire de création reste indépendant.
Le [renderer instancié](tree-rendering.md) consomme maintenant les blocs publiés,
pour la caméra comme pour les ombres.

## Réglages du jeu

`ENDURANCE/Ressources/Trees/object_placement.json` est le catalogue du jeu.
Il référence les **102 variantes exportées dans 17 groupes**, sans lire leurs
géométries. Les 26 profils suivent les IDs existants de `ProceduralTerrainBiomes.hpp`.
Un profil contient jusqu'à quatre groupes pondérés ; chaque groupe désigne une
plage de variantes. Les IDs de groupes et leurs poids occupent chacun un `uint`
à quatre octets. `255` signifie vide. Ajouter les nouveaux modèles à la fin des
tables préserve les IDs existants.

Les forêts combinent leurs familles habituelles : conifères/bouleaux/trembles
dans les zones froides, chênes/feuillus dans les zones tempérées,
acacias/baobabs dans les savanes, banians/palmiers dans les jungles et mangroves
dans le biome correspondant. Glace polaire, toundra gelée, déserts rocheux,
volcaniques et rouges, et fond marin sont initialement sans arbres. Les zones
ouvertes ont une densité réduite. Ce sont des réglages artistiques modifiables.

| Paramètre | Valeur du catalogue livré | Effet |
|---|---:|---|
| `maximumLod` | 2 | Collecte sur les LOD 0, 1, 2 déjà demandés par le terrain ; plage autorisée 0–2 |
| `maximumSectors` | 512 | Budget de secteurs résidents, indépendants des chunks affichés |
| `candidateSpacing` | 32 m | Pas souhaité des cellules de candidats, arrondi à la puissance de deux compatible avec les voxels |
| `minimumDistance` | 20 m | Exclusion horizontale entre sites voisins, avant quantification |
| `retentionDistance` | 2 048 m | Évacuation des secteurs trop éloignés, avec une marge d'un chunk fin |
| `capacity` par modèle | 65 536 | Réserve de positions ; multiple de 64, modifiable individuellement |
| `minimumScale` / `maximumScale` | Selon le groupe, voir ci-dessous | Plage utilisée par le renderer ; les limites de culling incluent l'échelle maximale |

La collecte garde au maximum 16 × 16 sites horizontaux par secteur. Avec
64 cellules, `maximumLod = 2` et `cellSize = 1`, un secteur fait 256 m ;
le pas de 32 m donne maintenant **8 × 8 candidats horizontaux**.
Un voxel plus large ou un autre réglage peut augmenter ce pas effectif.
Les valeurs de cette section viennent du catalogue du jeu ; les valeurs par
défaut des structures génériques du moteur ne remplacent pas ce catalogue.

Les plages d'échelle sont adaptées aux dimensions déjà présentes dans les
exports. L'ancien multiplicateur uniforme de 0,8 à 5 agrandissait aussi les
arbres géants : une variante `BIG_02` d'environ 252 m de large pouvait dépasser
1,2 km. Les variantes étroites gardent une plage plus large ; les géants restent
proches de leur taille exportée. Aucun maillage exporté n'est modifié.

| Groupe | Échelle minimale | Échelle maximale |
|---|---:|---:|
| `Acacia` | 0,8 | 1,6 |
| `ArbreMort` | 0,8 | 1,8 |
| `BIG_01`, `BIG_02`, `BIG_DEAD` | 0,85 | 1,15 |
| `Banyan` | 0,75 | 1,4 |
| `Baobab` | 0,8 | 1,7 |
| `Bouleau`, `Tremble` | 1 | 3,2 |
| `Chene` | 0,9 | 1,8 |
| `Conifere` | 1,2 | 3,6 |
| `Cypres` | 1 | 2,4 |
| `FeuilluEtale` | 1 | 2,6 |
| `Genevrier` | 0,65 | 1,4 |
| `Mangrove` | 0,8 | 1,8 |
| `Palmier` | 0,9 | 2,2 |
| `Retombant` | 0,9 | 1,9 |

Les trois groupes géants ont un poids relatif de 1 dans les profils qui les
utilisent. Leurs occurrences restent exceptionnelles, sans les supprimer.
Les `boundingRadius` du catalogue sont recalculés depuis les limites des
exports, l'échelle maximale et une marge de 2 m.

Chaque profil expose `density`, `minimumNormalY`, `forestPeriod`,
`forestThreshold`, `forestSoftness`, `minimumAboveSea` et `choices`.
`minimumNormalY = 0.82` autorise environ 35° de pente. Le masque de bosquets
utilise deux fréquences de bruit de valeur, sans recalculer le climat.
Le masque vaut `smoothstep(threshold - softness, threshold + softness, noise)`.
Sous la première borne, aucun arbre ne passe le masque ; au-dessus de la
seconde, seule la probabilité `density` et les autres tests limitent le peuplement.

| Profils du catalogue | `forestThreshold` | `forestSoftness` | Zone sans arbre | Masque pleinement boisé |
|---|---:|---:|---|---|
| Boisés, dont forêts, jungles et marais | 0,50 | 0,06 | Bruit ≤ 0,44 | Bruit ≥ 0,56 |
| Ouverts, dont toundras, steppes, prairies et savanes | 0,58 | 0,08 | Bruit ≤ 0,50 | Bruit ≥ 0,66 |

Les profils vides conservent une densité nulle. Les périodes régionales restent
propres aux biomes, entre 900 et 1 700 m ; `density` garde ses valeurs précédentes.
Le changement resserre les lisières et conserve des bosquets denses avec de
vraies clairières. Il ne remplace pas le masque par une suppression aléatoire
uniforme. Cette classification décrit les paramètres du fichier ; elle
n'impose pas une règle automatique lors du chargement d'un profil personnalisé.
Les choix de groupes sont des poids relatifs, pas des pourcentages obligatoires.
La mangrove autorise une petite immersion ; les autres profils évitent la mer.

Dans le composant terrain, **Points d'apparition des objets** affiche les
positions publiées, les blocs, la mémoire et les budgets atteints. La checkbox
`objectPlacementEnabled` et le chemin `objectPlacementFile` sont sérialisés.
Les modifications prennent effet après **Régénérer avec ces réglages**.
Un fichier de catalogue absent ou mal formé désactive le placement et affiche
l'erreur sans empêcher le terrain de démarrer. Un biome forcé sans objets
n'alloue pas de pool et n'exécute pas de collecte.

## Collecte et stabilité

1. Une cellule mondiale fixe en XZ fournit un candidat décalé par hash entier.
   Son identité dépend de cette cellule et de la seed, jamais du triangle,
   de l'ordre des atomiques, de la caméra ou d'un emplacement dans le pool.
2. Le shader réutilise les colonnes de climat déjà calculées. Il choisit un
   biome parmi les quatre voisins avec les poids d'interpolation du terrain
   et la proportion marine des colonnes,
   puis applique densité, masque de bosquet, groupe et variante.
3. Une priorité déterministe rejette les candidats trop proches dans les huit
   cellules voisines, y compris de l'autre côté d'un secteur déchargé. Cette
   exclusion conservatrice peut diminuer la densité près d'un voisin non
   plantable ; elle ne demande ni recherche de géométrie ni solveur Poisson.
4. Seuls les voxels contenant un candidat reconstruisent leurs triangles à
   partir des valeurs et tables déjà disponibles. Pente et altitude sont
   vérifiées ; les surplombs orientés vers le bas sont rejetés.
5. Un `atomicMax` garde le plus haut sol admissible par site et par bande
   verticale de la hauteur d'un chunk LOD0. Les faces de transition ne collectent
   rien. Les doublons sur une arête de triangle partagent le même enregistrement.
6. Un petit compute compacte ces résultats par modèle. Il parcourt au maximum
   256 enregistrements avec 64 cellules, `cellSize = 1` et les réglages du
   catalogue, contre 1 024 auparavant, **pas le maillage**. La limite du système
   reste 1 024 avec une grille horizontale de 16 × 16 et quatre bandes verticales.
   Un seul groupe de 64 threads construit un histogramme en mémoire partagée,
   calcule les offsets des modèles et écrit les positions dans une réserve
   commune de taille `sites`. Il remplace tous les compteurs, y compris les
   types vides, sans remise à zéro GPU séparée ni atomiques globaux de compactage.
   Le nombre de sites et de modèles sont des constantes de compilation pour
   ce catalogue ; aucune variante n'est compilée pendant la génération.
   La fence de fin d'émission du terrain couvre aussi ce travail.

Le réglage des clairières réutilise exactement les deux échantillons de bruit
existants, regroupés dans `objectForestMask`. Il n'ajoute ni passe de terrain,
ni lecture des positions côté CPU, ni recherche dans le maillage. Le plus grand
pas réduit le nombre de candidats et la zone temporaire de collecte ; il ne
supprime pas le coût des tests, des atomiques et des copies GPU.

Un secteur réserve jusqu'à 64 bits de couverture correspondant à ses chunks
LOD0. Les captures terminées sont conservées ; les captures concurrentes
réservent des bits distincts. Une subdivision, un raccord ou un regroupement
LOD ne régénère pas les positions déjà capturées. Les blocs ne sont publiés
qu'avec la couverture de terrain correspondante, après validation de l'émission.
L'abandon d'une transaction libère aussi les captures non publiées, après la
fin des travaux encore en vol.

Le système privilégie le coût : aucune tâche de terrain supplémentaire n'est
lancée pour les objets. Une zone vue uniquement au-delà de `maximumLod` reste
sans objets jusqu'à son premier passage à un LOD de collecte. Les positions
déjà obtenues restent disponibles lors de l'éloignement, dans le budget de
rétention. La hauteur capturée peut devenir approximative quand la géométrie
change de LOD. Après une évacuation complète, une première capture à un autre
LOD peut changer la hauteur ou l'admissibilité du site. Ce n'est pas une garantie
de placement identique à toute distance et dans tout ordre de visite.

Le test de sol est local : plusieurs étages suffisamment séparés peuvent
produire plusieurs objets au même XZ. Il ne vérifie pas la visibilité du ciel,
la place disponible pour une couronne ni les collisions avec les autres objets.

## Mémoire, publication et culling

Les positions permanentes occupent **8 octets** en `std430 uvec2` :

| Mot | Bits 0–15 | Bits 16–31 |
|---|---|---|
| 0 | X local UNORM16 | Y local UNORM16 |
| 1 | Z local UNORM16 | ID du site dans le secteur |

Décodage : `world = block.origin + localUNORM16 * block.extent / 65535`.
Pour 256 m, le pas de quantification vaut environ 3,9 mm. Le type, la seed et
l'origine du secteur sont communs au bloc ; aucune matrice n'est stockée par
instance. Le renderer dérive rotation et échelle du hash du site et de la seed,
sans se servir de l'index d'allocation. Les palettes restent celles des exports.

Chaque modèle possède sa plage dans un buffer unique. Des pages de 64 places
sont attribuées aux secteurs. Avec les 102 variantes à 65 536 places chacune,
la réserve vaut **51 Mio**, hors maillages et textures. Avec 64 cellules,
`cellSize = 1` et le catalogue actuel, la zone temporaire totale vaut
**29 248 octets pour huit travaux en vol**, contre 1 684 032 avant le compactage
partagé. Un site ne peut produire qu'une position gagnante ; réserver une plage
de `sites` positions pour chacun des 102 modèles gaspillait donc de l'espace.
Les positions temporaires dépendent désormais du nombre de sites et de travaux,
seuls les compteurs dépendent aussi du nombre de variantes.
Les compteurs lus côté CPU
occupent 408 octets par travail. Réduire les capacités des variantes rares est
possible ; les pages partiellement occupées consomment leur capacité entière.

Les seules données lues par le CPU sont les petits compteurs par modèle, après
la fence existante. Les positions restent sur le GPU, copiées vers les pages
permanentes. Une allocation insuffisante rejette la capture entière concernée,
incrémente un compteur et laisse le terrain fonctionner. Les pages retirées
sont réutilisées après une fence interrogée sans attente bloquante. Les
commandes de copie et la collecte ont un coût : ce chemin ne promet pas un
surcoût nul de génération.

La révision de publication change seulement lorsque la liste triée des blocs
publiés change réellement. Une publication de terrain lointain sans nouveaux
arbres, un raccord ou un changement de LOD conservant les mêmes pages ne
réinitialise donc plus les caches du renderer et des ombres. Les statistiques
de secteurs et d'instances continuent d'être actualisées indépendamment.
Le recalcul et le tri de cette liste sont aussi évités lorsque ni les pages ni
leur couverture n'ont changé. La recherche des ancêtres de terrain utilise un
tableau contigu réutilisé et les seuls LOD effectivement présents, au lieu de
recréer une table de nœuds à chaque transaction. `publicationChecks`,
`publicationRebuilds` et `coarseCoverageLookups` exposent ces opérations.

Chaque nouvelle page reçoit un numéro d'allocation monotone
`ObjectBlock::allocationRevision`, côté CPU uniquement. Son contenu GPU est
immuable jusqu'au retrait de la page. Une plage recyclée reçoit un nouveau
numéro, même si son origine, son nombre d'instances et son offset sont
identiques : les transforms précédents ne doivent alors pas être réutilisés.
La valeur 0 est réservée aux producteurs ne fournissant pas cette garantie.
Ce champ fait partie de la comparaison de publication, sans agrandir les
positions de 8 octets ni changer le format des shaders de collecte.

API de `ProceduralGeneration` :

- `getObjectInstanceBuffer()` : buffer de positions ; sa durée de vie est celle du générateur.
- `getObjectBlocks()` : blocs publiés, classés par modèle et plage d'instances.
- `getObjectPlacementStats()` : compteurs et révision des blocs ; invalider le cache de culling sur cette révision aussi.
- `getObjectPlacementError()` : diagnostic du placement.

`ProceduralTerrain::selectObjectBlocks(blocks, frustum, visible)` utilise le
frustum existant. Les limites incluent le volume du secteur, les modèles à
l'échelle maximale et une marge pour le vent. La sélection de caméra et celle
de chaque passe d'ombre restent distinctes. Le renderer ajoute un second test
GPU par instance et choisit les LOD sans lire les positions côté CPU.

## Fichiers et validation

- `Asset/Shader/ProceduralTerrain/objects_collect.glsl` : candidats, climat, pente et projection sur triangles.
- `Asset/Shader/ProceduralTerrain/objects_common.glsl` : hash, bruit et masque régional partagés.
- `Asset/Shader/ProceduralTerrain/objects_compact.C.glsl` : regroupement GPU par modèle.
- `ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainObjects.inl` : secteurs, réservations, pages, publication et recyclage.
- `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainObjects.hpp` : contrat de données et filtre frustum.
- `ANTAGONIZ/Source/ProjectUser/EnduranceObjectPlacement.cpp` : chargement du catalogue du jeu.

`python Tests/Trees/check_placement.py` exécute les vrais shaders et le backend
en OpenGL/EGL. `--fast-math` vérifie aussi la compilation C++ avec ce réglage.
Les tests contrôlent le terrain inchangé, la distance minimale, les frontières
positives/négatives/entières, les secteurs partiels, les changements de LOD,
les annulations, le débordement des pools, le recyclage, les biomes exclus,
la pente, la mer et le filtre frustum. Ils vérifient aussi que les publications
de terrain sans changement d'arbres gardent leur révision et qu'une page
recyclée reçoit une nouvelle identité d'allocation. Le catalogue maximal de
255 types et le coût constant du stockage temporaire des positions sont
également couverts.

`python Tests/Trees/check_compaction.py --output /tmp/tree-compaction.json`
compare directement l'ancien noyau de compactage et le noyau actuel sur le GPU.
Les 24 cas couvrent 1 à 1 024 sites, 1 à 255 types, les sorties vides, pleines
et réutilisées ainsi que des identifiants invalides. Les comptes et les
positions compactées sont identiques par type, indépendamment de leur ordre
d'écriture ; les sentinelles contrôlent les limites de chaque plage.

`python Tests/Trees/check_distribution.py` mesure le masque avec les fonctions
GLSL de production sur une région de **16,384 × 16,384 km**, seed 1337 par
défaut. Le test contrôle les véritables zones vides, les bosquets pleinement
boisés et le nombre de sites admissibles sur un sol plat entièrement plantable.
La pente, la mer, les budgets et les limites de collecte peuvent ensuite réduire
ces chiffres dans une vraie scène. Une autre taille de voxel peut aussi changer
le pas effectif ; ce test utilise le pas demandé dans le catalogue.

Exemple de comparaison reproductible avec le catalogue antérieur à cette passe :

```bash
git show b4ca0a1:ENDURANCE/Ressources/Trees/object_placement.json > /tmp/placement-before.json
python Tests/Trees/check_distribution.py \
  --baseline /tmp/placement-before.json \
  --output /tmp/tree-distribution.json \
  --captures /tmp/tree-distribution-captures
```

Les captures optionnelles demandent Pillow ; les mesures demandent NumPy et
OpenGL/EGL. Le JSON enregistre la seed, le renderer, l'étendue et les SHA-256
des catalogues, du helper GLSL et du script de mesure.

Résultats de référence sur 268,435456 km², avec le pas demandé et les réglages
ci-dessus :

| Biome | Sites/km² avant | Sites/km² après | Surface où le masque est nul | Surface où le masque vaut 1 |
|---|---:|---:|---:|---:|
| Taïga | 1 459,86 | 246,99 | 47,01 % | 27,42 % |
| Forêt boréale | 1 636,27 | 285,80 | 44,47 % | 30,41 % |
| Forêt tempérée | 1 524,91 | 266,55 | 44,47 % | 30,41 % |
| Forêt pluviale tempérée | 1 672,00 | 283,47 | 47,01 % | 27,42 % |
| Jungle tropicale | 1 546,41 | 249,95 | 52,44 % | 22,47 % |

Ces forêts ont environ **six fois moins de sites en moyenne sur le paysage**.
Les clairières occupaient seulement environ 10 à 13 % de la surface auparavant.
Avec le réglage actuel, 32,57 à 42,53 % des patches échantillonnés de 256 m sont
entièrement vides dans ces profils : les trous ne se limitent pas aux espaces
entre quelques arbres. Les bosquets denses restent présents. Dans les zones où
le masque vaut 1, la somme estimée des ellipses de couronnes représente environ
0,73 fois la surface au sol pour la forêt tempérée et 0,98 à 0,99 pour les deux
profils de jungle/forêt pluviale ci-dessus, avec les nouvelles échelles. Cette
somme inclut les chevauchements et les boîtes englobantes des modèles ; elle
ne mesure pas la couverture réelle des feuilles.

Les tests existants du backend, de l'ordonnanceur et du streaming de la mer
couvrent les points d'intégration. Ces vérifications sur Mesa logiciel ne
remplacent pas une mesure des temps GPU et un test visuel sur la machine cible.
