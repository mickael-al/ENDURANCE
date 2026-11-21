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

| Paramètre | Valeur initiale | Effet |
|---|---:|---|
| `maximumLod` | 2 | Collecte sur les LOD 0, 1, 2 déjà demandés par le terrain ; plage autorisée 0–2 |
| `maximumSectors` | 512 | Budget de secteurs résidents, indépendants des chunks affichés |
| `candidateSpacing` | 16 m | Pas souhaité des cellules de candidats, arrondi à la puissance de deux compatible avec les voxels |
| `minimumDistance` | 8 m | Exclusion horizontale entre sites voisins, avant quantification |
| `retentionDistance` | 2 048 m | Évacuation des secteurs trop éloignés, avec une marge d'un chunk fin |
| `capacity` par modèle | 65 536 | Réserve de positions ; multiple de 64, modifiable individuellement |
| `minimumScale` / `maximumScale` | 0,8 / 5 | Plage utilisée par le renderer ; les limites de culling incluent l'échelle maximale |

La collecte garde au maximum 16 × 16 sites horizontaux par secteur. Avec
64 cellules et `cellSize = 1`, un secteur fait 256 m et le pas est bien 16 m.
Un voxel plus large ou un autre réglage peut augmenter ce pas effectif.

Chaque profil expose `density`, `minimumNormalY`, `forestPeriod`,
`forestThreshold`, `forestSoftness`, `minimumAboveSea` et `choices`.
`minimumNormalY = 0.82` autorise environ 35° de pente. Le masque de bosquets
utilise deux fréquences de bruit de valeur, sans recalculer le climat.
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
   1 024 enregistrements pour les réglages initiaux, **pas le maillage**.
   La fence de fin d'émission du terrain couvre aussi ce travail.

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
la réserve vaut **51 Mio**, hors maillages et textures. La zone temporaire est
d'environ 2,4 Mio pour trois travaux en vol, et les compteurs lus côté CPU
occupent 408 octets par travail. Réduire les capacités des variantes rares est
possible ; les pages partiellement occupées consomment leur capacité entière.

Les seules données lues par le CPU sont les petits compteurs par modèle, après
la fence existante. Les positions restent sur le GPU, copiées vers les pages
permanentes. Une allocation insuffisante rejette la capture entière concernée,
incrémente un compteur et laisse le terrain fonctionner. Les pages retirées
sont réutilisées après une fence interrogée sans attente bloquante. Les
commandes de copie et la collecte ont un coût : ce chemin ne promet pas un
surcoût nul de génération.

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
- `Asset/Shader/ProceduralTerrain/objects_compact.C.glsl` : regroupement GPU par modèle.
- `ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainObjects.inl` : secteurs, réservations, pages, publication et recyclage.
- `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainObjects.hpp` : contrat de données et filtre frustum.
- `ANTAGONIZ/Source/ProjectUser/EnduranceObjectPlacement.cpp` : chargement du catalogue du jeu.

`python Tests/Trees/check_placement.py` exécute les vrais shaders et le backend
en OpenGL/EGL. `--fast-math` vérifie aussi la compilation C++ avec ce réglage.
Les tests contrôlent le terrain inchangé, la distance minimale, les frontières
positives/négatives/entières, les secteurs partiels, les changements de LOD,
les annulations, le débordement des pools, le recyclage, les biomes exclus,
la pente, la mer et le filtre frustum.
Les tests existants du backend, de l'ordonnanceur et du streaming de la mer
couvrent les points d'intégration. Ces vérifications sur Mesa logiciel ne
remplacent pas une mesure des temps GPU et un test visuel sur la machine cible.
