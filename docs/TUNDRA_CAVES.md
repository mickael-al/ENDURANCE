# Tundra : grottes rares accessibles depuis le sol

Le relief de `Tundra` est conservé à l'identique : montagnes, ondulations, cuvettes, bande 3D et paramètres 2D. L'ajout se trouve dans [caves.glsl](../Asset/Shader/ProceduralTerrain/biomes/Tundra/caves.glsl), appelé à la fin de [volume.C.glsl](../Asset/Shader/ProceduralTerrain/biomes/Tundra/volume.C.glsl), après les formations existantes.

Une petite partie des sites existants accueille une salle souterraine, une salle secondaire et une galerie à deux segments qui débouche au sol. À `uDetailScale = 0.35`, la salle principale mesure environ 180–250 m de longueur et 36–44 m de hauteur, avec son centre à 32–40 m sous le terrain. Le bruit 3D déforme ses parois de quelques mètres. Les hauteurs suivent le terrain local, comme les autres sculptures du biome ; ce n'est pas un plancher horizontal fixe en coordonnées monde.

## Réglages

Les constantes sont au début de `Tundra/caves.glsl` :

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `TUNDRA_CAVE_REGION_SCALE` | 9000 m | Taille du bruit qui organise les régions où des grottes sont possibles ; ce n'est pas la distance entre deux entrées. |
| `TUNDRA_CAVE_REGION_THRESHOLD` | 0,58 | Augmenter réduit la surface des régions admissibles. |
| `TUNDRA_CAVE_SITE_CHANCE` | 0,35 | Fraction des sites existants retenus avant le filtre régional ; diminuer raréfie les grottes. Mettre `0.0` les désactive. |
| `TUNDRA_CAVE_WALL_AMPLITUDE` | 4 m | Déformation maximale des parois, à l'échelle verticale de référence. |

Le masque régional utilise un bruit de valeur continu dans un domaine déformé, évalué au centre de chaque site. La sélection concerne la grotte entière : une limite du masque ne peut pas couper sa galerie. La forme reste contenue loin des frontières des cellules de placement. Les orientations, tailles et positions des sites existants varient ; tous les sites ne contiennent pas une grotte.

Les salles et leur galerie occupent la bande 3D historique de 512 unités brutes, sans ouvrir de bande supplémentaire. Les autres formations gardent donc leur fondu d'origine. L'opération enlève uniquement de la matière et son influence s'annule progressivement hors de la cavité.

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

Il vérifie la conservation exacte des colonnes et du volume historique lorsque le nouvel appel est désactivé, la rareté, le sol et le plafond des salles, leur connexion à l'air extérieur, ainsi que l'accumulation réelle et les points communs aux LOD près d'une paroi excavée. Le résultat marin est également comparé bit à bit à la référence.

Les six grottes échantillonnées sur deux seeds ont toutes un sol et un plafond, avec 100 % de l'air nouvellement excavé sous plafond connecté à l'extérieur. Leur dégagement vertical mesuré est de 35–50 m. Sur deux cartes de 2304 km², seulement 0,037–0,065 % des colonnes échantillonnées changent. Les 2662 densités de production et 250 points LOD ciblés concordent ; les 266256 densités marines comparées restent identiques. Les 26 biomes passent également les tests généraux de compilation, accumulation, transitions et LOD.

[Coupes des grottes](biome-validation/tundra_caves.png) · [Mesures](biome-validation/tundra_caves.json)

**Régénérer le terrain** après mise à jour des shaders. Les mesures OpenGL/Mesa vérifient la géométrie et le pipeline ; le parcours du joueur et les performances sur le GPU Windows restent à apprécier dans le jeu.
