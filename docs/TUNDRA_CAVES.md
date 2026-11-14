# Tundra : réseaux de cavités issus du bruit 3D marin

Les nouvelles cavités sont produites directement par **`fieldRecipeDensityAtPrepared()`**, la fonction de densité commune appelée par les fonds marins. Le code des nouvelles grottes ne construit plus de salles ellipsoïdales ni de galeries en capsules : le champ marin découpe les volumes, les passages et les ouvertures.

Le relief de Tundra, ses montagnes et ses anciennes formations 3D sont conservés. Les réseaux supplémentaires s'étendent dans des régions sélectionnées par un masque de bruit déformé. Une protection locale les fait disparaître progressivement près des anciennes sculptures ; elle ne rejette plus une région entière parce qu'elle contient un ancien site.

## Où régler la taille

La taille des cavités et l'étendue de leurs regroupements sont indépendantes.

Dans [caves.glsl](../Asset/Shader/ProceduralTerrain/biomes/Tundra/caves.glsl) :

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `TUNDRA_CAVE_HORIZONTAL_SCALE` | 8,0 | Multiplie les longueurs d'onde horizontales du bruit marin. Augmenter agrandit les cavités et les passages individuels. |
| `TUNDRA_CAVE_VERTICAL_SCALE` | 4,0 | Multiplie les longueurs d'onde verticales du bruit marin. |
| `TUNDRA_CAVE_DEPTH` | 180 m | Centre vertical de la bande de creusement sous le terrain. |
| `TUNDRA_CAVE_HALF_DEPTH` | 420 m | Demi-épaisseur maximale de cette bande ; son fondu ferme les cavités profondes. |
| `TUNDRA_CAVE_FEATHER` | 100 m | Transition aux limites verticales. |
| `TUNDRA_CAVE_CARVING` | 400 | Intensité du champ de creusement, en mètres de référence. |
| `TUNDRA_CAVE_SURFACE_RADIUS` | 0,065 | Seuil de cavité près du sol, repris progressivement jusqu'au seuil marin de 0,16 entre 25 et 140 m de profondeur. Une valeur plus élevée ouvre davantage le sol. |

Ces dimensions verticales sont données à `uDetailScale = 0.35`. Le bruit est évalué dans un repère relatif au terrain ; les réseaux suivent donc les grands changements d'altitude. La transformation des coordonnées est également appliquée à la hauteur, à la bande et aux amplitudes passées à la fonction marine, puis sa différence de densité est reconvertie en mètres du monde.

**Une longueur d'onde n'est pas un diamètre minimal de salle.** Le test mesure de véritables sections horizontales d'air sous plafond, avec sol et connexion à l'extérieur, et exige des cavités contenant un disque d'au moins 200 m de diamètre. Les passages et petits lobes peuvent être plus étroits.

Dans [cave_sites.glsl](../Asset/Shader/ProceduralTerrain/biomes/Tundra/cave_sites.glsl) :

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `TUNDRA_CAVE_REGION_SCALE` | 2800 m | Échelle des grandes régions accueillant plusieurs cavités. Augmenter étend les regroupements sans agrandir chaque cavité. |
| `TUNDRA_CAVE_REGION_THRESHOLD` | 0,70 | Augmenter raréfie les régions. |
| `TUNDRA_CAVE_REGION_FEATHER` | 0,12 | Largeur du fondu dans les valeurs du masque. |
| `TUNDRA_CAVE_BAND` | 4864 unités brutes | Bande 3D déclarée, ouverte seulement dans les zones admissibles. Elle couvre la bande interne avec une marge pour le fondu du pipeline. |

La bande et le masque protègent le socle hors de l'enveloppe. À proximité des formations 3D antérieures, une marge progressive de 150 m empêche les nouveaux creusements de modifier leur bande ou leur géométrie. Le volume marin original reste inchangé ; son déplacement de surface est désactivé uniquement dans l'adaptation Tundra, qui enlève de la matière.

## Vérification

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_tundra_caves.py \
  --reference-shaders ../tundra_before/Asset/Shader/ProceduralTerrain \
  --previous-shaders ../tundra_primitive_before/Asset/Shader/ProceduralTerrain \
  --output /tmp/tundra-networks
```

La référence est l'état avant tout ajout de grottes Tundra ; la comparaison optionnelle correspond à l'ancienne version avec deux salles fixes. Le test neutralise séparément le nouvel appel et **la fonction marine elle-même** : dans les deux cas, le volume antérieur doit être retrouvé. Cela démontre que le bruit partagé génère effectivement les cavités.

Les contrôles portent sur la conservation du relief et des anciens sites 3D, la rareté à grande échelle, les entrées près de la caméra sauvegardée, les dimensions réelles des cavités, les plafonds et sols, les poches fermées éventuelles, ainsi que les grilles mondiales de 8/16/32/64 m. L'accumulation de production et les échantillons partagés entre LOD sont comparés près d'une paroi réellement excavée.

Sur les seeds 1337 et 17, les plus grandes sections de cavités contrôlées contiennent des disques de **556 et 792 m de diamètre**. Les plus grands volumes souterrains reliés s'étendent respectivement sur **2,37 × 2,10 km** et **3,50 × 3,94 km**. Environ 98,92–99,26 % de l'air excavé sous plafond est relié à l'extérieur ; quelques poches restent fermées, comme peut le produire un champ de bruit.

Sur deux cartes de 2304 km², 7,93–8,76 % des colonnes échantillonnées contiennent du nouvel air aux profondeurs sondées. Le relief et les anciennes formations restent exactement identiques. Désactiver le helper marin restaure lui aussi exactement l'ancien volume, avec une erreur maximale mesurée de zéro. Les 2662 densités de production et 250 points LOD ciblés concordent ; les 26 biomes passent également la suite générale.

[Entrées au sol, avant/après](biome-validation/tundra_caves_local.png) · [Coupes des réseaux](biome-validation/tundra_caves.png) · [Mesures](biome-validation/tundra_caves.json)

**Régénérer le terrain** après récupération des shaders. Les mesures OpenGL/Mesa vérifient la géométrie et le pipeline ; elles ne remplacent pas le contrôle du rendu, des collisions et des performances sur le GPU du jeu.
