# Tundra : grottes rares accessibles depuis le sol

Le relief de `Tundra` et ses anciennes formations 3D sont conservés. Les grottes forment une **population supplémentaire indépendante**, avec leurs propres emplacements et leur propre bande 3D locale. Le placement se trouve dans [cave_sites.glsl](../Asset/Shader/ProceduralTerrain/biomes/Tundra/cave_sites.glsl), la géométrie dans [caves.glsl](../Asset/Shader/ProceduralTerrain/biomes/Tundra/caves.glsl), appelée par [volume.C.glsl](../Asset/Shader/ProceduralTerrain/biomes/Tundra/volume.C.glsl).

Chaque grotte comprend une salle souterraine, une salle secondaire et une galerie qui débouche au sol. À `uDetailScale = 0.35`, la salle principale mesure environ 200–270 m de longueur et 52–68 m de hauteur, avec son centre à 48–60 m sous le terrain. Le segment d'entrée a un rayon de 60 m, pour une ouverture au niveau du sol d'environ 100 m avant le bruit. Les hauteurs suivent le terrain local ; ce n'est pas un plancher horizontal fixe en coordonnées monde.

## Réglages

Les constantes de répartition sont au début de `Tundra/cave_sites.glsl` ; le réglage des parois reste dans `caves.glsl` :

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `TUNDRA_CAVE_SPACING` | 1800 m | Taille des cellules candidates indépendantes ; ce n'est pas la distance entre deux grottes. |
| `TUNDRA_CAVE_REGION_SCALE` | 3000 m | Taille du bruit qui organise les régions où des grottes sont possibles. |
| `TUNDRA_CAVE_REGION_THRESHOLD` | 0,35 | Augmenter réduit la surface des régions admissibles. |
| `TUNDRA_CAVE_SITE_CHANCE` | 0,85 | Fraction des nouvelles candidates retenues avant les filtres de région et de dégagement ; diminuer raréfie les grottes. Mettre `0.0` les désactive. |
| `TUNDRA_CAVE_BAND` | 768 unités brutes | Bande locale des nouvelles grottes, soit 268,8 m au total à l'échelle 0,35. |
| `TUNDRA_CAVE_WALL_AMPLITUDE` | 4 m | Déformation maximale des parois, à l'échelle verticale de référence. |

Le masque régional utilise un bruit de valeur continu dans un domaine déformé, évalué au centre de chaque nouvelle candidate. La sélection concerne la grotte entière : une limite du masque ne peut pas couper sa galerie. Les centres sont décalés et les orientations varient. Une vérification des voisins rejette les candidates dont l'enveloppe toucherait une ancienne formation 3D ou un rocher.

Les nouvelles bandes s'ouvrent uniquement là où les anciennes étaient nulles. Les montagnes, le relief, le décalage et le pilotage existants restent identiques ; les anciens sites 3D gardent aussi exactement leur bande et leur fondu. L'opération enlève uniquement de la matière et son influence s'annule progressivement hors de la cavité.

La première version liait au contraire les grottes aux anciennes formations et les filtrait trop fortement : avec la seed 1337, aucune ne se trouvait dans les 8 premiers kilomètres depuis la caméra sauvegardée. La première, à 9,4 km, avait une ouverture de 44 m pour des voxels de 64 m. La répartition indépendante et l'entrée élargie corrigent ces deux limitations, sans déplacer la caméra ni introduire de position spéciale dans les shaders.

## Fonction 3D commune retrouvée

Les fonds marins appellent `fieldVolumeRecipeDelta()` dans [biome_volume_common.glsl](../Asset/Shader/ProceduralTerrain/biome_volume_common.glsl). Son bruit et son champ de cavités sont maintenant exposés séparément par `fieldRecipeNoise()` et `fieldRecipeCavity()`, sans changer la formule marine.

Tundra réutilise ce champ pour irrégulariser les parois. Les ellipsoïdes des salles et les capsules de la galerie maintiennent un passage continu jusqu'au sol ; le bruit seul ne garantirait pas une entrée. Son amplitude reste inférieure au rayon de la galerie.

## Vérification

Le test dédié compare les shaders actuels à une extraction de `Asset/Shader` avant cet ajout :

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_tundra_caves.py \
  --reference-shaders ../tundra_before/Asset/Shader/ProceduralTerrain \
  --output /tmp/tundra-caves
```

Il vérifie la conservation exacte du relief et du volume historique lorsque le nouvel appel est désactivé, la rareté, le sol et le plafond des salles, leur connexion à l'air extérieur, ainsi que l'accumulation réelle et les points communs aux LOD près d'une paroi excavée. Il exige maintenant des entrées réellement ouvertes près de la caméra sauvegardée, des cavités sur les grilles mondiales de 8, 16 et 32 m, et de nouvelles excavations là où la bande 3D précédente était nulle. Le résultat marin est également comparé bit à bit à la référence.

Les rapports enregistrent les six grottes les plus proches sur deux seeds, les cartes de 2304 km² et la zone locale autour de la caméra. Ils distinguent les bandes ajoutées, les véritables entrées au sol et la visibilité des cavités aux différentes résolutions. Les tests généraux couvrent également les 26 biomes, l'accumulation, les transitions et les LOD.

[Entrées près de la caméra, avant/après](biome-validation/tundra_caves_local.png) · [Coupes des grottes](biome-validation/tundra_caves.png) · [Mesures](biome-validation/tundra_caves.json)

**Régénérer le terrain** après mise à jour des shaders. Les mesures OpenGL/Mesa vérifient la géométrie et le pipeline ; le parcours du joueur et les performances sur le GPU Windows restent à apprécier dans le jeu.
